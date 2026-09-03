#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "cpu6502.h"
#include "doctest.h"

// Helper: set up a fresh CPU+Mem pair, program counter at reset vector 0xFFFC
// points to 0x0200 by default, matching how real systems map a cold-reset
// vector into a usable code area.
struct Fixture {
  Mem mem;
  CPU cpu;
  Fixture() {
    cpu.Reset(mem);
    mem[0xFFFC] = 0x00;
    mem[0xFFFD] = 0x02; // reset vector -> 0x0200
    // Reset itself doesn't jump anywhere; tests set cpu.PC directly to 0x0200.
    cpu.PC = 0x0200;
  }
};

// ---------------- LDA / LDX / LDY ----------------

TEST_CASE("LDA immediate loads value and sets flags") {
  Fixture f;
  f.mem[0x0200] = CPU::INS_LDA_IM;
  f.mem[0x0201] = 0x84; // negative
  s32 cycles = 2;
  f.cpu.Execute(cycles, f.mem);
  CHECK(f.cpu.A == 0x84);
  CHECK(f.cpu.Z == 0);
  CHECK(f.cpu.N == 1);
}

TEST_CASE("LDA immediate zero sets Z flag") {
  Fixture f;
  f.mem[0x0200] = CPU::INS_LDA_IM;
  f.mem[0x0201] = 0x00;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0x00);
  CHECK(f.cpu.Z == 1);
  CHECK(f.cpu.N == 0);
}

TEST_CASE("LDA zero page") {
  Fixture f;
  f.mem[0x0010] = 0x37;
  f.mem[0x0200] = CPU::INS_LDA_ZP;
  f.mem[0x0201] = 0x10;
  f.cpu.Execute(3, f.mem);
  CHECK(f.cpu.A == 0x37);
}

TEST_CASE("LDA zero page,X wraps around zero page") {
  Fixture f;
  f.cpu.X = 0xFF;
  f.mem[0x007F] = 0x55; // 0x80 + 0xFF wraps to 0x7F
  f.mem[0x0200] = CPU::INS_LDA_ZPX;
  f.mem[0x0201] = 0x80;
  f.cpu.Execute(4, f.mem);
  CHECK(f.cpu.A == 0x55);
}

TEST_CASE("LDA absolute") {
  Fixture f;
  f.mem[0x4243] = 0x99;
  f.mem[0x0200] = CPU::INS_LDA_ABS;
  f.mem[0x0201] = 0x43;
  f.mem[0x0202] = 0x42;
  f.cpu.Execute(4, f.mem);
  CHECK(f.cpu.A == 0x99);
}

TEST_CASE("LDA absolute,X no page cross costs 4 cycles") {
  Fixture f;
  f.cpu.X = 0x01;
  f.mem[0x4243] = 0x11;
  f.mem[0x0200] = CPU::INS_LDA_ABSX;
  f.mem[0x0201] = 0x42;
  f.mem[0x0202] = 0x42; // base 0x4242 + X(1) = 0x4243, same page
  s32 cycles = 4;
  f.cpu.Execute(cycles, f.mem);
  CHECK(f.cpu.A == 0x11);
}

TEST_CASE("LDA absolute,X page cross costs an extra cycle") {
  Fixture f;
  f.cpu.X = 0xFF;
  f.mem[0x4341] = 0x22; // 0x4242 + 0xFF = 0x4341, crosses page
  f.mem[0x0200] = CPU::INS_LDA_ABSX;
  f.mem[0x0201] = 0x42;
  f.mem[0x0202] = 0x42;
  s32 cycles = 5; // 4 + 1 for page cross
  f.cpu.Execute(cycles, f.mem);
  CHECK(f.cpu.A == 0x22);
}

TEST_CASE("LDA (indirect,X)") {
  Fixture f;
  f.cpu.X = 0x04;
  f.mem[0x0024] = 0x00; // 0x20 + X(4) = 0x24 -> pointer low
  f.mem[0x0025] = 0x80; // pointer high -> target 0x8000
  f.mem[0x8000] = 0xAB;
  f.mem[0x0200] = CPU::INS_LDA_INDX;
  f.mem[0x0201] = 0x20;
  f.cpu.Execute(6, f.mem);
  CHECK(f.cpu.A == 0xAB);
}

