#include "../include/cpu.h"
#include "../include/memory.h"
#include<stdio.h>

void cpu_init(CPU8086 *cpu){
    cpu->ax = cpu->bx = cpu->cx = cpu->dx = 0;
    cpu->si = cpu->di = cpu->bp = cpu->sp = 0;
    cpu->ip = 0x0000; //satharana gathiyil 0x0000 il ninnum start cheyunne
    cpu->flags = 0x0000; //thodangumbo ella flag um clear cheyan
    cpu->cs = 0xFFFF; //CS:IP -> FFFF:0000
    cpu->ds = cpu->es = cpu->ss = 0;
}

int cpu_step(CPU8086 *cpu, Memory8086 *mem){
    uint32_t addr = (cpu->cs << 4) + cpu->ip;
    uint8_t opcode = mem_read8(mem, addr);

    printf("At %04X:%04X Opcode: 0x%02X\n", cpu->cs, cpu->ip, opcode);

    switch (opcode) {
        case 0x90: //NOP
            cpu->ip += 1;
            break;
        case 0xF4: //HLT
            printf("HLT encountered. Halting CPU.\n");
            return 0; //single halt
        case 0xB8: //MOV AX, imm16
        {
            cpu->ax = mem_read16(mem, addr + 1);
            cpu->ip += 3;
            break;
        }    
        default:
            printf("Unknown opcode: 0x%02X\n", opcode);
            return 0; //opcode unknown ayond stop
    }
    return 1;
}