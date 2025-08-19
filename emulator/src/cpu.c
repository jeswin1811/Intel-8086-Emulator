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

//setting the parity flag
static void set_pf(CPU8086 *cpu, uint16_t result){
    uint8_t v = result & 0xFF;
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    if (!(v & 1)) cpu->flags |= FLAG_PF;
    else cpu->flags &= ~FLAG_PF;
}

//setting the auxiliary carry flag for ADD
static void set_af_add(CPU8086 *cpu, uint16_t a, uint16_t b){
    if (((a & 0xF) + (b & 0xF)) > 0xF) cpu->flags |= 0x10;
    else cpu->flags &= ~0x10;
}
//setting the auxiliary carry flag for SUB
static void set_af_sub(CPU8086 *cpu, uint16_t a, uint16_t b){
    if ((a & 0xF) < (b & 0xF)) cpu->flags |= 0x10;
    else cpu->flags &= ~0x10;
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

// 16-bit ADD with flag updates

static uint16_t add16(CPU8086 *cpu, uint16_t a, uint16_t b) {
    uint16_t result = a + b;
    set_zf(cpu, result);
    set_sf(cpu, result);
    set_pf(cpu, result);
    set_cf_add(cpu, a, b);
    set_of_add(cpu, a, b, result);
    set_af_add(cpu, a, b);
    return result;
}

// 16-bit SUB with flag updates

static uint16_t sub16(CPU8086 *cpu, uint16_t a, uint16_t b) {
    uint16_t result = a - b;
    set_zf(cpu, result);
    set_sf(cpu, result);
    set_pf(cpu, result);
    set_cf_sub(cpu, a, b);
    set_of_sub(cpu, a, b, result);
    set_af_sub(cpu, a, b);
    return result;
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

    // Handle 8-bit MOV immediate to register (0xB0–0xB7)
    if(opcode >= 0xB0 && opcode <= 0xB7){
        int reg_index = opcode - 0xB0;
        // 8-bit register mapping: AL, CL, DL, BL, AH, CH, DH, BH
        uint8_t imm8 = mem_read8(mem, addr + 1);
        if(reg_index < 4){
            // Low byte: AL, CL, DL, BL
            ((uint8_t*)reg_table[reg_index])[0] = imm8;
        } else {
            // High byte: AH, CH, DH, BH
            ((uint8_t*)reg_table[reg_index-4])[1] = imm8;
        }
        cpu->ip += 2;
        return 1;
    }



    // --- String instructions and REP prefix ---
    static int rep_prefix = 0;
    if (opcode == 0xF3) { // REP/REPZ
        rep_prefix = 1;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xF2) { // REPNZ/REPNE
        rep_prefix = 2;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xA5) { // MOVSW
        uint16_t val = mem_read16(mem, cpu->ds + cpu->si);
        mem_write16(mem, cpu->es + cpu->di, val);
        int inc = (cpu->flags & 0x400) ? -2 : 2; // DF
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            if (cpu->cx) cpu->ip -= 1; else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xA4) { // MOVSB
        uint8_t val = mem_read8(mem, cpu->ds + cpu->si);
        mem_write8(mem, cpu->es + cpu->di, val);
        int inc = (cpu->flags & 0x400) ? -1 : 1; // DF
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            if (cpu->cx) cpu->ip -= 1; else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xAD) { // LODSW
        cpu->ax = mem_read16(mem, cpu->ds + cpu->si);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->si += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            if (cpu->cx) cpu->ip -= 1; else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xAC) { // LODSB
    ((uint8_t*)&cpu->ax)[0] = mem_read8(mem, cpu->ds + cpu->si);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->si += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            if (cpu->cx) cpu->ip -= 1; else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xAB) { // STOSW
        mem_write16(mem, cpu->es + cpu->di, cpu->ax);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            if (cpu->cx) cpu->ip -= 1; else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xAA) { // STOSB
    mem_write8(mem, cpu->es + cpu->di, ((uint8_t*)&cpu->ax)[0]);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            if (cpu->cx) cpu->ip -= 1; else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xAF) { // SCASW
        uint16_t val = mem_read16(mem, cpu->es + cpu->di);
        uint16_t result = cpu->ax - val;
        set_zf(cpu, result); set_sf(cpu, result); set_pf(cpu, result); set_cf_sub(cpu, cpu->ax, val); set_of_sub(cpu, cpu->ax, val, result); set_af_sub(cpu, cpu->ax, val);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1) repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2) repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat) cpu->ip -= 1;
            else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xAE) { // SCASB
        uint8_t val = mem_read8(mem, cpu->es + cpu->di);
        uint8_t result = ((uint8_t*)&cpu->ax)[0] - val;
        set_zf(cpu, result); set_sf(cpu, result); set_pf(cpu, result); set_cf_sub(cpu, ((uint8_t*)&cpu->ax)[0], val); set_of_sub(cpu, ((uint8_t*)&cpu->ax)[0], val, result); set_af_sub(cpu, ((uint8_t*)&cpu->ax)[0], val);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1) repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2) repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat) cpu->ip -= 1;
            else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xA7) { // CMPSW
        uint16_t src = mem_read16(mem, cpu->ds + cpu->si);
        uint16_t dst = mem_read16(mem, cpu->es + cpu->di);
        uint16_t result = src - dst;
        set_zf(cpu, result); set_sf(cpu, result); set_pf(cpu, result); set_cf_sub(cpu, src, dst); set_of_sub(cpu, src, dst, result); set_af_sub(cpu, src, dst);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1) repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2) repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat) cpu->ip -= 1;
            else rep_prefix = 0;
        }
        return 1;
    }
    if (opcode == 0xA6) { // CMPSB
        uint8_t src = mem_read8(mem, cpu->ds + cpu->si);
        uint8_t dst = mem_read8(mem, cpu->es + cpu->di);
        uint8_t result = src - dst;
        set_zf(cpu, result); set_sf(cpu, result); set_pf(cpu, result); set_cf_sub(cpu, src, dst); set_of_sub(cpu, src, dst, result); set_af_sub(cpu, src, dst);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx) {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1) repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2) repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat) cpu->ip -= 1;
            else rep_prefix = 0;
        }
        return 1;
    }
    // --- Segment override prefix (stub) ---
    // (0x26: ES, 0x2E: CS, 0x36: SS, 0x3E: DS)
    // You can implement this by setting a variable to override segment for next memory access
    if (opcode == 0x26 || opcode == 0x2E || opcode == 0x36 || opcode == 0x3E) {

        cpu->ip += 1;
        return 1;
    }

    // --- Far CALL/JMP/RET (stub) ---
    if (opcode == 0x9A) { // CALL far ptr
        uint16_t ip_new = mem_read16(mem, addr + 1);
        uint16_t cs_new = mem_read16(mem, addr + 3);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->cs);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->ip + 5);
        cpu->cs = cs_new;
        cpu->ip = ip_new;

        return 1;
    }
    if (opcode == 0xEA) { // JMP far ptr
        uint16_t ip_new = mem_read16(mem, addr + 1);
        uint16_t cs_new = mem_read16(mem, addr + 3);
        cpu->cs = cs_new;
        cpu->ip = ip_new;

        return 1;
    }
    if (opcode == 0xCB) { // RETF
        cpu->ip = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->cs = mem_read16(mem, cpu->sp); cpu->sp += 2;

        return 1;
    }

    // --- CLI/STI (Interrupt Flag) ---
    if (opcode == 0xFA) { cpu->flags &= ~0x0200; cpu->ip += 1; return 1; } // CLI
    if (opcode == 0xFB) { cpu->flags |= 0x0200; cpu->ip += 1; return 1; }  // STI

    // --- Real INT/IRET handling ---
    if (opcode == 0xCD) { // INT imm8
        uint8_t int_num = mem_read8(mem, addr + 1);
        // DOS output: int 21h
        if (int_num == 0x21) {
            uint8_t ah = (cpu->ax >> 8) & 0xFF;
            if (ah == 2) {
                // Print character in DL
                putchar(((uint8_t*)&cpu->dx)[0]);
                fflush(stdout);
                cpu->ip += 2;
                return 1;
            } else if (ah == 0x4C) {
                // Program terminate (int 21h, ah=4Ch)
                return 0;
            }
        }
        // Default INT handler (push flags/cs/ip, jump to IVT)
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->flags);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->cs);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->ip + 2);
        cpu->ip = mem_read16(mem, int_num * 4);
        cpu->cs = mem_read16(mem, int_num * 4 + 2);
        return 1;
    }
    if (opcode == 0xCF) { // IRET
        cpu->ip = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->cs = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->flags = mem_read16(mem, cpu->sp); cpu->sp += 2;

        return 1;
    }

    //MOV r/m16, r16 (0x89) and MOV r16, r/m16 (0x8b)
    if(opcode == 0x89 || opcode == 0x8B) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2; //opcode + modrm
        if(mod == 3){
            //register to register
            if (opcode == 0x89) {
                *reg_table[rm] = *reg_table[reg];
            }
            else{
                *reg_table[reg] = *reg_table[rm];
            }
        }
        else {
            //memory operand
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6){
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;   
            }
            else if (mod == 1){
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if(mod == 2) {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6)) {
                ea = calc_ea(cpu, rm, disp);
            }
            if(opcode == 0x89){
                mem_write16(mem, ea, *reg_table[reg]);
            }
            else {
                *reg_table[reg] = mem_read16(mem, ea);
            }
        }
        cpu->ip += instr_len;
        return 1;
    }

    // ADD/SUB r/m16, r16 and r16, r/m16
    if (opcode == 0x01 || opcode == 0x03 || opcode == 0x29 || opcode == 0x2B) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2; // opcode + modrm

        if (mod == 3) {
            // Register to register
            if (opcode == 0x01) { // ADD r/m16, r16
                *reg_table[rm] = add16(cpu, *reg_table[rm], *reg_table[reg]);
            } else if (opcode == 0x03) { // ADD r16, r/m16
                *reg_table[reg] = add16(cpu, *reg_table[reg], *reg_table[rm]);
            } else if (opcode == 0x29) { // SUB r/m16, r16
                *reg_table[rm] = sub16(cpu, *reg_table[rm], *reg_table[reg]);
            } else if (opcode == 0x2B) { // SUB r16, r/m16
                *reg_table[reg] = sub16(cpu, *reg_table[reg], *reg_table[rm]);
            }
        } else {
            // Memory operand
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6) {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            } else if (mod == 1) {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            } else if (mod == 2) {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6)) {
                ea = calc_ea(cpu, rm, disp);
            }
            if (opcode == 0x01) { // ADD [mem], r16
                uint16_t val = mem_read16(mem, ea);
                val = add16(cpu, val, *reg_table[reg]);
                mem_write16(mem, ea, val);
            } else if (opcode == 0x03) { // ADD r16, [mem]
                uint16_t val = mem_read16(mem, ea);
                *reg_table[reg] = add16(cpu, *reg_table[reg], val);
            } else if (opcode == 0x29) { // SUB [mem], r16
                uint16_t val = mem_read16(mem, ea);
                val = sub16(cpu, val, *reg_table[reg]);
                mem_write16(mem, ea, val);
            } else if (opcode == 0x2B) { // SUB r16, [mem]
                uint16_t val = mem_read16(mem, ea);
                *reg_table[reg] = sub16(cpu, *reg_table[reg], val);
            }
        }
        cpu->ip = addr + instr_len - (cpu->cs << 4); // Ensure IP is incremented by instr_len from current addr
        return 1;
    }

    // Handle 0x81: ADD/SUB r/m16, imm16 (ModR/M-based)
    if(opcode == 0x81) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);


        // Support ADD(0), OR(1), AND(4), SUB(5), XOR(6), CMP(7)
        if(reg == 0 || reg == 1 || reg == 4 || reg == 5 || reg == 6 || reg == 7){
            uint16_t imm;
            uint16_t old_val, result;
            uint32_t ea = 0;
            int instr_len = 2; //opcode+modrm

            if(mod == 3){//register-direct
                old_val = *reg_table[rm];
                imm = mem_read16(mem, addr + 2);
                instr_len += 2;
                switch(reg) {
                    case 0: // ADD
                        *reg_table[rm] = add16(cpu, old_val, imm);
                        break;
                    case 1: // OR
                        result = old_val | imm;
                        *reg_table[rm] = result;
                        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
                        break;
                    case 4: // AND
                        result = old_val & imm;
                        *reg_table[rm] = result;
                        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
                        break;
                    case 5: // SUB
                        *reg_table[rm] = sub16(cpu, old_val, imm);
                        break;
                    case 6: // XOR
                        result = old_val ^ imm;
                        *reg_table[rm] = result;
                        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
                        break;
                    case 7: // CMP
                        result = old_val - imm;
                        set_zf(cpu, result); set_sf(cpu, result);
                        set_cf_sub(cpu, old_val, imm); set_of_sub(cpu, old_val, imm, result);
                        break;
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
                switch(reg) {
                    case 0: // ADD
                        result = old_val + imm;
                        mem_write16(mem, ea, result);
                        set_zf(cpu, result); set_sf(cpu, result);
                        set_cf_add(cpu, old_val, imm); set_of_add(cpu, old_val, imm, result);
                        break;
                    case 1: // OR
                        result = old_val | imm;
                        mem_write16(mem, ea, result);
                        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
                        break;
                    case 4: // AND
                        result = old_val & imm;
                        mem_write16(mem, ea, result);
                        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
                        break;
                    case 5: // SUB
                        result = old_val - imm;
                        mem_write16(mem, ea, result);
                        set_zf(cpu, result); set_sf(cpu, result);
                        set_cf_sub(cpu, old_val, imm); set_of_sub(cpu, old_val, imm, result);
                        break;
                    case 6: // XOR
                        result = old_val ^ imm;
                        mem_write16(mem, ea, result);
                        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
                        break;
                    case 7: // CMP
                        result = old_val - imm;
                        set_zf(cpu, result); set_sf(cpu, result);
                        set_cf_sub(cpu, old_val, imm); set_of_sub(cpu, old_val, imm, result);
                        break;
                }
            }
            cpu->ip += instr_len;
            return 1;
        }
    }

    // AND/OR/XOR/CMP r/m16, r16 and r16, r/m16
    if (opcode == 0x21 || opcode == 0x23 || opcode == 0x09 || opcode == 0x0B ||
        opcode == 0x31 || opcode == 0x33 || opcode == 0x39 || opcode == 0x3B) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2; // opcode + modrm

        // Helper lambdas for logic ops
        #define LOGIC_OP(op, a, b) ((a) op (b))
        #define SET_LOGIC_FLAGS(cpu, result) \
            set_zf(cpu, result); \
            set_sf(cpu, result); \
            cpu->flags &= ~(FLAG_CF | FLAG_OF);

        if (mod == 3) {
            uint16_t src = *reg_table[reg];
            uint16_t dst = *reg_table[rm];
            uint16_t result = 0;
            if (opcode == 0x21) { // AND r/m16, r16
                result = dst & src;
                *reg_table[rm] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x23) { // AND r16, r/m16
                result = *reg_table[reg] & *reg_table[rm];
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x09) { // OR r/m16, r16
                result = dst | src;
                *reg_table[rm] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x0B) { // OR r16, r/m16
                result = *reg_table[reg] | *reg_table[rm];
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x31) { // XOR r/m16, r16
                result = dst ^ src;
                *reg_table[rm] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x33) { // XOR r16, r/m16
                result = *reg_table[reg] ^ *reg_table[rm];
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x39) { // CMP r/m16, r16
                result = dst - src;
                set_zf(cpu, result);
                set_sf(cpu, result);
                set_cf_sub(cpu, dst, src);
                set_of_sub(cpu, dst, src, result);
            } else if (opcode == 0x3B) { // CMP r16, r/m16
                result = *reg_table[reg] - *reg_table[rm];
                set_zf(cpu, result);
                set_sf(cpu, result);
                set_cf_sub(cpu, *reg_table[reg], *reg_table[rm]);
                set_of_sub(cpu, *reg_table[reg], *reg_table[rm], result);
            }
        } else {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6) {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            } else if (mod == 1) {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            } else if (mod == 2) {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6)) {
                ea = calc_ea(cpu, rm, disp);
            }
            uint16_t src = *reg_table[reg];
            uint16_t dst = mem_read16(mem, ea);
            uint16_t result = 0;
            if (opcode == 0x21) { // AND [mem], r16
                result = dst & src;
                mem_write16(mem, ea, result);
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x23) { // AND r16, [mem]
                result = *reg_table[reg] & dst;
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x09) { // OR [mem], r16
                result = dst | src;
                mem_write16(mem, ea, result);
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x0B) { // OR r16, [mem]
                result = *reg_table[reg] | dst;
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x31) { // XOR [mem], r16
                result = dst ^ src;
                mem_write16(mem, ea, result);
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x33) { // XOR r16, [mem]
                result = *reg_table[reg] ^ dst;
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            } else if (opcode == 0x39) { // CMP [mem], r16
                result = dst - src;
                set_zf(cpu, result);
                set_sf(cpu, result);
                set_cf_sub(cpu, dst, src);
                set_of_sub(cpu, dst, src, result);
            } else if (opcode == 0x3B) { // CMP r16, [mem]
                result = *reg_table[reg] - dst;
                set_zf(cpu, result);
                set_sf(cpu, result);
                set_cf_sub(cpu, *reg_table[reg], dst);
                set_of_sub(cpu, *reg_table[reg], dst, result);
            }
        }
        cpu->ip = addr + instr_len - (cpu->cs << 4);
        return 1;
    }

    // INC/DEC/NOT/NEG/TEST/MUL/IMUL/DIV/IDIV r/m16 (0xFF, 0xF7)
    if (opcode == 0xFF || opcode == 0xF7) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint32_t ea = 0;
        if (mod == 0 && rm == 6) {
            ea = mem_read16(mem, addr + 2);
            instr_len += 2;
        } else if (mod == 1) {
            ea = calc_ea(cpu, rm, (int8_t)mem_read8(mem, addr + 2));
            instr_len += 1;
        } else if (mod == 2) {
            ea = calc_ea(cpu, rm, mem_read16(mem, addr + 2));
            instr_len += 2;
        } else if (mod == 3) {
            ea = rm; // register index
        } else {
            ea = calc_ea(cpu, rm, 0);
        }
        uint16_t val = (mod == 3) ? *reg_table[ea] : mem_read16(mem, ea);
        uint16_t result = 0;
        switch (reg) {
            case 0: // INC
                result = val + 1;
                if (mod == 3) *reg_table[ea] = result; else mem_write16(mem, ea, result);
                set_zf(cpu, result); set_sf(cpu, result);
                break;
            case 1: // DEC
                result = val - 1;
                if (mod == 3) *reg_table[ea] = result; else mem_write16(mem, ea, result);
                set_zf(cpu, result); set_sf(cpu, result);
                break;
            case 2: // NOT (only 0xF7)
                if (opcode == 0xF7) {
                    result = ~val;
                    if (mod == 3) *reg_table[ea] = result; else mem_write16(mem, ea, result);
                }
                break;
            case 3: // NEG (only 0xF7)
                if (opcode == 0xF7) {
                    result = -val;
                    if (mod == 3) *reg_table[ea] = result; else mem_write16(mem, ea, result);
                    set_zf(cpu, result); set_sf(cpu, result);
                    cpu->flags |= (val != 0) ? FLAG_CF : 0;
                }
                break;
            case 4: // MUL (only 0xF7)
                // AX = AX * r/m16 (unsigned)
                if (opcode == 0xF7) {
                    uint32_t prod = cpu->ax * val;
                    cpu->dx = (prod >> 16) & 0xFFFF;
                    cpu->ax = prod & 0xFFFF;
                    // Set flags as per 8086 rules (CF/OF if upper word nonzero)
                    if (cpu->dx) cpu->flags |= FLAG_CF | FLAG_OF;
                    else cpu->flags &= ~(FLAG_CF | FLAG_OF);
                }
                break;
            case 5: // IMUL (only 0xF7)
                if (opcode == 0xF7) {
                    int32_t prod = (int16_t)cpu->ax * (int16_t)val;
                    cpu->dx = (prod >> 16) & 0xFFFF;
                    cpu->ax = prod & 0xFFFF;
                    if ((cpu->dx != 0) && (cpu->dx != -1)) cpu->flags |= FLAG_CF | FLAG_OF;
                    else cpu->flags &= ~(FLAG_CF | FLAG_OF);
                }
                break;
            case 6: // DIV (only 0xF7)
                if (opcode == 0xF7) {
                    uint32_t dividend = ((uint32_t)cpu->dx << 16) | cpu->ax;
                    if (val == 0) { return 0; }
                    cpu->ax = dividend / val;
                    cpu->dx = dividend % val;
                }
                break;
            case 7: // IDIV (only 0xF7)
                if (opcode == 0xF7) {
                    int32_t dividend = ((int32_t)cpu->dx << 16) | cpu->ax;
                    if ((int16_t)val == 0) { return 0; }
                    cpu->ax = dividend / (int16_t)val;
                    cpu->dx = dividend % (int16_t)val;
                }
                break;
        }
        cpu->ip += instr_len;
        return 1;
    }

    // ADC/SBB r/m16, r16 and r16, r/m16 (0x11, 0x13, 0x19, 0x1B)
    if (opcode == 0x11 || opcode == 0x13 || opcode == 0x19 || opcode == 0x1B) {
    uint8_t modrm = mem_read8(mem, addr + 1);
    uint8_t mod, reg, rm;
    decode_modrm(modrm, &mod, &reg, &rm);
    int instr_len = 2;
    uint16_t cf = (cpu->flags & FLAG_CF) ? 1 : 0;
    if (mod == 3) {
        if (opcode == 0x11) *reg_table[rm] = add16(cpu, *reg_table[rm], *reg_table[reg] + cf);
        else if (opcode == 0x13) *reg_table[reg] = add16(cpu, *reg_table[reg], *reg_table[rm] + cf);
        else if (opcode == 0x19) *reg_table[rm] = sub16(cpu, *reg_table[rm], *reg_table[reg] + cf);
        else if (opcode == 0x1B) *reg_table[reg] = sub16(cpu, *reg_table[reg], *reg_table[rm] + cf);
    } else {
        int16_t disp = 0;
        uint32_t ea = 0;
        if (mod == 0 && rm == 6) { ea = mem_read16(mem, addr + 2); instr_len += 2; }
        else if (mod == 1) { disp = (int8_t)mem_read8(mem, addr + 2); instr_len += 1; }
        else if (mod == 2) { disp = mem_read16(mem, addr + 2); instr_len += 2; }
        if (!(mod == 0 && rm == 6)) ea = calc_ea(cpu, rm, disp);
        uint16_t memval = mem_read16(mem, ea);
        if (opcode == 0x11) { // ADC [mem], reg
            uint16_t result = add16(cpu, memval, *reg_table[reg] + cf);
            mem_write16(mem, ea, result);
        } else if (opcode == 0x13) { // ADC reg, [mem]
            *reg_table[reg] = add16(cpu, *reg_table[reg], memval + cf);
        } else if (opcode == 0x19) { // SBB [mem], reg
            uint16_t result = sub16(cpu, memval, *reg_table[reg] + cf);
            mem_write16(mem, ea, result);
        } else if (opcode == 0x1B) { // SBB reg, [mem]
            *reg_table[reg] = sub16(cpu, *reg_table[reg], memval + cf);
        }
    }
    cpu->ip += instr_len;
    return 1;
    }

    // --- TEST r/m16, r16 (0x85) and TEST AX, imm16 (0xA9) ---
    if (opcode == 0x85) { // TEST r/m16, r16
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint16_t result = 0;
        if (mod == 3) {
            result = *reg_table[rm] & *reg_table[reg];
        } else {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6) { ea = mem_read16(mem, addr + 2); instr_len += 2; }
            else if (mod == 1) { disp = (int8_t)mem_read8(mem, addr + 2); instr_len += 1; }
            else if (mod == 2) { disp = mem_read16(mem, addr + 2); instr_len += 2; }
            if (!(mod == 0 && rm == 6)) ea = calc_ea(cpu, rm, disp);
            result = mem_read16(mem, ea) & *reg_table[reg];
        }
        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
        cpu->ip += instr_len;
        return 1;
    }
    if (opcode == 0xA9) { // TEST AX, imm16
        uint16_t imm = mem_read16(mem, addr + 1);
        uint16_t result = cpu->ax & imm;
        set_zf(cpu, result); set_sf(cpu, result); cpu->flags &= ~(FLAG_CF | FLAG_OF);
        cpu->ip += 3;
        return 1;
    }

    // --- PUSH/POP segment registers (0x06, 0x0E, 0x16, 0x1E, 0x07, 0x17, 0x1F) ---
    if (opcode == 0x06) { cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->es); cpu->ip += 1; return 1; }
    if (opcode == 0x0E) { cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->cs); cpu->ip += 1; return 1; }
    if (opcode == 0x16) { cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->ss); cpu->ip += 1; return 1; }
    if (opcode == 0x1E) { cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->ds); cpu->ip += 1; return 1; }
    if (opcode == 0x07) { cpu->es = mem_read16(mem, cpu->sp); cpu->sp += 2; cpu->ip += 1; return 1; }
    if (opcode == 0x17) { cpu->ss = mem_read16(mem, cpu->sp); cpu->sp += 2; cpu->ip += 1; return 1; }
    if (opcode == 0x1F) { cpu->ds = mem_read16(mem, cpu->sp); cpu->sp += 2; cpu->ip += 1; return 1; }

    // --- Conditional jumps (short, 8-bit displacement) ---
    if (opcode == 0x74) { // JE/JZ
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_ZF) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x75) { // JNE/JNZ
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_ZF)) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x72) { // JC
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_CF) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x73) { // JNC
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_CF)) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x78) { // JS
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_SF) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x79) { // JNS
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_SF)) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7A) { // JP/JPE
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_PF) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7B) { // JNP/JPO
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_PF)) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7C) { // JL/JNGE
        int8_t rel = mem_read8(mem, addr + 1);
        if (((cpu->flags & FLAG_SF) != 0) != ((cpu->flags & FLAG_OF) != 0)) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7D) { // JGE/JNL
        int8_t rel = mem_read8(mem, addr + 1);
        if (((cpu->flags & FLAG_SF) != 0) == ((cpu->flags & FLAG_OF) != 0)) cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7E) { // JLE/JNG
        int8_t rel = mem_read8(mem, addr + 1);
        if ((cpu->flags & FLAG_ZF) || (((cpu->flags & FLAG_SF) != 0) != ((cpu->flags & FLAG_OF) != 0)))
            cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7F) { // JG/JNLE
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_ZF) && (((cpu->flags & FLAG_SF) != 0) == ((cpu->flags & FLAG_OF) != 0)))
            cpu->ip += 2 + rel;
        else cpu->ip += 2;
        return 1;
    }

    // --- MOV segment register, r/m16 and r/m16, segment register (0x8C, 0x8E) ---
    if (opcode == 0x8C || opcode == 0x8E) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint16_t *seg_regs[4] = { &cpu->es, &cpu->cs, &cpu->ss, &cpu->ds };
        if (mod == 3) {
            if (opcode == 0x8C) *reg_table[rm] = *seg_regs[reg];
            else *seg_regs[reg] = *reg_table[rm];
        }
        // (memory forms can be added similarly)
        cpu->ip += instr_len;
        return 1;
    }

    // --- MOV r/m16, imm16 (0xC7 /0) ---
    if (opcode == 0xC7) {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint16_t imm = mem_read16(mem, addr + instr_len);
        instr_len += 2;
        if (mod == 3) *reg_table[rm] = imm;
        else {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6) { ea = mem_read16(mem, addr + 2); instr_len += 2; }
            else if (mod == 1) { disp = (int8_t)mem_read8(mem, addr + 2); instr_len += 1; }
            else if (mod == 2) { disp = mem_read16(mem, addr + 2); instr_len += 2; }
            if (!(mod == 0 && rm == 6)) ea = calc_ea(cpu, rm, disp);
            mem_write16(mem, ea, imm);
        }
        cpu->ip += instr_len;
        return 1;
    }

    // --- I/O instructions (IN, OUT) ---
    // (INT 0xCD handler is below, only once)
    if (opcode == 0xED) { // IN AX, DX
        cpu->ax = 0;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xE6) { // OUT imm8, AL
        uint8_t port = mem_read8(mem, addr + 1);
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0xE7) { // OUT imm8, AX
        uint8_t port = mem_read8(mem, addr + 1);
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0xEE) { // OUT DX, AL
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xEF) { // OUT DX, AX
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xCF) { // IRET
        cpu->ip = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->cs = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->flags = mem_read16(mem, cpu->sp); cpu->sp += 2;
        return 1;
    }
    // Main opcode switch
    if(opcode >= 0xB8 && opcode <= 0xBF){

        int reg_index = opcode - 0xB8;
        *reg_table[reg_index] = mem_read16(mem, addr + 1);
        cpu->ip += 3;
        return 1;
    }
    if(opcode >= 0x40 && opcode <=0x47){
        int reg_index = opcode - 0x40;
        (*reg_table[reg_index])++;
        set_zf(cpu, *reg_table[reg_index]);
        set_sf(cpu, *reg_table[reg_index]);
        cpu->ip += 1;
        return 1;
    }
    if(opcode >= 0x48 && opcode <= 0x4F){
        int reg_index = opcode - 0x48;
        (*reg_table[reg_index])--;
        set_zf(cpu, *reg_table[reg_index]);
        set_sf(cpu, *reg_table[reg_index]);
        cpu->ip += 1;
        return 1;
    }
    switch (opcode) {
        case 0x90: //NOP
            cpu->ip += 1;
            break;
        case 0xF4: //HLT
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
            return 0; //opcode unknown ayond stop
    }
    return 1;
}

