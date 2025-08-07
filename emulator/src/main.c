#include <stdio.h>
#include "../include/cpu.h"
#include "../include/memory.h"

int main() {
    CPU8086 cpu;
    Memory8086 mem;

    cpu_init(&cpu);

    printf("8086 Emulator Started\n");
    printf("CS:IP = %04X:%04X\n",cpu.cs, cpu.ip);

    //physical address reset vector calculate cheyan
    uint32_t reset_addr = (cpu.cs << 4) + cpu.ip;

    //MOV AX, 0x1234 (MOV AX, imm16 form)
    mem_write8(&mem, reset_addr + 0, 0xB8);
    mem_write16(&mem, reset_addr + 1, 0x1234);
    
    //oru NOP instruction
    mem_write8(&mem, reset_addr + 3, 0x90);

    //oru HLT instruction 
    mem_write8(&mem, reset_addr + 4, 0xF4);

    //HLT allel unknown opcode varunna vare work cheyunna fetch-execute loop
    while(cpu_step(&cpu, &mem)){
        printf("AX=%04X BX=%04X CX=%04X DX=%04X IP=%04X\n", cpu.ax, cpu.bx, cpu.cx, cpu.dx, cpu.ip); //cpu state print cheyan
    }
    return 0;
}
