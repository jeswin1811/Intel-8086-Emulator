#ifndef CPU_H
#define CPU_H

#include<stdint.h>
#include "../include/cpu.h"
#include "../include/memory.h"

typedef struct {
    uint16_t ax, bx, cx, dx;
    uint16_t si, di, bp, sp;
    uint16_t ip;
    uint16_t flags;
    uint16_t cs, ds, es, ss;
} CPU8086;

void cpu_init(CPU8086 *cpu);

int cpu_step(CPU8086 *cpu, Memory8086 *mem);

#endif