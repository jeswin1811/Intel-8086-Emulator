#include <stdio.h>
#include <string.h>
#include "../include/cpu.h"
#include "../include/memory.h"

int main() {
    CPU8086 cpu;
    Memory8086 mem;

    memset(&mem, 0, sizeof(mem));

    cpu_init(&cpu);

    printf("8086 Emulator Started\n");
    printf("CS:IP = %04X:%04X\n",cpu.cs, cpu.ip);

    //physical address reset vector calculate cheyan
    uint32_t reset_addr = (cpu.cs << 4) + cpu.ip;

    // MOV AX, 0x0001
    mem_write8(&mem, reset_addr + 0, 0xB8);
    mem_write16(&mem, reset_addr + 1, 0x0001);

    // ADD AX, 0xFFFF (should set carry and overflow)
    mem_write8(&mem, reset_addr + 3, 0x05);
    mem_write16(&mem, reset_addr + 4, 0xFFFF);

    // INC AX (should wrap to 0, set zero flag)
    mem_write8(&mem, reset_addr + 6, 0x40);

    // DEC AX (should go to 0xFFFF, set sign flag)
    mem_write8(&mem, reset_addr + 7, 0x48);

    // SUB AX, 0x0001 (should go to 0xFFFE)
    mem_write8(&mem, reset_addr + 8, 0x2D);
    mem_write16(&mem, reset_addr + 9, 0x0001);

    // ADD BX, 0x1234 (opcode 0x81, modrm 0xC3)
    mem_write8(&mem, reset_addr + 11, 0x81); // opcode
    mem_write8(&mem, reset_addr + 12, 0xC3); // modrm
    mem_write16(&mem, reset_addr + 13, 0x1234); // imm16

    // SUB BX, 0x0001 (opcode 0x81, modrm 0xEB)
    mem_write8(&mem, reset_addr + 15, 0x81); // opcode
    mem_write8(&mem, reset_addr + 16, 0xEB); // modrm
    mem_write16(&mem, reset_addr + 17, 0x0001); // imm16

    // HLT
    mem_write8(&mem, reset_addr + 19, 0xF4);

    for (int i = 0; i < 20; ++i) {
    printf("addr=%u val=%02X\n", reset_addr + i, mem_read8(&mem, reset_addr + i));
    }
    printf("\n");

    //HLT allel unknown opcode varunna vare work cheyunna fetch-execute loop
    while(cpu_step(&cpu, &mem)){
        printf("AX=%04X BX=%04X CX=%04X DX=%04X IP=%04X FLAGS=%04X\n", cpu.ax, cpu.bx, cpu.cx, cpu.dx, cpu.ip, cpu.flags); //cpu state print cheyan
    }
    return 0;
}