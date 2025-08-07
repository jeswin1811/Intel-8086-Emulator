#ifndef MEMORY_H
#define MEMORY_H

#include<stdint.h>

#define MEMORY_SIZE 0x100000

typedef struct {
    uint8_t data[MEMORY_SIZE];
}Memory8086;

//byte read cheyan
uint8_t mem_read8(Memory8086 *mem, uint32_t addr);

//byte write cheyan
void mem_write8(Memory8086 *mem, uint32_t addr, uint8_t value);

//word read cheyan
uint16_t mem_read16(Memory8086 *mem, uint32_t addr);

//word write cheyan
void mem_write16(Memory8086 *mem, uint32_t addr, uint16_t value);

#endif