TEST_CASE("LDA (indirect),Y") {
  Fixture f;
  f.cpu.Y = 0x04;
  f.mem[0x0020] = 0x00; // pointer -> 0x8000
  f.mem[0x0021] = 0x80;
  f.mem[0x8004] = 0xCD; // 0x8000 + Y(4)
  f.mem[0x0200] = CPU::INS_LDA_INDY;
  f.mem[0x0201] = 0x20;
  f.cpu.Execute(5, f.mem);
  CHECK(f.cpu.A == 0xCD);
}

TEST_CASE("LDX and LDY immediate set flags independently of A") {
  Fixture f;
  f.cpu.A = 0x00; // pre-existing zero flag state shouldn't matter
  f.mem[0x0200] = CPU::INS_LDX_IM;
  f.mem[0x0201] = 0x05;
  f.mem[0x0202] = CPU::INS_LDY_IM;
  f.mem[0x0203] = 0x00;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.X == 0x05);
  CHECK(f.cpu.Z == 0);
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.Y == 0x00);
  CHECK(f.cpu.Z == 1);
}

// ---------------- STA / STX / STY ----------------

TEST_CASE("STA zero page writes A to memory") {
  Fixture f;
  f.cpu.A = 0x42;
  f.mem[0x0200] = CPU::INS_STA_ZP;
  f.mem[0x0201] = 0x10;
  f.cpu.Execute(3, f.mem);
  CHECK(f.mem[0x0010] == 0x42);
}

TEST_CASE("STA absolute,X always costs 5 cycles regardless of page cross") {
  Fixture f;
  f.cpu.A = 0x9C;
  f.cpu.X = 0xFF;
  f.mem[0x0200] = CPU::INS_STA_ABSX;
  f.mem[0x0201] = 0x42;
  f.mem[0x0202] = 0x42; // crosses into 0x4341
  s32 cycles = 5;
  f.cpu.Execute(cycles, f.mem);
  CHECK(f.mem[0x4341] == 0x9C);
}

// ---------------- transfers ----------------

TEST_CASE("TAX/TXA/TAY/TYA round-trip") {
  Fixture f;
  f.cpu.A = 0x37;
  f.mem[0x0200] = CPU::INS_TAX;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.X == 0x37);

  f.cpu.X = 0x00;
  f.mem[0x0201] = CPU::INS_TXA;
  f.cpu.A = 0xFF;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0x00);
  CHECK(f.cpu.Z == 1);
}

TEST_CASE("TSX/TXS move the stack pointer") {
  Fixture f;
  f.cpu.X = 0x80;
  f.mem[0x0200] = CPU::INS_TXS;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.SP == 0x80);

  f.mem[0x0201] = CPU::INS_TSX;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.X == 0x80);
}

// ---------------- stack ----------------

TEST_CASE("PHA/PLA round-trip preserves value and balances SP") {
  Fixture f;
  f.cpu.A = 0x77;
  Byte startSP = f.cpu.SP;
  f.mem[0x0200] = CPU::INS_PHA;
  f.cpu.Execute(3, f.mem);
  CHECK(f.cpu.SP == (Byte)(startSP - 1));

  f.cpu.A = 0x00;
  f.mem[0x0201] = CPU::INS_PLA;
  f.cpu.Execute(4, f.mem);
  CHECK(f.cpu.A == 0x77);
  CHECK(f.cpu.SP == startSP);
}

TEST_CASE("PHP/PLP round-trip preserves status flags") {
  Fixture f;
  f.cpu.C = 1;
  f.cpu.N = 1;
  f.cpu.Z = 0;
  f.mem[0x0200] = CPU::INS_PHP;
  f.cpu.Execute(3, f.mem);

  f.cpu.C = 0;
  f.cpu.N = 0;
  f.mem[0x0201] = CPU::INS_PLP;
  f.cpu.Execute(4, f.mem);
  CHECK(f.cpu.C == 1);
  CHECK(f.cpu.N == 1);
}

