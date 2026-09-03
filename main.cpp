#include "cpu6502.h"

int main() {
  Mem mem;
  CPU cpu;
  cpu.Reset(mem);

  // Small self-test program:
  //   JSR $4242
  //   $4242: LDA #$10
  //          CLC
  //          ADC #$05      ; A = 0x15
  //          STA $10       ; mem[0x0010] = 0x15
  //          LDX $10       ; X = 0x15
  //          RTS
  mem[0xFFFC] = CPU::INS_JSR;
  mem[0xFFFD] = 0x42;
  mem[0xFFFE] = 0x42;
  mem[0x4242] = CPU::INS_LDA_IM;
  mem[0x4243] = 0x10;
  mem[0x4244] = CPU::INS_CLC;
  mem[0x4245] = CPU::INS_ADC_IM;
  mem[0x4246] = 0x05;
  mem[0x4247] = CPU::INS_STA_ZP;
  mem[0x4248] = 0x10;
  mem[0x4249] = CPU::INS_LDX_ZP;
  mem[0x424A] = 0x10;
  mem[0x424B] = CPU::INS_RTS;

  cpu.Execute(6 + 2 + 2 + 2 + 3 + 3 + 6, mem);

  printf("A=0x%02X X=0x%02X mem[0x10]=0x%02X PC=0x%04X SP=0x%02X\n", cpu.A,
         cpu.X, mem[0x10], cpu.PC, cpu.SP);
  return 0;
}