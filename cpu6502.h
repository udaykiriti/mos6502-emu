#pragma once
#include <stdio.h>
#include <stdlib.h>
// Reference: https://www.masswerk.at/6502/6502_instruction_set.html
// Covers the full official 6502 instruction set (no illegal/undocumented
// opcodes). Decimal (BCD) mode for ADC/SBC is not implemented (D flag is
// tracked but ignored in math).

using Byte = unsigned char;
using Word = unsigned short;
using u32 = unsigned int;
using s32 = signed int;

struct Mem {
  static constexpr u32 MAX_MEM = 1024 * 64;
  Byte Data[MAX_MEM];

  void Initialise() {
    for (u32 i = 0; i < MAX_MEM; i++)
      Data[i] = 0;
  }

  Byte operator[](u32 Address) const { return Data[Address]; }
  Byte &operator[](u32 Address) { return Data[Address]; }

  void WriteWord(Word Value, u32 Address, s32 &Cycles) {
    Data[Address] = Value & 0xFF;
    Data[Address + 1] = (Value >> 8);
    Cycles -= 2;
  }
};

struct CPU {
  Word PC;
  Byte SP; // 8-bit, indexes page 0x0100-0x01FF
  Byte A, X, Y;

  Byte C : 1;
  Byte Z : 1;
  Byte I : 1;
  Byte D : 1;
  Byte B : 1;
  Byte Unused : 1; // always reads as 1 on real hardware
  Byte V : 1;
  Byte N : 1;

  void Reset(Mem &memory) {
    PC = 0xFFFC;
    SP = 0xFF;
    C = Z = I = D = B = V = N = 0;
    Unused = 1;
    A = X = Y = 0;
    memory.Initialise();
  }

  // fetch / read / write 

  Byte FetchByte(s32 &Cycles, Mem &memory) {
    Byte Data = memory[PC];
    PC++;
    Cycles--;
    return Data;
  }

  Word FetchWord(s32 &Cycles, Mem &memory) {
    Word Data = memory[PC];
    PC++;
    Data |= (memory[PC] << 8);
    PC++;
    Cycles -= 2;
    return Data;
  }

  Byte ReadByte(s32 &Cycles, Word Address, Mem &memory) {
    Byte Data = memory[Address];
    Cycles--;
    return Data;
  }

  void WriteByte(Byte Value, Word Address, Mem &memory, s32 &Cycles) {
    memory[Address] = Value;
    Cycles--;
  }

  // stack 

  void PushByteToStack(s32 &Cycles, Mem &memory, Byte Value) {
    memory[0x0100 + SP] = Value;
    SP--;
    Cycles--;
  }

  Byte PopByteFromStack(s32 &Cycles, Mem &memory) {
    SP++;
    Byte Value = memory[0x0100 + SP];
    Cycles--;
    return Value;
  }

  void PushWordToStack(s32 &Cycles, Mem &memory, Word Value) {
    PushByteToStack(Cycles, memory, (Value >> 8) & 0xFF);
    PushByteToStack(Cycles, memory, Value & 0xFF);
  }

  Word PopWordFromStack(s32 &Cycles, Mem &memory) {
    Byte Lo = PopByteFromStack(Cycles, memory);
    Byte Hi = PopByteFromStack(Cycles, memory);
    return (Word)Lo | ((Word)Hi << 8);
  }

  Byte GetPS() const {
    return (C << 0) | (Z << 1) | (I << 2) | (D << 3) | (B << 4) | (1 << 5) |
           (V << 6) | (N << 7);
  }

  void SetPS(Byte Value) {
    C = (Value >> 0) & 1;
    Z = (Value >> 1) & 1;
    I = (Value >> 2) & 1;
    D = (Value >> 3) & 1;
    B = (Value >> 4) & 1;
    V = (Value >> 6) & 1;
    N = (Value >> 7) & 1;
  }

  // addressing modes (return effective address; consume cycles)

  Word AddrZeroPage(s32 &Cycles, Mem &memory) {
    return FetchByte(Cycles, memory);
  }

  Word AddrZeroPageX(s32 &Cycles, Mem &memory) {
    Byte Addr = FetchByte(Cycles, memory);
    Addr += X;
    Cycles--;
    return Addr;
  }

