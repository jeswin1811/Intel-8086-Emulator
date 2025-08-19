#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/cpu.h"
#include "../include/memory.h"

// Loader for .com/.bin files
int load_bin(Memory8086 *mem, const char *filename, uint16_t load_addr) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Could not open %s\n", filename);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(&mem->data[load_addr], 1, size, f);
    fclose(f);
    printf("Loaded %ld bytes to 0x%04X\n", size, load_addr);
    return 1;
}

int main(int argc, char **argv) {
    CPU8086 cpu;
    Memory8086 mem;
    memset(&mem, 0, sizeof(mem));
    cpu_init(&cpu);

    if (argc < 2) {
        printf("Usage: %s program.com\n", argv[0]);
        return 1;
    }

    // Load .com file at 0x100 (typical for DOS .com)
    if (!load_bin(&mem, argv[1], 0x100)) return 1;
    cpu.cs = 0x0000;
    cpu.ip = 0x0100;

    printf("8086 Emulator Started\n");
    printf("CS:IP = %04X:%04X\n",cpu.cs, cpu.ip);

    //HLT allel unknown opcode varunna vare work cheyunna fetch-execute loop
    while(cpu_step(&cpu, &mem)){
        // No per-instruction print; output will be from DOS int 21h, ah=2 only
    }
    return 0;
}