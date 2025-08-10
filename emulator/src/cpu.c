#include "../include/cpu.h"
#include "../include/memory.h"
#include<stdio.h>

void cpu_init(CPU8086 *cpu){
    cpu->ax = cpu->bx = cpu->cx = cpu->dx = 0;
    cpu->si = cpu->di = cpu->bp = cpu->sp = 0;
    cpu->ip = 0x0000; //satharana gathiyil 0x0000 il ninnum start cheyunne
    cpu->flags = 0x0000; //thodangumbo ella flag um clear cheyan
    cpu->cs = 0x0000; //CS:IP -> FFFF:0000 (just for testing i put 0000)
    cpu->ds = cpu->es = cpu->ss = 0;
}

//Decode ModR/M
static void decode_modrm(uint8_t modrm, uint8_t *mod, uint8_t *reg, uint8_t *rm){
    *mod = (modrm >> 6) & 0x3;
    *reg = (modrm >> 3) & 0x7;
    *rm = modrm & 0x7;
}

//effective address calculate cheyan 
static uint32_t calc_ea(CPU8086 *cpu, uint8_t rm, int16_t disp) {
    switch (rm) {
        case 0: return cpu->bx + cpu->si + disp;
        case 1: return cpu->bx + cpu->di + disp;
        case 2: return cpu->bp + cpu->si + disp;
        case 3: return cpu->bp + cpu->di + disp;
        case 4: return cpu->si + disp;
        case 5: return cpu->di + disp;
        case 6: return cpu->bp + disp;
        case 7: return cpu->bx + disp;
        default: return 0;
    }
}

//setting the zero flag
static void set_zf(CPU8086 *cpu, uint16_t result){
    if(result == 0){
        cpu->flags |= FLAG_ZF;
    }
    else
    cpu->flags &= ~FLAG_ZF;
}

//setting the sign flag
static void set_sf(CPU8086 *cpu, uint16_t result){
    if(result & 0x8000){
        cpu->flags |= FLAG_SF;
    }
    else
    cpu->flags &= ~FLAG_SF;
}

//setting the carry flag for ADD
static void set_cf_add(CPU8086 *cpu, uint16_t a, uint16_t b){
    uint32_t result = (uint32_t)a + (uint32_t)b;
    if(result > 0xFFFF){
        cpu->flags |= FLAG_CF;
    }
    else
        cpu->flags &= ~FLAG_CF;
}

//setting the overflow flag for ADD
static void set_of_add(CPU8086 *cpu, uint16_t a, uint16_t b, uint16_t result){
    if(((a^result) & (b^result) & 0x8000) != 0){
        cpu->flags |= FLAG_OF;
    }
    else
        cpu->flags &= ~FLAG_OF;
}

//setting the carry flag for SUB
static void set_cf_sub(CPU8086 *cpu, uint16_t a, uint16_t b){
    if(a < b){
        cpu->flags |= FLAG_CF;
    }
    else
        cpu->flags &= ~FLAG_CF;
}

//setting the overflow flag for SUB
static void set_of_sub(CPU8086 *cpu, uint16_t a, uint16_t b, uint16_t result){
    if(((a^b) & (a^result) & 0x8000) != 0){
        cpu->flags |= FLAG_OF;
    }
    else
        cpu->flags &= ~FLAG_OF;
}