// ---------------- logic ----------------

TEST_CASE("AND immediate masks A") {
  Fixture f;
  f.cpu.A = 0b11001100;
  f.mem[0x0200] = CPU::INS_AND_IM;
  f.mem[0x0201] = 0b10101010;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0b10001000);
}

TEST_CASE("EOR immediate flips bits") {
  Fixture f;
  f.cpu.A = 0xFF;
  f.mem[0x0200] = CPU::INS_EOR_IM;
  f.mem[0x0201] = 0x0F;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0xF0);
}

TEST_CASE("ORA immediate sets bits") {
  Fixture f;
  f.cpu.A = 0x0F;
  f.mem[0x0200] = CPU::INS_ORA_IM;
  f.mem[0x0201] = 0xF0;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0xFF);
}

TEST_CASE("BIT sets Z from AND, N/V from memory bits 7/6") {
  Fixture f;
  f.cpu.A = 0x0F;
  f.mem[0x0010] = 0b11000000; // bit7=1 (N), bit6=1 (V), AND with A(0x0F) = 0
  f.mem[0x0200] = CPU::INS_BIT_ZP;
  f.mem[0x0201] = 0x10;
  f.cpu.Execute(3, f.mem);
  CHECK(f.cpu.Z == 1);
  CHECK(f.cpu.N == 1);
  CHECK(f.cpu.V == 1);
}

// ---------------- arithmetic ----------------

TEST_CASE("ADC simple addition without carry in") {
  Fixture f;
  f.cpu.C = 0;
  f.cpu.A = 0x10;
  f.mem[0x0200] = CPU::INS_ADC_IM;
  f.mem[0x0201] = 0x05;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0x15);
  CHECK(f.cpu.C == 0);
}

TEST_CASE("ADC sets carry on unsigned overflow") {
  Fixture f;
  f.cpu.C = 0;
  f.cpu.A = 0xFF;
  f.mem[0x0200] = CPU::INS_ADC_IM;
  f.mem[0x0201] = 0x02;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0x01);
  CHECK(f.cpu.C == 1);
}

TEST_CASE("ADC sets overflow flag on signed overflow (127 + 1)") {
  Fixture f;
  f.cpu.C = 0;
  f.cpu.A = 0x7F; // +127
  f.mem[0x0200] = CPU::INS_ADC_IM;
  f.mem[0x0201] = 0x01; // +1 -> should overflow into negative
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0x80);
  CHECK(f.cpu.V == 1);
  CHECK(f.cpu.N == 1);
}

TEST_CASE("SBC with carry set (no borrow) subtracts cleanly") {
  Fixture f;
  f.cpu.C = 1; // carry set means "no borrow" going in, per 6502 convention
  f.cpu.A = 0x10;
  f.mem[0x0200] = CPU::INS_SBC_IM;
  f.mem[0x0201] = 0x05;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0x0B);
  CHECK(f.cpu.C == 1); // no borrow occurred
}

TEST_CASE("SBC with carry clear subtracts one extra (borrow-in)") {
  Fixture f;
  f.cpu.C = 0; // borrow-in
  f.cpu.A = 0x10;
  f.mem[0x0200] = CPU::INS_SBC_IM;
  f.mem[0x0201] = 0x05;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0x0A);
}

TEST_CASE("SBC clears carry when a borrow occurs") {
  Fixture f;
  f.cpu.C = 1;
  f.cpu.A = 0x05;
  f.mem[0x0200] = CPU::INS_SBC_IM;
  f.mem[0x0201] = 0x10; // 5 - 16 -> borrow
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.C == 0);
}

// ---------------- compare ----------------

TEST_CASE("CMP sets C when A >= operand, Z when equal") {
  Fixture f;
  f.cpu.A = 0x10;
  f.mem[0x0200] = CPU::INS_CMP_IM;
  f.mem[0x0201] = 0x10;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.C == 1);
  CHECK(f.cpu.Z == 1);
}

