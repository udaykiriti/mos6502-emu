#include <stdio.h>
#include <stdlib.h>
// https://www.masswerk.at/6502/6502_instruction_set.html
using Byte = unsigned char;
using Word = unsigned short;
using u32 = unsigned int;
using s32 = signed int;

struct Mem {
  static constexpr u32 MAX_MEM = 1024 * 64;
  Byte Data[MAX_MEM];

  void Initialise() {
    for (u32 i = 0; i < MAX_MEM; i++) {
      Data[i] = 0;
    }
  }

  // read 1 byte
  Byte operator[](u32 Address) const {
    // assert here Address is < MAX_MEM
    return Data[Address];
  }

  // write 1 byte
  Byte& operator[](u32 Address) {
    // assert here Address is < MAX_MEM
    return Data[Address];
  }

  // write 2 bytes
  void WriteWord(Word Value, u32 Address, s32& Cycles) {
    Data[Address] = Value & 0xFF;
    Data[Address + 1] = (Value >> 8);
    Cycles -= 2;
  }
};

struct CPU {
  Word PC; // Program counter
  Byte SP; // stack pointer (8-bit, indexes into page 0x0100-0x01FF)
  Byte A, X, Y; // registers

  Byte C : 1; // status flag
  Byte Z : 1; // status flag
  Byte I : 1; // status flag
  Byte D : 1; // status flag
  Byte B : 1; // status flag
  Byte V : 1; // status flag
  Byte N : 1; // status flag

  void Reset(Mem& memory) {
    PC = 0xFFFC;
    SP = 0xFF; // real 6502 resets SP to 0xFD, using 0xFF here is fine for this toy
    C = Z = I = D = B = V = N = 0;
    A = X = Y = 0;
    memory.Initialise();
  }

  Byte FetchByte(s32& Cycles, Mem& memory) {
    Byte Data = memory[PC];
    PC++;
    Cycles--;
    return Data;
  }

  Word FetchWord(s32& Cycles, Mem& memory) {
    // 6502 is little endian
    Word Data = memory[PC];
    PC++;
    Data |= (memory[PC] << 8);
    PC++;
    Cycles -= 2;
    return Data;
  }

  // reads a byte from an arbitrary 16-bit address (not PC)
  Byte ReadByte(s32& Cycles, Word Address, Mem& memory) {
    Byte Data = memory[Address];
    Cycles--;
    return Data;
  }

  // stack lives at 0x0100-0x01FF, grows downward
  void PushWordToStack(s32& Cycles, Mem& memory, Word Value) {
    memory.WriteWord(Value, 0x0100 + SP - 1, Cycles);
    SP -= 2;
  }

  Word PopWordFromStack(s32& Cycles, Mem& memory) {
    SP += 2;
    Word Value = memory[0x0100 + SP - 1];
    Value |= (memory[0x0100 + SP] << 8);
    Cycles -= 2;
    return Value;
  }

  // opcodes
  static constexpr Byte
      INS_LDA_IM  = 0xA9,
      INS_LDA_ZP  = 0xA5,
      INS_LDA_ZPX = 0xB5,
      INS_LDA_ABS = 0xAD,

      INS_LDX_IM  = 0xA2,
      INS_LDX_ZP  = 0xA6,
      INS_LDX_ZPY = 0xB6,

      INS_LDY_IM  = 0xA0,
      INS_LDY_ZP  = 0xA4,
      INS_LDY_ZPX = 0xB4,

      INS_JSR     = 0x20,
      INS_RTS     = 0x60,
      INS_NOP     = 0xEA;

  void SetStatusFor(Byte Reg) {
    Z = (Reg == 0);
    N = (Reg & 0b10000000) > 0;
  }

  void Execute(s32 Cycles, Mem& memory) {
    while (Cycles > 0) {
      Byte Ins = FetchByte(Cycles, memory);
      switch (Ins) {
        case INS_LDA_IM: {
          A = FetchByte(Cycles, memory);
          SetStatusFor(A);
        } break;

        case INS_LDA_ZP: {
          Byte ZeroPageAddress = FetchByte(Cycles, memory);
          A = ReadByte(Cycles, ZeroPageAddress, memory);
          SetStatusFor(A);
        } break;

        case INS_LDA_ZPX: {
          Byte ZeroPageAddress = FetchByte(Cycles, memory);
          ZeroPageAddress += X;
          Cycles--; // extra cycle for the add
          A = ReadByte(Cycles, ZeroPageAddress, memory);
          SetStatusFor(A);
        } break;

        case INS_LDA_ABS: {
          Word AbsAddress = FetchWord(Cycles, memory);
          A = ReadByte(Cycles, AbsAddress, memory);
          SetStatusFor(A);
        } break;

        case INS_LDX_IM: {
          X = FetchByte(Cycles, memory);
          SetStatusFor(X);
        } break;

        case INS_LDX_ZP: {
          Byte ZeroPageAddress = FetchByte(Cycles, memory);
          X = ReadByte(Cycles, ZeroPageAddress, memory);
          SetStatusFor(X);
        } break;

        case INS_LDX_ZPY: {
          Byte ZeroPageAddress = FetchByte(Cycles, memory);
          ZeroPageAddress += Y;
          Cycles--;
          X = ReadByte(Cycles, ZeroPageAddress, memory);
          SetStatusFor(X);
        } break;

        case INS_LDY_IM: {
          Y = FetchByte(Cycles, memory);
          SetStatusFor(Y);
        } break;

        case INS_LDY_ZP: {
          Byte ZeroPageAddress = FetchByte(Cycles, memory);
          Y = ReadByte(Cycles, ZeroPageAddress, memory);
          SetStatusFor(Y);
        } break;

        case INS_LDY_ZPX: {
          Byte ZeroPageAddress = FetchByte(Cycles, memory);
          ZeroPageAddress += X;
          Cycles--;
          Y = ReadByte(Cycles, ZeroPageAddress, memory);
          SetStatusFor(Y);
        } break;

        case INS_JSR: {
          Word SubAddr = FetchWord(Cycles, memory);
          PushWordToStack(Cycles, memory, PC - 1);
          PC = SubAddr;
          Cycles--;
        } break;

        case INS_RTS: {
          Word ReturnAddress = PopWordFromStack(Cycles, memory);
          PC = ReturnAddress + 1;
          Cycles -= 2; // approximate; real RTS timing differs slightly
        } break;

        case INS_NOP: {
          Cycles--;
        } break;

        default: {
          printf("Instruction not handled: 0x%02X\n", Ins);
          Cycles--; // avoid infinite loop on unknown opcode
        } break;
      }
    }
  }
};

int main() {
  Mem mem;
  CPU cpu;
  cpu.Reset(mem);

  // program: JSR $4242 ; at $4242: LDA #$84 ; RTS
  mem[0xFFFC] = CPU::INS_JSR;
  mem[0xFFFD] = 0x42;
  mem[0xFFFE] = 0x42;
  mem[0x4242] = CPU::INS_LDA_IM;
  mem[0x4243] = 0x84;
  mem[0x4244] = CPU::INS_RTS;

  cpu.Execute(6 + 2 + 6, mem); // JSR(6) + LDA_IM(2) + RTS(6)

  printf("A = 0x%02X, PC = 0x%04X, SP = 0x%02X\n", cpu.A, cpu.PC, cpu.SP);
  return 0;
}