int cpu_step(CPU8086 *cpu, Memory8086 *mem){
    uint32_t addr = (cpu->cs << 4) + cpu->ip;
    uint8_t opcode = mem_read8(mem, addr);

    // eluppam registers access cheyan oru array of pointers
    uint16_t* reg_table[8] = {
        &cpu->ax,
        &cpu->cx,
        &cpu->dx,
        &cpu->bx,
        &cpu->sp,
        &cpu->bp,
        &cpu->si,
        &cpu->di,
    };

    printf("At %04X:%04X Opcode: 0x%02X\n", cpu->cs, cpu->ip, opcode);

    // Handle 0x81: ADD/SUB r/m16, imm16 (ModR/M-based)
    if(opcode == 0x81) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        printf("ModR/M decode: mod=%d reg=%d rm=%d\n", mod, reg, rm);
        //currently supports only ADD(reg==0) and SUB(reg==5)
        if(reg == 0 || reg == 5){
            uint16_t imm;
            uint16_t old_val, result;
            uint32_t ea = 0;
            int instr_len = 2; //opcode+modrm

            if(mod == 3){//register-direct
                old_val = *reg_table[rm];
                imm = mem_read16(mem, addr + 2);
                instr_len += 2;
                if(reg == 0){ //ADD
                    *reg_table[rm] += imm;
                    set_zf(cpu, *reg_table[rm]);
                    set_sf(cpu, *reg_table[rm]);
                    set_cf_add(cpu, old_val, imm);
                    set_of_add(cpu, old_val, imm, *reg_table[rm]);
                }
                else{ //SUB
                    *reg_table[rm] -= imm;
                    set_zf(cpu, *reg_table[rm]);
                    set_sf(cpu, *reg_table[rm]);
                    set_cf_sub(cpu, old_val, imm);
                    set_of_sub(cpu, old_val, imm, *reg_table[rm]);
                }
            }
            else { //Memory operand
                int16_t disp = 0;
                if(mod == 0 && rm == 6){ //direct address
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
                }
                else if(mod == 1) { //8-bit displacement
                    disp = (int8_t)mem_read8(mem, addr + 2);
                    instr_len += 1;
                }
                else if(mod == 2){ //16-bit displacement
                    disp = mem_read16(mem, addr + 2);
                    instr_len += 2;
                }
                if(!(mod == 0 && rm == 6)){
                    ea = calc_ea(cpu, rm, disp);
                }
                imm = mem_read16(mem, addr + instr_len);
                instr_len += 2;
                old_val = mem_read16(mem, ea);
                if(reg == 0){ //ADD
                    result = old_val + imm;
                    mem_write16(mem, ea, result);
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    set_cf_add(cpu, old_val, imm);
                    set_of_add(cpu, old_val, imm, result);
                }
                else{ //SUB
                    result = old_val - imm;
                    mem_write16(mem, ea, result);
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    set_cf_sub(cpu, old_val, imm);
                    set_of_sub(cpu, old_val, imm, result);
                }
            }
            cpu->ip += instr_len;
            return 1;
        }
    }

    switch (opcode) {
        case 0x90: //NOP
            cpu->ip += 1;
            break;
        case 0xF4: //HLT
            printf("HLT encountered. Halting CPU.\n");
            return 0; //single halt 
        case 0x05: //ADD AX, imm16
            {
                uint16_t old_ax = cpu->ax;
                uint16_t imm = mem_read16(mem, addr + 1);
                cpu->ax += imm;
                set_zf(cpu, cpu->ax);
                set_sf(cpu, cpu->ax);
                set_cf_add(cpu, old_ax, imm);
                set_of_add(cpu, old_ax, imm, cpu->ax);
                cpu->ip += 3;
                break;
            }
        case 0x2D://SUB AX, imm16
            {
                uint16_t old_ax = cpu->ax;
                uint16_t imm = mem_read16(mem, addr + 1);
                cpu->ax -= imm;
                set_zf(cpu, cpu->ax);
                set_sf(cpu, cpu->ax);
                set_cf_sub(cpu, old_ax, imm);
                set_of_sub(cpu, old_ax, imm, cpu->ax);
                cpu->ip += 3;
                break;
            }        
        default:
            // ithanu big brain MOV operation logic.
            if(opcode >= 0xB8 && opcode <= 0xBF){
                int reg_index = opcode - 0xB8;
                *reg_table[reg_index] = mem_read16(mem, addr + 1);
                cpu->ip += 3;
                break;
            }
            //INC
            if(opcode >= 0x40 && opcode <=0x47){
                int reg_index = opcode - 0x40;
                (*reg_table[reg_index])++;
                set_zf(cpu, *reg_table[reg_index]);
                set_sf(cpu, *reg_table[reg_index]);
                cpu->ip += 1;
                break;
            }
            //DEC
            if(opcode >= 0x48 && opcode <= 0x4F){
                int reg_index = opcode - 0x48;
                (*reg_table[reg_index])--;
                set_zf(cpu, *reg_table[reg_index]);
                set_sf(cpu, *reg_table[reg_index]);
                cpu->ip += 1;
                break;
            }
            printf("Unknown opcode: 0x%02X\n", opcode);
            return 0; //opcode unknown ayond stop
    }
    return 1;
}