  Word AddrZeroPageY(s32 &Cycles, Mem &memory) {
    Byte Addr = FetchByte(Cycles, memory);
    Addr += Y;
    Cycles--;
    return Addr;
  }

  Word AddrAbsolute(s32 &Cycles, Mem &memory) {
    return FetchWord(Cycles, memory);
  }

  // PageCrossPenalty=true: only charges the extra cycle if the page boundary is
  // crossed (used by read instructions). Pass false for RMW/store, which always
  // pay it, and add the cycle manually at the call site.
  Word AddrAbsoluteX(s32 &Cycles, Mem &memory, bool PageCrossPenalty) {
    Word Base = FetchWord(Cycles, memory);
    Word Addr = Base + X;
    if (PageCrossPenalty && ((Base ^ Addr) >> 8) != 0)
      Cycles--;
    return Addr;
  }

  Word AddrAbsoluteY(s32 &Cycles, Mem &memory, bool PageCrossPenalty) {
    Word Base = FetchWord(Cycles, memory);
    Word Addr = Base + Y;
    if (PageCrossPenalty && ((Base ^ Addr) >> 8) != 0)
      Cycles--;
    return Addr;
  }

  Word AddrIndirectX(s32 &Cycles, Mem &memory) {
    Byte ZPAddr = FetchByte(Cycles, memory);
    ZPAddr += X;
    Cycles--; // extra cycle for the index add
    Word Addr = memory[ZPAddr] | (memory[(Byte)(ZPAddr + 1)] << 8);
    Cycles -= 2;
    return Addr;
  }

  Word AddrIndirectY(s32 &Cycles, Mem &memory, bool PageCrossPenalty) {
    Byte ZPAddr = FetchByte(Cycles, memory);
    Word Base = memory[ZPAddr] | (memory[(Byte)(ZPAddr + 1)] << 8);
    Cycles -= 2;
    Word Addr = Base + Y;
    if (PageCrossPenalty && ((Base ^ Addr) >> 8) != 0)
      Cycles--;
    return Addr;
  }

  // flag / math helpers

  void SetZN(Byte Reg) {
    Z = (Reg == 0);
    N = (Reg & 0b10000000) > 0;
  }

  void ADC(Byte Operand) {
    bool SameSign = !((A ^ Operand) & 0x80);
    u32 Sum = A + Operand + C;
    C = Sum > 0xFF;
    Byte Result = (Byte)Sum;
    V = SameSign && ((A ^ Result) & 0x80);
    A = Result;
    SetZN(A);
  }

  void SBC(Byte Operand) {
    ADC(~Operand); // A - M - (1-C) == A + ~M + C
  }

  void Compare(Byte Reg, Byte Operand) {
    Byte Result = Reg - Operand;
    C = Reg >= Operand;
    SetZN(Result);
  }

  Byte ASL(Byte Value) {
    C = (Value & 0x80) > 0;
    Value <<= 1;
    SetZN(Value);
    return Value;
  }

  Byte LSR(Byte Value) {
    C = Value & 0x01;
    Value >>= 1;
    SetZN(Value);
    return Value;
  }

  Byte ROL(Byte Value) {
    bool NewCarry = (Value & 0x80) > 0;
    Value = (Value << 1) | C;
    C = NewCarry;
    SetZN(Value);
    return Value;
  }

  Byte ROR(Byte Value) {
    bool NewCarry = Value & 0x01;
    Value = (Value >> 1) | (C << 7);
    C = NewCarry;
    SetZN(Value);
    return Value;
  }

  void Branch(s32 &Cycles, Mem &memory, bool Condition) {
    signed char Offset = (signed char)FetchByte(Cycles, memory);
    if (Condition) {
      Word OldPC = PC;
      PC += Offset;
      Cycles--; // branch taken
      if ((OldPC >> 8) != (PC >> 8))
        Cycles--; // crossed a page
    }
  }

  // opcodes 

  enum : Byte {
    INS_LDA_IM = 0xA9,
    INS_LDA_ZP = 0xA5,
    INS_LDA_ZPX = 0xB5,
    INS_LDA_ABS = 0xAD,
    INS_LDA_ABSX = 0xBD,
    INS_LDA_ABSY = 0xB9,
    INS_LDA_INDX = 0xA1,
    INS_LDA_INDY = 0xB1,

