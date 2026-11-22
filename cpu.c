#include "cpu_common.h"

/* 每个阶段通过 CPU 结构体中的暂存值传递数据。请补全以下六个阶段。 */
void fetch(CPU *cpu)
{
    uint64_t first = read_mem(cpu, cpu->pc, 1);
    if (cpu->stat != AOK) return;

  cpu->icode = (int)(first >> 4);
  cpu->ifun = (int)(first & 0x0F);

  cpu->rA = RNONE;
  cpu->rB = RNONE;
  cpu->valC = 0;

  if (cpu->icode < I_HALT || cpu->icode > I_POPQ) {
    cpu->stat = INS;
    return;
  }

  if (cpu->icode == I_HALT) {
    cpu->valP = cpu->pc + 1;
    cpu->stat = HLT;
    return;
  }

  int hasReg;
  switch (cpu->icode) {
  case I_RRMOVQ: case I_IRMOVQ: case I_ALU:
  case I_RMMOVQ: case I_MRMOVQ: case I_PUSHQ: case I_POPQ:
    hasReg = 1;
    break;
  default:
    hasReg = 0;
    break;
  }

  int hasImm;
  switch (cpu->icode) {
  case I_IRMOVQ: case I_RMMOVQ: case I_MRMOVQ:
  case I_JMP: case I_CALL:
    hasImm = 1;
    break;
  default:
    hasImm = 0;
    break;
  }

  uint64_t len = 1;

  if (hasReg) {
    uint64_t regByte = read_mem(cpu, cpu->pc + len, 1);
    if (cpu->stat != AOK) return;
    cpu->rA = (int)(regByte >> 4);
    cpu->rB = (int)(regByte & 0x0F);
    len++;
  }

  if (hasImm) {
    cpu->valC = read_mem(cpu, cpu->pc + len, 8);
    if (cpu->stat != AOK) return;
    len += 8;
  }

  cpu->valP = cpu->pc + len;
}

void decode(CPU *cpu)
{
    int srcA;
    switch (cpu->icode) {
    case I_RRMOVQ:
    case I_RMMOVQ:
    case I_ALU:
    case I_PUSHQ:
        srcA = cpu->rA;
        break;
    case I_POPQ:
    case I_RET:
        srcA = RSP;
        break;
    default:
        srcA = RNONE;
        break;
    }

    int srcB;
    switch (cpu->icode) {
    case I_ALU:
    case I_RMMOVQ:
    case I_MRMOVQ:
        srcB = cpu->rB;
        break;
    case I_PUSHQ:
    case I_POPQ:
    case I_CALL:
    case I_RET:
        srcB = RSP;
        break;
    default:
        srcB = RNONE;
        break;
    }

    cpu->valA = (srcA == RNONE) ? 0 : cpu->reg[srcA];
    cpu->valB = (srcB == RNONE) ? 0 : cpu->reg[srcB];
}

void execute(CPU *cpu)
{
    uint64_t aluA;
    uint64_t aluB;

    switch (cpu->icode) {
    case I_RRMOVQ:
        aluA = cpu->valA;
        aluB = 0;
        break;
    case I_IRMOVQ:
        aluA = cpu->valC;
        aluB = 0;
        break;
    case I_RMMOVQ:
    case I_MRMOVQ:
        aluA = cpu->valC;
        aluB = cpu->valB;
        break;
    case I_ALU:
        aluA = cpu->valA;
        aluB = cpu->valB;
        break;
    case I_CALL:
    case I_PUSHQ:
        aluA = (uint64_t)-8;
        aluB = cpu->valB;
        break;
    case I_RET:
    case I_POPQ:
        aluA = 8;
        aluB = cpu->valB;
        break;
    default:
        aluA = 0;
        aluB = 0;
        break;
    }

    int aluFun = 0;
    if (cpu->icode == I_ALU) {
        aluFun = cpu->ifun;
    }

    switch (aluFun) {
    case 1:
        cpu->valE = aluB - aluA;
        break;
    case 2:
        cpu->valE = aluB & aluA;
        break;
    case 3:
        cpu->valE = aluB ^ aluA;
        break;
    default:
        cpu->valE = aluB + aluA;
        break;
    }

    cpu->cnd = 0;
    if (cpu->icode == I_JMP || cpu->icode == I_RRMOVQ) {
        cpu->cnd = check_cond(cpu, cpu->ifun);
    }

    if (cpu->icode == I_ALU) {
        int64_t a = (int64_t)aluA;
        int64_t b = (int64_t)aluB;
        int64_t r = (int64_t)cpu->valE;

        if (aluFun == 1) {
            int64_t na = (int64_t)((uint64_t)0 - aluA);
            cpu->of = (b > 0 && na > 0 && r < 0) || (b < 0 && na < 0 && r > 0);
        } else if (aluFun == 0) {
            cpu->of = (b > 0 && a > 0 && r < 0) || (b < 0 && a < 0 && r > 0);
        } else {
            cpu->of = 0;
        }

        cpu->zf = (cpu->valE == 0);
        cpu->sf = (int)((cpu->valE >> 63) & 1);
    }
}

void memory_stage(CPU *cpu)
{
    uint64_t addr = 0;
    uint64_t data = 0;
    int needRead = 0;
    int needWrite = 0;

    switch (cpu->icode) {
    case I_RMMOVQ:
        addr = cpu->valE;
        data = cpu->valA;
        needWrite = 1;
        break;
    case I_PUSHQ:
        addr = cpu->valE;
        data = cpu->valA;
        needWrite = 1;
        break;
    case I_CALL:
        addr = cpu->valE;
        data = cpu->valP;
        needWrite = 1;
        break;
    case I_MRMOVQ:
        addr = cpu->valE;
        needRead = 1;
        break;
    case I_POPQ:
    case I_RET:
        addr = cpu->valA;
        needRead = 1;
        break;
    default:
        break;
    }

    if (needWrite) {
        write_mem(cpu, addr, 8, data);
    }

    cpu->valM = 0;
    if (needRead) {
        cpu->valM = read_mem(cpu, addr, 8);
    }
}

void write_back(CPU *cpu)
{
    int dstE = RNONE;
    int dstM = RNONE;

    switch (cpu->icode) {
    case I_RRMOVQ:
        if (cpu->ifun == 0 || cpu->cnd) {
            dstE = cpu->rB;
        }
        break;
    case I_IRMOVQ:
    case I_ALU:
        dstE = cpu->rB;
        break;
    case I_PUSHQ:
    case I_CALL:
    case I_RET:
        dstE = RSP;
        break;
    case I_POPQ:
        dstE = RSP;
        dstM = cpu->rA;
        break;
    case I_MRMOVQ:
        dstM = cpu->rA;
        break;
    default:
        break;
    }

    if (dstE != RNONE) {
        cpu->reg[dstE] = cpu->valE;
    }
    if (dstM != RNONE) {
        cpu->reg[dstM] = cpu->valM;
    }
}

void update_pc(CPU *cpu)
{
    switch (cpu->icode) {
    case I_CALL:
        cpu->pc = cpu->valC;
        break;
    case I_JMP:
        if (cpu->cnd) {
            cpu->pc = cpu->valC;
        } else {
            cpu->pc = cpu->valP;
        }
        break;
    case I_RET:
        cpu->pc = cpu->valM;
        break;
    default:
        cpu->pc = cpu->valP;
        break;
    }
}
