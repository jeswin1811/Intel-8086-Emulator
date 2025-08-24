#include "../include/memory.h"
#include <stdio.h>

uint8_t mem_read8(Memory8086 *mem, uint32_t addr){
    if(addr < MEMORY_SIZE)
        return mem->data[addr];
    return 0xFF; //ith out of bounds inu vendi
}

void mem_write8(Memory8086 *mem, uint32_t addr, uint8_t value){
    if(addr < MEMORY_SIZE)
        mem->data[addr] = value;
}

uint16_t mem_read16(Memory8086 *mem, uint32_t addr){
    uint8_t low = mem_read8(mem, addr);
    uint8_t high = mem_read8(mem, addr + 1);
    return (high << 8) | low;
}

void mem_write16(Memory8086 *mem, uint32_t addr, uint16_t value){
    mem_write8(mem, addr, value & 0xFF);
    mem_write8(mem, addr + 1, (value >> 8) & 0xFF);
}