    INS_LDX_IM = 0xA2,
    INS_LDX_ZP = 0xA6,
    INS_LDX_ZPY = 0xB6,
    INS_LDX_ABS = 0xAE,
    INS_LDX_ABSY = 0xBE,
    INS_LDY_IM = 0xA0,
    INS_LDY_ZP = 0xA4,
    INS_LDY_ZPX = 0xB4,
    INS_LDY_ABS = 0xAC,
    INS_LDY_ABSX = 0xBC,

    INS_STA_ZP = 0x85,
    INS_STA_ZPX = 0x95,
    INS_STA_ABS = 0x8D,
    INS_STA_ABSX = 0x9D,
    INS_STA_ABSY = 0x99,
    INS_STA_INDX = 0x81,
    INS_STA_INDY = 0x91,
    INS_STX_ZP = 0x86,
    INS_STX_ZPY = 0x96,
    INS_STX_ABS = 0x8E,
    INS_STY_ZP = 0x84,
    INS_STY_ZPX = 0x94,
    INS_STY_ABS = 0x8C,

    INS_TAX = 0xAA,
    INS_TAY = 0xA8,
    INS_TXA = 0x8A,
    INS_TYA = 0x98,
    INS_TSX = 0xBA,
    INS_TXS = 0x9A,
    INS_PHA = 0x48,
    INS_PHP = 0x08,
    INS_PLA = 0x68,
    INS_PLP = 0x28,

    INS_AND_IM = 0x29,
    INS_AND_ZP = 0x25,
    INS_AND_ZPX = 0x35,
    INS_AND_ABS = 0x2D,
    INS_AND_ABSX = 0x3D,
    INS_AND_ABSY = 0x39,
    INS_AND_INDX = 0x21,
    INS_AND_INDY = 0x31,

    INS_EOR_IM = 0x49,
    INS_EOR_ZP = 0x45,
    INS_EOR_ZPX = 0x55,
    INS_EOR_ABS = 0x4D,
    INS_EOR_ABSX = 0x5D,
    INS_EOR_ABSY = 0x59,
    INS_EOR_INDX = 0x41,
    INS_EOR_INDY = 0x51,

    INS_ORA_IM = 0x09,
    INS_ORA_ZP = 0x05,
    INS_ORA_ZPX = 0x15,
    INS_ORA_ABS = 0x0D,
    INS_ORA_ABSX = 0x1D,
    INS_ORA_ABSY = 0x19,
    INS_ORA_INDX = 0x01,
    INS_ORA_INDY = 0x11,

    INS_BIT_ZP = 0x24,
    INS_BIT_ABS = 0x2C,

    INS_ADC_IM = 0x69,
    INS_ADC_ZP = 0x65,
    INS_ADC_ZPX = 0x75,
    INS_ADC_ABS = 0x6D,
    INS_ADC_ABSX = 0x7D,
    INS_ADC_ABSY = 0x79,
    INS_ADC_INDX = 0x61,
    INS_ADC_INDY = 0x71,

    INS_SBC_IM = 0xE9,
    INS_SBC_ZP = 0xE5,
    INS_SBC_ZPX = 0xF5,
    INS_SBC_ABS = 0xED,
    INS_SBC_ABSX = 0xFD,
    INS_SBC_ABSY = 0xF9,
    INS_SBC_INDX = 0xE1,
    INS_SBC_INDY = 0xF1,

    INS_CMP_IM = 0xC9,
    INS_CMP_ZP = 0xC5,
    INS_CMP_ZPX = 0xD5,
    INS_CMP_ABS = 0xCD,
    INS_CMP_ABSX = 0xDD,
    INS_CMP_ABSY = 0xD9,
    INS_CMP_INDX = 0xC1,
    INS_CMP_INDY = 0xD1,
    INS_CPX_IM = 0xE0,
    INS_CPX_ZP = 0xE4,
    INS_CPX_ABS = 0xEC,
    INS_CPY_IM = 0xC0,
    INS_CPY_ZP = 0xC4,
    INS_CPY_ABS = 0xCC,