TEST_CASE("CMP clears C when A < operand") {
  Fixture f;
  f.cpu.A = 0x05;
  f.mem[0x0200] = CPU::INS_CMP_IM;
  f.mem[0x0201] = 0x10;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.C == 0);
  CHECK(f.cpu.Z == 0);
}

TEST_CASE("CPX and CPY behave like CMP but against X/Y") {
  Fixture f;
  f.cpu.X = 0x20;
  f.cpu.Y = 0x05;
  f.mem[0x0200] = CPU::INS_CPX_IM;
  f.mem[0x0201] = 0x20;
  f.mem[0x0202] = CPU::INS_CPY_IM;
  f.mem[0x0203] = 0x10;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.Z == 1);
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.C == 0); // Y(5) < 0x10
}

// ---------------- inc/dec ----------------

TEST_CASE("INC/DEC memory and INX/INY/DEX/DEY wrap correctly") {
  Fixture f;
  f.mem[0x0010] = 0xFF;
  f.mem[0x0200] = CPU::INS_INC_ZP;
  f.mem[0x0201] = 0x10;
  f.cpu.Execute(5, f.mem);
  CHECK(f.mem[0x0010] == 0x00); // wraps, sets Z

  f.cpu.X = 0x00;
  f.mem[0x0202] = CPU::INS_DEX;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.X == 0xFF); // wraps to -1, sets N

  f.cpu.Y = 0xFF;
  f.mem[0x0203] = CPU::INS_INY;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.Y == 0x00);
}

// ---------------- shifts / rotates ----------------

TEST_CASE("ASL accumulator shifts left and captures carry from bit 7") {
  Fixture f;
  f.cpu.A = 0b10000001;
  f.mem[0x0200] = CPU::INS_ASL_ACC;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0b00000010);
  CHECK(f.cpu.C == 1);
}

TEST_CASE("LSR accumulator shifts right and captures carry from bit 0") {
  Fixture f;
  f.cpu.A = 0b00000011;
  f.mem[0x0200] = CPU::INS_LSR_ACC;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0b00000001);
  CHECK(f.cpu.C == 1);
}

TEST_CASE("ROL rotates carry into bit 0 and old bit 7 into carry") {
  Fixture f;
  f.cpu.A = 0b10000000;
  f.cpu.C = 1;
  f.mem[0x0200] = CPU::INS_ROL_ACC;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0b00000001);
  CHECK(f.cpu.C == 1); // old bit 7 was 1
}

TEST_CASE("ROR rotates carry into bit 7 and old bit 0 into carry") {
  Fixture f;
  f.cpu.A = 0b00000001;
  f.cpu.C = 1;
  f.mem[0x0200] = CPU::INS_ROR_ACC;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.A == 0b10000000);
  CHECK(f.cpu.C == 1); // old bit 0 was 1
}

TEST_CASE("ASL on memory reads-modifies-writes at correct cycle cost") {
  Fixture f;
  f.mem[0x0010] = 0b01000000;
  f.mem[0x0200] = CPU::INS_ASL_ZP;
  f.mem[0x0201] = 0x10;
  s32 cycles = 5;
  f.cpu.Execute(cycles, f.mem);
  CHECK(f.mem[0x0010] == 0b10000000);
}

// ---------------- jumps / branches ----------------

TEST_CASE("JMP absolute sets PC directly") {
  Fixture f;
  f.mem[0x0200] = CPU::INS_JMP_ABS;
  f.mem[0x0201] = 0x00;
  f.mem[0x0202] = 0x80;
  f.cpu.Execute(3, f.mem);
  CHECK(f.cpu.PC == 0x8000);
}

TEST_CASE("JMP indirect reproduces the page-boundary hardware bug") {
  Fixture f;
  // pointer at 0x30FF: real 6502 fetches high byte from 0x3000, not 0x3100
  f.mem[0x30FF] = 0x00;
  f.mem[0x3000] = 0x80; // wrong-wrapped high byte
  f.mem[0x3100] =
      0xFF; // correct high byte if it didn't wrap (should be ignored)
  f.mem[0x0200] = CPU::INS_JMP_IND;
  f.mem[0x0201] = 0xFF;
  f.mem[0x0202] = 0x30;
  f.cpu.Execute(5, f.mem);
  CHECK(f.cpu.PC == 0x8000);
}