    INS_INC_ZP = 0xE6,
    INS_INC_ZPX = 0xF6,
    INS_INC_ABS = 0xEE,
    INS_INC_ABSX = 0xFE,
    INS_INX = 0xE8,
    INS_INY = 0xC8,
    INS_DEC_ZP = 0xC6,
    INS_DEC_ZPX = 0xD6,
    INS_DEC_ABS = 0xCE,
    INS_DEC_ABSX = 0xDE,
    INS_DEX = 0xCA,
    INS_DEY = 0x88,

    INS_ASL_ACC = 0x0A,
    INS_ASL_ZP = 0x06,
    INS_ASL_ZPX = 0x16,
    INS_ASL_ABS = 0x0E,
    INS_ASL_ABSX = 0x1E,
    INS_LSR_ACC = 0x4A,
    INS_LSR_ZP = 0x46,
    INS_LSR_ZPX = 0x56,
    INS_LSR_ABS = 0x4E,
    INS_LSR_ABSX = 0x5E,
    INS_ROL_ACC = 0x2A,
    INS_ROL_ZP = 0x26,
    INS_ROL_ZPX = 0x36,
    INS_ROL_ABS = 0x2E,
    INS_ROL_ABSX = 0x3E,
    INS_ROR_ACC = 0x6A,
    INS_ROR_ZP = 0x66,
    INS_ROR_ZPX = 0x76,
    INS_ROR_ABS = 0x6E,
    INS_ROR_ABSX = 0x7E,

    INS_JMP_ABS = 0x4C,
    INS_JMP_IND = 0x6C,
    INS_JSR = 0x20,
    INS_RTS = 0x60,

    INS_BCC = 0x90,
    INS_BCS = 0xB0,
    INS_BEQ = 0xF0,
    INS_BMI = 0x30,
    INS_BNE = 0xD0,
    INS_BPL = 0x10,
    INS_BVC = 0x50,
    INS_BVS = 0x70,

    INS_CLC = 0x18,
    INS_CLD = 0xD8,
    INS_CLI = 0x58,
    INS_CLV = 0xB8,
    INS_SEC = 0x38,
    INS_SED = 0xF8,
    INS_SEI = 0x78,

    INS_BRK = 0x00,
    INS_NOP = 0xEA,
    INS_RTI = 0x40,
  };

  // Loads a value using the given opcode's addressing mode. `Byte* RegOrNull`
  // used only so the same helper can serve LDA/LDX/LDY via a switch outside.
  void Execute(s32 Cycles, Mem &memory) {
    while (Cycles > 0) {
      Byte Ins = FetchByte(Cycles, memory);
      switch (Ins) {

      // LDA 
      case INS_LDA_IM:
        A = FetchByte(Cycles, memory);
        SetZN(A);
        break;
      case INS_LDA_ZP:
        A = ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_LDA_ZPX:
        A = ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_LDA_ABS:
        A = ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_LDA_ABSX:
        A = ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_LDA_ABSY:
        A = ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_LDA_INDX:
        A = ReadByte(Cycles, AddrIndirectX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_LDA_INDY:
        A = ReadByte(Cycles, AddrIndirectY(Cycles, memory, true), memory);
        SetZN(A);
        break;

      // LDX 
      case INS_LDX_IM:
        X = FetchByte(Cycles, memory);
        SetZN(X);
        break;
      case INS_LDX_ZP:
        X = ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory);
        SetZN(X);
        break;
      case INS_LDX_ZPY:
        X = ReadByte(Cycles, AddrZeroPageY(Cycles, memory), memory);
        SetZN(X);
        break;
      case INS_LDX_ABS:
        X = ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory);
        SetZN(X);
        break;
      case INS_LDX_ABSY:
        X = ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory);
        SetZN(X);
        break;

      // LDY 
      case INS_LDY_IM:
        Y = FetchByte(Cycles, memory);
        SetZN(Y);
        break;
      case INS_LDY_ZP:
        Y = ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory);
        SetZN(Y);
        break;
      case INS_LDY_ZPX:
        Y = ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory);
        SetZN(Y);
        break;
      case INS_LDY_ABS:
        Y = ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory);
        SetZN(Y);
        break;
      case INS_LDY_ABSX:
        Y = ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory);
        SetZN(Y);
        break;

      // STA / STX / STY
      case INS_STA_ZP:
        WriteByte(A, AddrZeroPage(Cycles, memory), memory, Cycles);
        break;
      case INS_STA_ZPX:
        WriteByte(A, AddrZeroPageX(Cycles, memory), memory, Cycles);
        break;
      case INS_STA_ABS:
        WriteByte(A, AddrAbsolute(Cycles, memory), memory, Cycles);
        break;
      case INS_STA_ABSX: {
        Word Addr = AddrAbsoluteX(Cycles, memory, false);
        Cycles--;
        WriteByte(A, Addr, memory, Cycles);
      } break;
      case INS_STA_ABSY: {
        Word Addr = AddrAbsoluteY(Cycles, memory, false);
        Cycles--;
        WriteByte(A, Addr, memory, Cycles);
      } break;
      case INS_STA_INDX:
        WriteByte(A, AddrIndirectX(Cycles, memory), memory, Cycles);
        break;
      case INS_STA_INDY: {
        Word Addr = AddrIndirectY(Cycles, memory, false);
        Cycles--;
        WriteByte(A, Addr, memory, Cycles);
      } break;
      case INS_STX_ZP:
        WriteByte(X, AddrZeroPage(Cycles, memory), memory, Cycles);
        break;
      case INS_STX_ZPY:
        WriteByte(X, AddrZeroPageY(Cycles, memory), memory, Cycles);
        break;
      case INS_STX_ABS:
        WriteByte(X, AddrAbsolute(Cycles, memory), memory, Cycles);
        break;
      case INS_STY_ZP:
        WriteByte(Y, AddrZeroPage(Cycles, memory), memory, Cycles);
        break;
      case INS_STY_ZPX:
        WriteByte(Y, AddrZeroPageX(Cycles, memory), memory, Cycles);
        break;
      case INS_STY_ABS:
        WriteByte(Y, AddrAbsolute(Cycles, memory), memory, Cycles);
        break;

      // register transfers
      case INS_TAX:
        X = A;
        SetZN(X);
        Cycles--;
        break;
      case INS_TAY:
        Y = A;
        SetZN(Y);
        Cycles--;
        break;
      case INS_TXA:
        A = X;
        SetZN(A);
        Cycles--;
        break;
      case INS_TYA:
        A = Y;
        SetZN(A);
        Cycles--;
        break;
      case INS_TSX:
        X = SP;
        SetZN(X);
        Cycles--;
        break;
      case INS_TXS:
        SP = X;
        Cycles--;
        break;

      // stack 
      // PHA/PHP/PLA/PLP each have a "dummy" internal cycle beyond the raw
      // stack access (real 6502 timing: PHA/PHP=3 cycles, PLA/PLP=4 cycles).
      case INS_PHA:
        Cycles--;
        PushByteToStack(Cycles, memory, A);
        break;
      case INS_PHP:
        Cycles--;
        PushByteToStack(Cycles, memory, GetPS() | 0b00110000);
        break;
      case INS_PLA:
        Cycles--;
        A = PopByteFromStack(Cycles, memory);
        SetZN(A);
        Cycles--;
        break;
      case INS_PLP:
        Cycles--;
        SetPS(PopByteFromStack(Cycles, memory));
        Cycles--;
        break;

      // AND 
      case INS_AND_IM:
        A &= FetchByte(Cycles, memory);
        SetZN(A);
        break;
      case INS_AND_ZP:
        A &= ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_AND_ZPX:
        A &= ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_AND_ABS:
        A &= ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_AND_ABSX:
        A &= ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_AND_ABSY:
        A &= ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_AND_INDX:
        A &= ReadByte(Cycles, AddrIndirectX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_AND_INDY:
        A &= ReadByte(Cycles, AddrIndirectY(Cycles, memory, true), memory);
        SetZN(A);
        break;

      // EOR 
      case INS_EOR_IM:
        A ^= FetchByte(Cycles, memory);
        SetZN(A);
        break;
      case INS_EOR_ZP:
        A ^= ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_EOR_ZPX:
        A ^= ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_EOR_ABS:
        A ^= ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_EOR_ABSX:
        A ^= ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_EOR_ABSY:
        A ^= ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_EOR_INDX:
        A ^= ReadByte(Cycles, AddrIndirectX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_EOR_INDY:
        A ^= ReadByte(Cycles, AddrIndirectY(Cycles, memory, true), memory);
        SetZN(A);
        break;

      // ORA
      case INS_ORA_IM:
        A |= FetchByte(Cycles, memory);
        SetZN(A);
        break;
      case INS_ORA_ZP:
        A |= ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_ORA_ZPX:
        A |= ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_ORA_ABS:
        A |= ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_ORA_ABSX:
        A |= ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_ORA_ABSY:
        A |= ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory);
        SetZN(A);
        break;
      case INS_ORA_INDX:
        A |= ReadByte(Cycles, AddrIndirectX(Cycles, memory), memory);
        SetZN(A);
        break;
      case INS_ORA_INDY:
        A |= ReadByte(Cycles, AddrIndirectY(Cycles, memory, true), memory);
        SetZN(A);
        break;

      // BIT 
      case INS_BIT_ZP:
      case INS_BIT_ABS: {
        Word Addr = (Ins == INS_BIT_ZP) ? AddrZeroPage(Cycles, memory)
                                        : AddrAbsolute(Cycles, memory);
        Byte Value = ReadByte(Cycles, Addr, memory);
        Z = (A & Value) == 0;
        N = (Value & 0x80) > 0;
        V = (Value & 0x40) > 0;
      } break;

      // ADC / SBC
      case INS_ADC_IM:
        ADC(FetchByte(Cycles, memory));
        break;
      case INS_ADC_ZP:
        ADC(ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory));
        break;
      case INS_ADC_ZPX:
        ADC(ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory));
        break;
      case INS_ADC_ABS:
        ADC(ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory));
        break;
      case INS_ADC_ABSX:
        ADC(ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory));
        break;
      case INS_ADC_ABSY:
        ADC(ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory));
        break;
      case INS_ADC_INDX:
        ADC(ReadByte(Cycles, AddrIndirectX(Cycles, memory), memory));
        break;
      case INS_ADC_INDY:
        ADC(ReadByte(Cycles, AddrIndirectY(Cycles, memory, true), memory));
        break;

      case INS_SBC_IM:
        SBC(FetchByte(Cycles, memory));
        break;
      case INS_SBC_ZP:
        SBC(ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory));
        break;
      case INS_SBC_ZPX:
        SBC(ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory));
        break;
      case INS_SBC_ABS:
        SBC(ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory));
        break;
      case INS_SBC_ABSX:
        SBC(ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory));
        break;
      case INS_SBC_ABSY:
        SBC(ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory));
        break;
      case INS_SBC_INDX:
        SBC(ReadByte(Cycles, AddrIndirectX(Cycles, memory), memory));
        break;
      case INS_SBC_INDY:
        SBC(ReadByte(Cycles, AddrIndirectY(Cycles, memory, true), memory));
        break;

      // CMP / CPX / CPY 
      case INS_CMP_IM:
        Compare(A, FetchByte(Cycles, memory));
        break;
      case INS_CMP_ZP:
        Compare(A, ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory));
        break;
      case INS_CMP_ZPX:
        Compare(A, ReadByte(Cycles, AddrZeroPageX(Cycles, memory), memory));
        break;
      case INS_CMP_ABS:
        Compare(A, ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory));
        break;
      case INS_CMP_ABSX:
        Compare(A,
                ReadByte(Cycles, AddrAbsoluteX(Cycles, memory, true), memory));
        break;
      case INS_CMP_ABSY:
        Compare(A,
                ReadByte(Cycles, AddrAbsoluteY(Cycles, memory, true), memory));
        break;
      case INS_CMP_INDX:
        Compare(A, ReadByte(Cycles, AddrIndirectX(Cycles, memory), memory));
        break;
      case INS_CMP_INDY:
        Compare(A,
                ReadByte(Cycles, AddrIndirectY(Cycles, memory, true), memory));
        break;

      case INS_CPX_IM:
        Compare(X, FetchByte(Cycles, memory));
        break;
      case INS_CPX_ZP:
        Compare(X, ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory));
        break;
      case INS_CPX_ABS:
        Compare(X, ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory));
        break;
      case INS_CPY_IM:
        Compare(Y, FetchByte(Cycles, memory));
        break;
      case INS_CPY_ZP:
        Compare(Y, ReadByte(Cycles, AddrZeroPage(Cycles, memory), memory));
        break;
      case INS_CPY_ABS:
        Compare(Y, ReadByte(Cycles, AddrAbsolute(Cycles, memory), memory));
        break;

      // INC / DEC (read-modify-write; +1 cycle for the write-back) 
      case INS_INC_ZP: {
        Word Addr = AddrZeroPage(Cycles, memory);
        Byte V2 = ReadByte(Cycles, Addr, memory) + 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_INC_ZPX: {
        Word Addr = AddrZeroPageX(Cycles, memory);
        Byte V2 = ReadByte(Cycles, Addr, memory) + 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_INC_ABS: {
        Word Addr = AddrAbsolute(Cycles, memory);
        Byte V2 = ReadByte(Cycles, Addr, memory) + 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_INC_ABSX: {
        Word Addr = AddrAbsoluteX(Cycles, memory, false);
        Cycles--;
        Byte V2 = ReadByte(Cycles, Addr, memory) + 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_INX:
        X++;
        SetZN(X);
        Cycles--;
        break;
      case INS_INY:
        Y++;
        SetZN(Y);
        Cycles--;
        break;

      case INS_DEC_ZP: {
        Word Addr = AddrZeroPage(Cycles, memory);
        Byte V2 = ReadByte(Cycles, Addr, memory) - 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_DEC_ZPX: {
        Word Addr = AddrZeroPageX(Cycles, memory);
        Byte V2 = ReadByte(Cycles, Addr, memory) - 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_DEC_ABS: {
        Word Addr = AddrAbsolute(Cycles, memory);
        Byte V2 = ReadByte(Cycles, Addr, memory) - 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_DEC_ABSX: {
        Word Addr = AddrAbsoluteX(Cycles, memory, false);
        Cycles--;
        Byte V2 = ReadByte(Cycles, Addr, memory) - 1;
        Cycles--;
        SetZN(V2);
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_DEX:
        X--;
        SetZN(X);
        Cycles--;
        break;
      case INS_DEY:
        Y--;
        SetZN(Y);
        Cycles--;
        break;

      //shifts / rotates
      case INS_ASL_ACC:
        A = ASL(A);
        Cycles--;
        break;
      case INS_ASL_ZP: {
        Word Addr = AddrZeroPage(Cycles, memory);
        Byte V2 = ASL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ASL_ZPX: {
        Word Addr = AddrZeroPageX(Cycles, memory);
        Byte V2 = ASL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ASL_ABS: {
        Word Addr = AddrAbsolute(Cycles, memory);
        Byte V2 = ASL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ASL_ABSX: {
        Word Addr = AddrAbsoluteX(Cycles, memory, false);
        Cycles--;
        Byte V2 = ASL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;

      case INS_LSR_ACC:
        A = LSR(A);
        Cycles--;
        break;
      case INS_LSR_ZP: {
        Word Addr = AddrZeroPage(Cycles, memory);
        Byte V2 = LSR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_LSR_ZPX: {
        Word Addr = AddrZeroPageX(Cycles, memory);
        Byte V2 = LSR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_LSR_ABS: {
        Word Addr = AddrAbsolute(Cycles, memory);
        Byte V2 = LSR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_LSR_ABSX: {
        Word Addr = AddrAbsoluteX(Cycles, memory, false);
        Cycles--;
        Byte V2 = LSR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;

      case INS_ROL_ACC:
        A = ROL(A);
        Cycles--;
        break;
      case INS_ROL_ZP: {
        Word Addr = AddrZeroPage(Cycles, memory);
        Byte V2 = ROL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ROL_ZPX: {
        Word Addr = AddrZeroPageX(Cycles, memory);
        Byte V2 = ROL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ROL_ABS: {
        Word Addr = AddrAbsolute(Cycles, memory);
        Byte V2 = ROL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ROL_ABSX: {
        Word Addr = AddrAbsoluteX(Cycles, memory, false);
        Cycles--;
        Byte V2 = ROL(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;

      case INS_ROR_ACC:
        A = ROR(A);
        Cycles--;
        break;
      case INS_ROR_ZP: {
        Word Addr = AddrZeroPage(Cycles, memory);
        Byte V2 = ROR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ROR_ZPX: {
        Word Addr = AddrZeroPageX(Cycles, memory);
        Byte V2 = ROR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ROR_ABS: {
        Word Addr = AddrAbsolute(Cycles, memory);
        Byte V2 = ROR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;
      case INS_ROR_ABSX: {
        Word Addr = AddrAbsoluteX(Cycles, memory, false);
        Cycles--;
        Byte V2 = ROR(ReadByte(Cycles, Addr, memory));
        Cycles--;
        WriteByte(V2, Addr, memory, Cycles);
      } break;

      // jumps / calls 
      case INS_JMP_ABS:
        PC = AddrAbsolute(Cycles, memory);
        break;
      case INS_JMP_IND: {
        Word Ptr = AddrAbsolute(Cycles, memory);
        // Real 6502 bug: if Ptr is at a page boundary (low byte 0xFF), the high
        // byte wraps within the same page instead of crossing. Reproduced here.
        Word Lo = Ptr;
        Word Hi = (Ptr & 0xFF00) | ((Ptr + 1) & 0x00FF);
        PC = memory[Lo] | (memory[Hi] << 8);
        Cycles -= 2;
      } break;

      case INS_JSR: {
        Word SubAddr = FetchWord(Cycles, memory);
        PushWordToStack(Cycles, memory, PC - 1);
        PC = SubAddr;
        Cycles--;
      } break;

      case INS_RTS: {
        Word ReturnAddr = PopWordFromStack(Cycles, memory);
        PC = ReturnAddr + 1;
        Cycles -= 3;
      } break;

      // branches
      case INS_BCC:
        Branch(Cycles, memory, C == 0);
        break;
      case INS_BCS:
        Branch(Cycles, memory, C == 1);
        break;
      case INS_BEQ:
        Branch(Cycles, memory, Z == 1);
        break;
      case INS_BMI:
        Branch(Cycles, memory, N == 1);
        break;
      case INS_BNE:
        Branch(Cycles, memory, Z == 0);
        break;
      case INS_BPL:
        Branch(Cycles, memory, N == 0);
        break;
      case INS_BVC:
        Branch(Cycles, memory, V == 0);
        break;
      case INS_BVS:
        Branch(Cycles, memory, V == 1);
        break;

      // flag instructions
      case INS_CLC:
        C = 0;
        Cycles--;
        break;
      case INS_CLD:
        D = 0;
        Cycles--;
        break;
      case INS_CLI:
        I = 0;
        Cycles--;
        break;
      case INS_CLV:
        V = 0;
        Cycles--;
        break;
      case INS_SEC:
        C = 1;
        Cycles--;
        break;
      case INS_SED:
        D = 1;
        Cycles--;
        break;
      case INS_SEI:
        I = 1;
        Cycles--;
        break;

      // system 
      case INS_NOP:
        Cycles--;
        break;

      case INS_BRK: {
        Cycles--; // BRK is followed by a padding/signature byte, discarded
        PushWordToStack(Cycles, memory, PC + 1);
        PushByteToStack(Cycles, memory, GetPS() | 0b00110000);
        I = 1;
        Byte VecLo = ReadByte(Cycles, 0xFFFE, memory);
        Byte VecHi = ReadByte(Cycles, 0xFFFF, memory);
        PC = VecLo | (VecHi << 8);
      } break;

      case INS_RTI: {
        Cycles -= 2; // dummy next-byte read + stack-pointer increment
        SetPS(PopByteFromStack(Cycles, memory));
        PC = PopWordFromStack(Cycles, memory);
      } break;

      default:
        printf("Instruction not handled: 0x%02X\n", Ins);
        Cycles--; // avoid an infinite loop on unknown opcodes
        break;
      }
    }
  }
};