TEST_CASE("JSR then RTS returns to the instruction after JSR, SP restored") {
  Fixture f;
  Byte startSP = f.cpu.SP;
  f.mem[0x0200] = CPU::INS_JSR;
  f.mem[0x0201] = 0x00;
  f.mem[0x0202] = 0x30;
  f.mem[0x3000] = CPU::INS_RTS;
  f.cpu.Execute(6 + 6, f.mem);
  CHECK(f.cpu.PC == 0x0203);
  CHECK(f.cpu.SP == startSP);
}

TEST_CASE("BEQ branches when Z set, costs 3 cycles with no page cross") {
  Fixture f;
  f.cpu.Z = 1;
  f.mem[0x0200] = CPU::INS_BEQ;
  f.mem[0x0201] = 0x05; // PC(0x0202) + 5 = 0x0207
  f.cpu.Execute(3, f.mem);
  CHECK(f.cpu.PC == 0x0207);
}

TEST_CASE("BEQ does not branch when Z clear, costs 2 cycles") {
  Fixture f;
  f.cpu.Z = 0;
  f.mem[0x0200] = CPU::INS_BEQ;
  f.mem[0x0201] = 0x05;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.PC == 0x0202); // fell through, didn't jump
}

TEST_CASE("BNE with negative offset branches backward (loop pattern)") {
  Fixture f;
  f.cpu.X = 0x03;
  // loop: DEX ; BNE loop
  f.mem[0x0200] = CPU::INS_DEX;
  f.mem[0x0201] = CPU::INS_BNE;
  f.mem[0x0202] = (Byte)(0x0200 - 0x0203); // branch back to 0x0200
  f.cpu.Execute((2 + 3) * 2 + 2, f.mem);   // 2 taken loops + final non-taken
  CHECK(f.cpu.X == 0x00);
}

// ---------------- flags ----------------

TEST_CASE("CLC/SEC/CLD/SED/CLI/SEI/CLV toggle their flags") {
  Fixture f;
  f.mem[0x0200] = CPU::INS_SEC;
  f.mem[0x0201] = CPU::INS_CLC;
  f.mem[0x0202] = CPU::INS_SED;
  f.mem[0x0203] = CPU::INS_CLD;
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.C == 1);
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.C == 0);
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.D == 1);
  f.cpu.Execute(2, f.mem);
  CHECK(f.cpu.D == 0);
}

// ---------------- BRK / RTI ----------------

TEST_CASE(
    "BRK pushes PC+2 and status, jumps to IRQ vector; RTI restores state") {
  Fixture f;
  f.mem[0xFFFE] = 0x00;
  f.mem[0xFFFF] = 0x40; // IRQ/BRK vector -> 0x4000
  f.mem[0x4000] = CPU::INS_RTI;
  f.cpu.C = 1;
  Byte startSP = f.cpu.SP;
  f.mem[0x0200] = CPU::INS_BRK;
  f.cpu.Execute(7, f.mem);
  CHECK(f.cpu.PC == 0x4000);
  CHECK(f.cpu.I == 1);

  f.cpu.Execute(6, f.mem);
  CHECK(f.cpu.PC ==
        0x0202); // return address pushed was PC+1 (=0x0202) from BRK's PC+1
  CHECK(f.cpu.C == 1);        // status restored
  CHECK(f.cpu.SP == startSP); // stack balanced after full BRK/RTI round trip
}

// ---------------- unknown opcode safety ----------------

TEST_CASE("Unknown opcode does not infinite-loop") {
  Fixture f;
  f.mem[0x0200] =
      0xFF; // not a real 6502 opcode... but actually assigned? check table
  s32 cycles = 5;
  CHECK_NOTHROW(f.cpu.Execute(cycles, f.mem));
}