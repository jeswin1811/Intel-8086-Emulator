#include "../include/cpu.h"
#include "../include/memory.h"

#include <stdio.h>
#define OUTPUT_SIZE 65536
char emu_output[OUTPUT_SIZE];
size_t emu_out_pos = 0;
static uint16_t override_value = 0;

void emu_putchar(char c)
{
    if (emu_out_pos < OUTPUT_SIZE - 1)
    {
        emu_output[emu_out_pos++] = c;
        emu_output[emu_out_pos] = 0; // keep buffer NUL-terminated
    }
}

void emu_puts(const char *s)
{
    while (*s)
        emu_putchar(*s++);
}

void emu_output_flush() { emu_output[emu_out_pos] = 0; }

void cpu_init(CPU8086 *cpu)
{
    cpu->ax = cpu->bx = cpu->cx = cpu->dx = 0;
    cpu->si = cpu->di = cpu->bp = cpu->sp = 0;
    cpu->ip = 0x0000;    // satharana gathiyil 0x0000 il ninnum start cheyunne
    cpu->flags = 0x0000; // thodangumbo ella flag um clear cheyan
    cpu->cs = 0x0000;    // CS:IP -> FFFF:0000 (just for testing i put 0000)
    cpu->ds = cpu->es = cpu->ss = 0;
}

// Decode ModR/M
static void decode_modrm(uint8_t modrm, uint8_t *mod, uint8_t *reg, uint8_t *rm)
{
    *mod = (modrm >> 6) & 0x3;
    *reg = (modrm >> 3) & 0x7;
    *rm = modrm & 0x7;
}

// effective address calculate cheyan
static uint32_t calc_ea(CPU8086 *cpu, uint8_t rm, int16_t disp)
{
    switch (rm)
    {
    case 0:
        return cpu->bx + cpu->si + disp;
    case 1:
        return cpu->bx + cpu->di + disp;
    case 2:
        return cpu->bp + cpu->si + disp;
    case 3:
        return cpu->bp + cpu->di + disp;
    case 4:
        return cpu->si + disp;
    case 5:
        return cpu->di + disp;
    case 6:
        return cpu->bp + disp;
    case 7:
        return cpu->bx + disp;
    default:
        return 0;
    }
}

// setting the zero flag
static void set_zf(CPU8086 *cpu, uint16_t result)
{
    if (result == 0)
    {
        cpu->flags |= FLAG_ZF;
    }
    else
        cpu->flags &= ~FLAG_ZF;
}

// setting the sign flag
static void set_sf(CPU8086 *cpu, uint16_t result)
{
    if (result & 0x8000)
    {
        cpu->flags |= FLAG_SF;
    }
    else
        cpu->flags &= ~FLAG_SF;
}

// setting the parity flag
static void set_pf(CPU8086 *cpu, uint16_t result)
{
    uint8_t v = result & 0xFF;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    if (!(v & 1))
        cpu->flags |= FLAG_PF;
    else
        cpu->flags &= ~FLAG_PF;
}

// setting the auxiliary carry flag for ADD
static void set_af_add(CPU8086 *cpu, uint16_t a, uint16_t b)
{
    if (((a & 0xF) + (b & 0xF)) > 0xF)
        cpu->flags |= 0x10;
    else
        cpu->flags &= ~0x10;
}
// setting the auxiliary carry flag for SUB
static void set_af_sub(CPU8086 *cpu, uint16_t a, uint16_t b)
{
    if ((a & 0xF) < (b & 0xF))
        cpu->flags |= 0x10;
    else
        cpu->flags &= ~0x10;
}

// setting the carry flag for ADD
static void set_cf_add(CPU8086 *cpu, uint16_t a, uint16_t b)
{
    uint32_t result = (uint32_t)a + (uint32_t)b;
    if (result > 0xFFFF)
    {
        cpu->flags |= FLAG_CF;
    }
    else
        cpu->flags &= ~FLAG_CF;
}

// setting the overflow flag for ADD
static void set_of_add(CPU8086 *cpu, uint16_t a, uint16_t b, uint16_t result)
{
    if (((a ^ result) & (b ^ result) & 0x8000) != 0)
    {
        cpu->flags |= FLAG_OF;
    }
    else
        cpu->flags &= ~FLAG_OF;
}

// setting the carry flag for SUB
static void set_cf_sub(CPU8086 *cpu, uint16_t a, uint16_t b)
{
    if (a < b)
    {
        cpu->flags |= FLAG_CF;
    }
    else
        cpu->flags &= ~FLAG_CF;
}

// setting the overflow flag for SUB
static void set_of_sub(CPU8086 *cpu, uint16_t a, uint16_t b, uint16_t result)
{
    if (((a ^ b) & (a ^ result) & 0x8000) != 0)
    {
        cpu->flags |= FLAG_OF;
    }
    else
        cpu->flags &= ~FLAG_OF;
}

// 16-bit ADD with flag updates

static uint16_t add16(CPU8086 *cpu, uint16_t a, uint16_t b)
{
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

static uint16_t sub16(CPU8086 *cpu, uint16_t a, uint16_t b)
{
    uint16_t result = a - b;
    set_zf(cpu, result);
    set_sf(cpu, result);
    set_pf(cpu, result);
    set_cf_sub(cpu, a, b);
    set_of_sub(cpu, a, b, result);
    set_af_sub(cpu, a, b);
    return result;
}

int cpu_step(CPU8086 *cpu, Memory8086 *mem)
{
    uint32_t addr = (cpu->cs << 4) + cpu->ip;
    uint8_t opcode = mem_read8(mem, addr);
    // One-time trace when starting a program at 0000:0100
    static int traced_start = 0;
    if (!traced_start && cpu->cs == 0x0000 && cpu->ip == 0x0100)
    {
        traced_start = 1;
        fprintf(stderr, "[trace] start bytes at 0000:0100:");
        for (int i = 0; i < 12; ++i)
        {
            uint8_t b = mem_read8(mem, addr + i);
            fprintf(stderr, " %02X", b);
        }
        fprintf(stderr, "\n");
    }
    uint16_t *reg_table[8] = {
        &cpu->ax,
        &cpu->cx,
        &cpu->dx,
        &cpu->bx,
        &cpu->sp,
        &cpu->bp,
        &cpu->si,
        &cpu->di,
    };
    // 8-bit register table for AL, CL, DL, BL, AH, CH, DH, BH
    uint8_t *reg8_table[8] = {
    ((uint8_t *)&cpu->ax) + 0, // AL
    ((uint8_t *)&cpu->cx) + 0, // CL
    ((uint8_t *)&cpu->dx) + 0, // DL
    ((uint8_t *)&cpu->bx) + 0, // BL
    ((uint8_t *)&cpu->ax) + 1, // AH
    ((uint8_t *)&cpu->cx) + 1, // CH
    ((uint8_t *)&cpu->dx) + 1, // DH
    ((uint8_t *)&cpu->bx) + 1, // BH
    };
    static int rep_prefix = 0;
    static int segment_override = 0;

    // Segment override prefix
    if (opcode == 0x26)
    {
        segment_override = 1;
        override_value = cpu->es;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x2E)
    {
        segment_override = 1;
        override_value = cpu->cs;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x36)
    {
        segment_override = 1;
        override_value = cpu->ss;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x3E)
    {
        segment_override = 1;
        override_value = cpu->ds;
        cpu->ip += 1;
        return 1;
    }

    // REP/REPNZ
    if (opcode == 0xF2)
    {
        rep_prefix = 2;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xF3)
    {
        rep_prefix = 1;
        cpu->ip += 1;
        return 1;
    }

    // MOV r8, imm8 (B0..B7)
    if (opcode >= 0xB0 && opcode <= 0xB7)
    {
        uint8_t imm8 = mem_read8(mem, addr + 1);
        *reg8_table[opcode & 0x7] = imm8;
        cpu->ip += 2; // opcode + imm8
        return 1;
    }

    // MOV r16, imm16 (B8..BF)
    if (opcode >= 0xB8 && opcode <= 0xBF)
    {
        uint16_t imm16 = mem_read16(mem, addr + 1);
        *reg_table[opcode & 0x7] = imm16;
        cpu->ip += 3; // opcode + imm16
        return 1;
    }

    // MOVSW
    if (opcode == 0xA5)
    {
        uint16_t src_seg = segment_override ? override_value : cpu->ds;
        uint16_t val = mem_read16(mem, (src_seg << 4) + cpu->si);
        mem_write16(mem, (cpu->es << 4) + cpu->di, val);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            if (cpu->cx)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // MOVSB
    if (opcode == 0xA4)
    {
        uint16_t src_seg = segment_override ? override_value : cpu->ds;
        uint8_t val = mem_read8(mem, (src_seg << 4) + cpu->si);
        mem_write8(mem, (cpu->es << 4) + cpu->di, val);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            if (cpu->cx)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // LODSW
    if (opcode == 0xAD)
    {
        uint16_t src_seg = segment_override ? override_value : cpu->ds;
        cpu->ax = mem_read16(mem, (src_seg << 4) + cpu->si);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->si += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            if (cpu->cx)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // LODSB
    if (opcode == 0xAC)
    {
        uint16_t src_seg = segment_override ? override_value : cpu->ds;
        ((uint8_t *)&cpu->ax)[0] = mem_read8(mem, (src_seg << 4) + cpu->si);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->si += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            if (cpu->cx)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // STOSW
    if (opcode == 0xAB)
    {
        mem_write16(mem, (cpu->es << 4) + cpu->di, cpu->ax);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            if (cpu->cx)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // STOSB
    if (opcode == 0xAA)
    {
        mem_write8(mem, (cpu->es << 4) + cpu->di, ((uint8_t *)&cpu->ax)[0]);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            if (cpu->cx)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // SCASW
    if (opcode == 0xAF)
    {
        uint16_t val = mem_read16(mem, (cpu->es << 4) + cpu->di);
        uint16_t result = cpu->ax - val;
        set_zf(cpu, result);
        set_sf(cpu, result);
        set_pf(cpu, result);
        set_cf_sub(cpu, cpu->ax, val);
        set_of_sub(cpu, cpu->ax, val, result);
        set_af_sub(cpu, cpu->ax, val);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1)
                repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2)
                repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // SCASB
    if (opcode == 0xAE)
    {
        uint8_t val = mem_read8(mem, (cpu->es << 4) + cpu->di);
        uint8_t result = ((uint8_t *)&cpu->ax)[0] - val;
        set_zf(cpu, result);
        set_sf(cpu, result);
        set_pf(cpu, result);
        set_cf_sub(cpu, ((uint8_t *)&cpu->ax)[0], val);
        set_of_sub(cpu, ((uint8_t *)&cpu->ax)[0], val, result);
        set_af_sub(cpu, ((uint8_t *)&cpu->ax)[0], val);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1)
                repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2)
                repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // CMPSW
    if (opcode == 0xA7)
    {
        uint16_t src = mem_read16(mem, (cpu->ds << 4) + cpu->si);
        uint16_t dst = mem_read16(mem, (cpu->es << 4) + cpu->di);
        uint16_t result = src - dst;
        set_zf(cpu, result);
        set_sf(cpu, result);
        set_pf(cpu, result);
        set_cf_sub(cpu, src, dst);
        set_of_sub(cpu, src, dst, result);
        set_af_sub(cpu, src, dst);
        int inc = (cpu->flags & 0x400) ? -2 : 2;
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1)
                repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2)
                repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // CMPSB
    if (opcode == 0xA6)
    {
        uint8_t src = mem_read8(mem, (cpu->ds << 4) + cpu->si);
        uint8_t dst = mem_read8(mem, (cpu->es << 4) + cpu->di);
        uint8_t result = src - dst;
        set_zf(cpu, result);
        set_sf(cpu, result);
        set_pf(cpu, result);
        set_cf_sub(cpu, src, dst);
        set_of_sub(cpu, src, dst, result);
        set_af_sub(cpu, src, dst);
        int inc = (cpu->flags & 0x400) ? -1 : 1;
        cpu->si += inc;
        cpu->di += inc;
        cpu->ip += 1;
        if (rep_prefix && cpu->cx)
        {
            cpu->cx--;
            int repeat = 0;
            if (rep_prefix == 1)
                repeat = (cpu->flags & FLAG_ZF);
            else if (rep_prefix == 2)
                repeat = !(cpu->flags & FLAG_ZF);
            if (cpu->cx && repeat)
                cpu->ip -= 1;
            else
                rep_prefix = 0;
        }
        segment_override = 0;
        return 1;
    }
    // --- Segment override prefix (stub) ---
    // (0x26: ES, 0x2E: CS, 0x36: SS, 0x3E: DS)
    // You can implement this by setting a variable to override segment for next memory access
    if (opcode == 0x26 || opcode == 0x2E || opcode == 0x36 || opcode == 0x3E)
    {

        cpu->ip += 1;
        return 1;
    }

    // --- Far CALL/JMP/RET (stub) ---
    if (opcode == 0x9A)
    { // CALL far ptr
        uint16_t ip_new = mem_read16(mem, addr + 1);
        uint16_t cs_new = mem_read16(mem, addr + 3);
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->cs);
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->ip + 5);
        cpu->cs = cs_new;
        cpu->ip = ip_new;

        return 1;
    }
    if (opcode == 0xEA)
    { // JMP far ptr
        uint16_t ip_new = mem_read16(mem, addr + 1);
        uint16_t cs_new = mem_read16(mem, addr + 3);
        cpu->cs = cs_new;
        cpu->ip = ip_new;

        return 1;
    }
    if (opcode == 0xCB)
    { // RETF
        cpu->ip = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        cpu->cs = mem_read16(mem, cpu->sp);
        cpu->sp += 2;

        return 1;
    }

    // --- CLI/STI (Interrupt Flag) ---
    if (opcode == 0xFA)
    {
        cpu->flags &= ~0x0200;
        cpu->ip += 1;
        return 1;
    } // CLI
    if (opcode == 0xFB)
    {
        cpu->flags |= 0x0200;
        cpu->ip += 1;
        return 1;
    } // STI

    // --- Real INT/IRET handling ---
    if (opcode == 0xCD)
    { // INT imm8
        uint8_t int_num = mem_read8(mem, addr + 1);
        if (int_num == 0x21)
        {
            uint8_t ah = (cpu->ax >> 8) & 0xFF;
            switch (ah)
            {
            case 0x0: // Program terminate (DOS)
                emu_output_flush();
                return 0;
            case 0x2: // Print char in DL
            {
                uint8_t dl = ((uint8_t *)&cpu->dx)[0];
                fprintf(stderr, "[debug] INT21 AH=02 DL=0x%02X ('%c')\n", dl, (dl >= 32 && dl < 127) ? (char)dl : '.');
                emu_putchar(dl);
                cpu->ip += 2;
                return 1;
            }
            case 0x9:
            { // Print string at DS:DX, '$'-terminated
                uint32_t str_addr = (cpu->ds << 4) + cpu->dx;
                for (;;)
                {
                    char ch = mem_read8(mem, str_addr++);
                    if (ch == '$')
                        break;
                    emu_putchar(ch);
                }
                cpu->ip += 2;
                return 1;
            }
            case 0x1:
            { // Read char to AL (stub: return 'A')
                ((uint8_t *)&cpu->ax)[0] = 'A';
                cpu->ip += 2;
                return 1;
            }
            case 0x3D: // Open file
                emu_puts("[DOS] INT 21h AH=3Dh: Open file (not implemented)\n");
                cpu->ip += 2;
                return 1;
            case 0x3E: // Close file
                emu_puts("[DOS] INT 21h AH=3Eh: Close file (not implemented)\n");
                cpu->ip += 2;
                return 1;
            case 0x3F: // Read file
                emu_puts("[DOS] INT 21h AH=3Fh: Read file (not implemented)\n");
                cpu->ip += 2;
                return 1;
            case 0x40: // Write file
                emu_puts("[DOS] INT 21h AH=40h: Write file (not implemented)\n");
                cpu->ip += 2;
                return 1;
            case 0x48: // Allocate memory
                emu_puts("[DOS] INT 21h AH=48h: Allocate memory (not implemented)\n");
                cpu->ip += 2;
                return 1;
            case 0x49: // Free memory
                emu_puts("[DOS] INT 21h AH=49h: Free memory (not implemented)\n");
                cpu->ip += 2;
                return 1;
            case 0x4A: // Resize memory block
                emu_puts("[DOS] INT 21h AH=4Ah: Resize memory block (not implemented)\n");
                cpu->ip += 2;
                return 1;
            case 0x4C: // Exit
                fprintf(stderr, "[debug] INT21 AH=4C exit\n");
                emu_output_flush();
                return 0;
            default:
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "[DOS] INT 21h AH=%02Xh not implemented\n", ah);
                emu_puts(buf);
                cpu->ip += 2;
                return 1;
            }
            }
        }
        else if (int_num == 0x10)
        {
            emu_puts("[BIOS] INT 10h: Video service (not implemented)\n");
            cpu->ip += 2;
            return 1;
        }
        else if (int_num == 0x16)
        {
            emu_puts("[BIOS] INT 16h: Keyboard service (not implemented)\n");
            cpu->ip += 2;
            return 1;
        }
        // Default INT handler (push flags/cs/ip, jump to IVT)
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->flags);
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->cs);
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->ip + 2);
        cpu->ip = mem_read16(mem, int_num * 4);
        cpu->cs = mem_read16(mem, int_num * 4 + 2);
        return 1;
    }
    if (opcode == 0xCF)
    { // IRET
        cpu->ip = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        cpu->cs = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        cpu->flags = mem_read16(mem, cpu->sp);
        cpu->sp += 2;

        return 1;
    }

    // MOV r/m16, r16 (0x89) and MOV r16, r/m16 (0x8b)
    if (opcode == 0x89 || opcode == 0x8B)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2; // opcode + modrm
        if (mod == 3)
        {
            // register to register
            if (opcode == 0x89)
            {
                *reg_table[rm] = *reg_table[reg];
            }
            else
            {
                *reg_table[reg] = *reg_table[rm];
            }
        }
        else
        {
            // memory operand
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6))
            {
                ea = calc_ea(cpu, rm, disp);
            }
            if (opcode == 0x89)
            {
                mem_write16(mem, ea, *reg_table[reg]);
            }
            else
            {
                *reg_table[reg] = mem_read16(mem, ea);
            }
        }
        cpu->ip += instr_len;
        return 1;
    }

    // ADD/SUB r/m16, r16 and r16, r/m16
    if (opcode == 0x01 || opcode == 0x03 || opcode == 0x29 || opcode == 0x2B)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2; // opcode + modrm

        if (mod == 3)
        {
            // Register to register
            if (opcode == 0x01)
            { // ADD r/m16, r16
                *reg_table[rm] = add16(cpu, *reg_table[rm], *reg_table[reg]);
            }
            else if (opcode == 0x03)
            { // ADD r16, r/m16
                *reg_table[reg] = add16(cpu, *reg_table[reg], *reg_table[rm]);
            }
            else if (opcode == 0x29)
            { // SUB r/m16, r16
                *reg_table[rm] = sub16(cpu, *reg_table[rm], *reg_table[reg]);
            }
            else if (opcode == 0x2B)
            { // SUB r16, r/m16
                *reg_table[reg] = sub16(cpu, *reg_table[reg], *reg_table[rm]);
            }
        }
        else
        {
            // Memory operand
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6))
            {
                ea = calc_ea(cpu, rm, disp);
            }
            if (opcode == 0x01)
            { // ADD [mem], r16
                uint16_t val = mem_read16(mem, ea);
                val = add16(cpu, val, *reg_table[reg]);
                mem_write16(mem, ea, val);
            }
            else if (opcode == 0x03)
            { // ADD r16, [mem]
                uint16_t val = mem_read16(mem, ea);
                *reg_table[reg] = add16(cpu, *reg_table[reg], val);
            }
            else if (opcode == 0x29)
            { // SUB [mem], r16
                uint16_t val = mem_read16(mem, ea);
                val = sub16(cpu, val, *reg_table[reg]);
                mem_write16(mem, ea, val);
            }
            else if (opcode == 0x2B)
            { // SUB r16, [mem]
                uint16_t val = mem_read16(mem, ea);
                *reg_table[reg] = sub16(cpu, *reg_table[reg], val);
            }
        }
        cpu->ip = addr + instr_len - (cpu->cs << 4); // Ensure IP is incremented by instr_len from current addr
        return 1;
    }

    // Handle 0x81: ADD/SUB r/m16, imm16 (ModR/M-based)
    if (opcode == 0x81)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);

        // Support ADD(0), OR(1), AND(4), SUB(5), XOR(6), CMP(7)
        if (reg == 0 || reg == 1 || reg == 4 || reg == 5 || reg == 6 || reg == 7)
        {
            uint16_t imm;
            uint16_t old_val, result;
            uint32_t ea = 0;
            int instr_len = 2; // opcode+modrm

            if (mod == 3)
            { // register-direct
                old_val = *reg_table[rm];
                imm = mem_read16(mem, addr + 2);
                instr_len += 2;
                switch (reg)
                {
                case 0: // ADD
                    *reg_table[rm] = add16(cpu, old_val, imm);
                    break;
                case 1: // OR
                    result = old_val | imm;
                    *reg_table[rm] = result;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 4: // AND
                    result = old_val & imm;
                    *reg_table[rm] = result;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 5: // SUB
                    *reg_table[rm] = sub16(cpu, old_val, imm);
                    break;
                case 6: // XOR
                    result = old_val ^ imm;
                    *reg_table[rm] = result;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 7: // CMP
                    result = old_val - imm;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    set_cf_sub(cpu, old_val, imm);
                    set_of_sub(cpu, old_val, imm, result);
                    break;
                }
            }
            else
            { // Memory operand
                int16_t disp = 0;
                if (mod == 0 && rm == 6)
                { // direct address
                    ea = mem_read16(mem, addr + 2);
                    instr_len += 2;
                }
                else if (mod == 1)
                { // 8-bit displacement
                    disp = (int8_t)mem_read8(mem, addr + 2);
                    instr_len += 1;
                }
                else if (mod == 2)
                { // 16-bit displacement
                    disp = mem_read16(mem, addr + 2);
                    instr_len += 2;
                }
                if (!(mod == 0 && rm == 6))
                {
                    ea = calc_ea(cpu, rm, disp);
                }
                imm = mem_read16(mem, addr + instr_len);
                instr_len += 2;
                old_val = mem_read16(mem, ea);
                switch (reg)
                {
                case 0: // ADD
                    result = old_val + imm;
                    mem_write16(mem, ea, result);
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    set_cf_add(cpu, old_val, imm);
                    set_of_add(cpu, old_val, imm, result);
                    break;
                case 1: // OR
                    result = old_val | imm;
                    mem_write16(mem, ea, result);
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 4: // AND
                    result = old_val & imm;
                    mem_write16(mem, ea, result);
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 5: // SUB
                    result = old_val - imm;
                    mem_write16(mem, ea, result);
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    set_cf_sub(cpu, old_val, imm);
                    set_of_sub(cpu, old_val, imm, result);
                    break;
                case 6: // XOR
                    result = old_val ^ imm;
                    mem_write16(mem, ea, result);
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 7: // CMP
                    result = old_val - imm;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    set_cf_sub(cpu, old_val, imm);
                    set_of_sub(cpu, old_val, imm, result);
                    break;
                }
            }
            cpu->ip += instr_len;
            return 1;
        }
    }

    // AND/OR/XOR/CMP r/m16, r16 and r16, r/m16
    if (opcode == 0x21 || opcode == 0x23 || opcode == 0x09 || opcode == 0x0B ||
        opcode == 0x31 || opcode == 0x33 || opcode == 0x39 || opcode == 0x3B)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2; // opcode + modrm

// Helper lambdas for logic ops
#define LOGIC_OP(op, a, b) ((a)op(b))
#define SET_LOGIC_FLAGS(cpu, result) \
    set_zf(cpu, result);             \
    set_sf(cpu, result);             \
    cpu->flags &= ~(FLAG_CF | FLAG_OF);

        if (mod == 3)
        {
            uint16_t src = *reg_table[reg];
            uint16_t dst = *reg_table[rm];
            uint16_t result = 0;
            if (opcode == 0x21)
            { // AND r/m16, r16
                result = dst & src;
                *reg_table[rm] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x23)
            { // AND r16, r/m16
                result = *reg_table[reg] & *reg_table[rm];
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x09)
            { // OR r/m16, r16
                result = dst | src;
                *reg_table[rm] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x0B)
            { // OR r16, r/m16
                result = *reg_table[reg] | *reg_table[rm];
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x31)
            { // XOR r/m16, r16
                result = dst ^ src;
                *reg_table[rm] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x33)
            { // XOR r16, r/m16
                result = *reg_table[reg] ^ *reg_table[rm];
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x39)
            { // CMP r/m16, r16
                result = dst - src;
                set_zf(cpu, result);
                set_sf(cpu, result);
                set_cf_sub(cpu, dst, src);
                set_of_sub(cpu, dst, src, result);
            }
            else if (opcode == 0x3B)
            { // CMP r16, r/m16
                result = *reg_table[reg] - *reg_table[rm];
                set_zf(cpu, result);
                set_sf(cpu, result);
                set_cf_sub(cpu, *reg_table[reg], *reg_table[rm]);
                set_of_sub(cpu, *reg_table[reg], *reg_table[rm], result);
            }
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6))
            {
                ea = calc_ea(cpu, rm, disp);
            }
            uint16_t src = *reg_table[reg];
            uint16_t dst = mem_read16(mem, ea);
            uint16_t result = 0;
            if (opcode == 0x21)
            { // AND [mem], r16
                result = dst & src;
                mem_write16(mem, ea, result);
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x23)
            { // AND r16, [mem]
                result = *reg_table[reg] & dst;
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x09)
            { // OR [mem], r16
                result = dst | src;
                mem_write16(mem, ea, result);
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x0B)
            { // OR r16, [mem]
                result = *reg_table[reg] | dst;
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x31)
            { // XOR [mem], r16
                result = dst ^ src;
                mem_write16(mem, ea, result);
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x33)
            { // XOR r16, [mem]
                result = *reg_table[reg] ^ dst;
                *reg_table[reg] = result;
                SET_LOGIC_FLAGS(cpu, result);
            }
            else if (opcode == 0x39)
            { // CMP [mem], r16
                result = dst - src;
                set_zf(cpu, result);
                set_sf(cpu, result);
                set_cf_sub(cpu, dst, src);
                set_of_sub(cpu, dst, src, result);
            }
            else if (opcode == 0x3B)
            { // CMP r16, [mem]
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

    // AND/OR/XOR/CMP r/m8, r8 and r8, r/m8 (0x20,0x22,0x08,0x0A,0x30,0x32,0x38,0x3A)
    if (opcode == 0x20 || opcode == 0x22 || opcode == 0x08 || opcode == 0x0A ||
        opcode == 0x30 || opcode == 0x32 || opcode == 0x38 || opcode == 0x3A)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        if (mod == 3)
        {
            uint8_t src = *reg8_table[reg];
            uint8_t dst = *reg8_table[rm];
            uint8_t result = 0;
            if (opcode == 0x20)
            { // AND r/m8, r8
                result = dst & src;
                *reg8_table[rm] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x22)
            { // AND r8, r/m8
                result = src & dst;
                *reg8_table[reg] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x08)
            { // OR r/m8, r8
                result = dst | src;
                *reg8_table[rm] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x0A)
            { // OR r8, r/m8
                result = src | dst;
                *reg8_table[reg] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x30)
            { // XOR r/m8, r8
                result = dst ^ src;
                *reg8_table[rm] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x32)
            { // XOR r8, r/m8
                result = src ^ dst;
                *reg8_table[reg] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x38)
            { // CMP r/m8, r8
                uint8_t r = dst - src;
                set_zf(cpu, r);
                if (r & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, r);
                if (dst < src)
                    cpu->flags |= FLAG_CF;
                else
                    cpu->flags &= ~FLAG_CF;
                if (((dst ^ src) & (dst ^ r) & 0x80) != 0)
                    cpu->flags |= FLAG_OF;
                else
                    cpu->flags &= ~FLAG_OF;
            }
            else if (opcode == 0x3A)
            { // CMP r8, r/m8
                uint8_t r = src - dst;
                set_zf(cpu, r);
                if (r & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, r);
                if (src < dst)
                    cpu->flags |= FLAG_CF;
                else
                    cpu->flags &= ~FLAG_CF;
                if (((src ^ dst) & (src ^ r) & 0x80) != 0)
                    cpu->flags |= FLAG_OF;
                else
                    cpu->flags &= ~FLAG_OF;
            }
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6))
                ea = calc_ea(cpu, rm, disp);
            uint8_t src = *reg8_table[reg];
            uint8_t dst = mem_read8(mem, seg + ea);
            uint8_t result = 0;
            if (opcode == 0x20)
            {
                result = dst & src;
                mem_write8(mem, seg + ea, result);
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x22)
            {
                result = dst & src;
                *reg8_table[reg] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x08)
            {
                result = dst | src;
                mem_write8(mem, seg + ea, result);
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x0A)
            {
                result = dst | src;
                *reg8_table[reg] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x30)
            {
                result = dst ^ src;
                mem_write8(mem, seg + ea, result);
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x32)
            {
                result = dst ^ src;
                *reg8_table[reg] = result;
                set_zf(cpu, result);
                if (result & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, result);
                cpu->flags &= ~(FLAG_CF | FLAG_OF);
            }
            else if (opcode == 0x38)
            {
                uint8_t r = dst - src;
                set_zf(cpu, r);
                if (r & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, r);
                if (dst < src)
                    cpu->flags |= FLAG_CF;
                else
                    cpu->flags &= ~FLAG_CF;
                if (((dst ^ src) & (dst ^ r) & 0x80) != 0)
                    cpu->flags |= FLAG_OF;
                else
                    cpu->flags &= ~FLAG_OF;
            }
            else if (opcode == 0x3A)
            {
                uint8_t r = src - dst;
                set_zf(cpu, r);
                if (r & 0x80)
                    cpu->flags |= FLAG_SF;
                else
                    cpu->flags &= ~FLAG_SF;
                set_pf(cpu, r);
                if (src < dst)
                    cpu->flags |= FLAG_CF;
                else
                    cpu->flags &= ~FLAG_CF;
                if (((src ^ dst) & (src ^ r) & 0x80) != 0)
                    cpu->flags |= FLAG_OF;
                else
                    cpu->flags &= ~FLAG_OF;
            }
        }
        cpu->ip = addr + instr_len - (cpu->cs << 4);
        segment_override = 0;
        return 1;
    }

    // MOV r/m8, r8 (0x88) and MOV r8, r/m8 (0x8A)
    if (opcode == 0x88 || opcode == 0x8A)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        if (mod == 3)
        {
            if (opcode == 0x88)
                *reg8_table[rm] = *reg8_table[reg];
            else
                *reg8_table[reg] = *reg8_table[rm];
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            ea = calc_ea(cpu, rm, disp);
            if (opcode == 0x88)
            {
                mem_write8(mem, seg + ea, *reg8_table[reg]);
            }
            else
            {
                uint8_t v = mem_read8(mem, seg + ea);
                *reg8_table[reg] = v;
            }
        }
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // Shift/rotate r/m8 imm8 (0xC0) and r/m16 imm8 (0xC1)
    if (opcode == 0xC0 || opcode == 0xC1)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint8_t count = mem_read8(mem, addr + 2) & 0x1F; // only low 5 bits used
        instr_len += 1;
        int is16 = (opcode == 0xC1);
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;

        if (mod == 3)
        {
            if (is16)
            {
                uint16_t *dst = reg_table[rm];
                while (count--)
                {
                    if (reg == 4)
                        *dst <<= 1; // SHL
                    else if (reg == 5)
                        *dst >>= 1; // SHR
                    else if (reg == 0)
                        *dst = (*dst << 1) | ((*dst & 0x8000) ? 1 : 0); // ROL
                    else if (reg == 1)
                        *dst = (*dst >> 1) | ((*dst & 1) ? 0x8000 : 0); // ROR
                }
            }
            else
            {
                uint8_t *dst = reg8_table[rm];
                while (count--)
                {
                    if (reg == 4)
                        *dst <<= 1;
                    else if (reg == 5)
                        *dst >>= 1;
                    else if (reg == 0)
                        *dst = (*dst << 1) | ((*dst & 0x80) ? 1 : 0);
                    else if (reg == 1)
                        *dst = (*dst >> 1) | ((*dst & 1) ? 0x80 : 0);
                }
            }
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            ea = calc_ea(cpu, rm, disp);
            if (is16)
            {
                uint16_t val = mem_read16(mem, seg + ea);
                while (count--)
                {
                    if (reg == 4)
                        val <<= 1;
                    else if (reg == 5)
                        val >>= 1;
                    else if (reg == 0)
                        val = (val << 1) | ((val & 0x8000) ? 1 : 0);
                    else if (reg == 1)
                        val = (val >> 1) | ((val & 1) ? 0x8000 : 0);
                }
                mem_write16(mem, seg + ea, val);
            }
            else
            {
                uint8_t val = mem_read8(mem, seg + ea);
                while (count--)
                {
                    if (reg == 4)
                        val <<= 1;
                    else if (reg == 5)
                        val >>= 1;
                    else if (reg == 0)
                        val = (val << 1) | ((val & 0x80) ? 1 : 0);
                    else if (reg == 1)
                        val = (val >> 1) | ((val & 1) ? 0x80 : 0);
                }
                mem_write8(mem, seg + ea, val);
            }
        }
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // SHL/SHR/ROL/ROR r/m8, 1 (0xD0), r/m8, CL (0xD2), r/m16, 1 (0xD1), r/m16, CL (0xD3)
    if (opcode == 0xD0 || opcode == 0xD2 || opcode == 0xD1 || opcode == 0xD3)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        int is16 = (opcode == 0xD1 || opcode == 0xD3);
        int count = (opcode == 0xD0 || opcode == 0xD1) ? 1 : ((cpu->cx) & 0xFF);
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        if (mod == 3)
        {
            if (is16)
            {
                uint16_t *dst = reg_table[rm];
                while (count--)
                {
                    if (reg == 4)
                        *dst <<= 1; // SHL
                    else if (reg == 5)
                        *dst >>= 1; // SHR
                    else if (reg == 0)
                        *dst = (*dst << 1) | ((*dst & 0x8000) ? 1 : 0); // ROL
                    else if (reg == 1)
                        *dst = (*dst >> 1) | ((*dst & 1) ? 0x8000 : 0); // ROR
                }
            }
            else
            {
                uint8_t *dst = reg8_table[rm];
                while (count--)
                {
                    if (reg == 4)
                        *dst <<= 1;
                    else if (reg == 5)
                        *dst >>= 1;
                    else if (reg == 0)
                        *dst = (*dst << 1) | ((*dst & 0x80) ? 1 : 0);
                    else if (reg == 1)
                        *dst = (*dst >> 1) | ((*dst & 1) ? 0x80 : 0);
                }
            }
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            ea = calc_ea(cpu, rm, disp);
            if (is16)
            {
                uint16_t val = mem_read16(mem, seg + ea);
                while (count--)
                {
                    if (reg == 4)
                        val <<= 1;
                    else if (reg == 5)
                        val >>= 1;
                    else if (reg == 0)
                        val = (val << 1) | ((val & 0x8000) ? 1 : 0);
                    else if (reg == 1)
                        val = (val >> 1) | ((val & 1) ? 0x8000 : 0);
                }
                mem_write16(mem, seg + ea, val);
            }
            else
            {
                uint8_t val = mem_read8(mem, seg + ea);
                while (count--)
                {
                    if (reg == 4)
                        val <<= 1;
                    else if (reg == 5)
                        val >>= 1;
                    else if (reg == 0)
                        val = (val << 1) | ((val & 0x80) ? 1 : 0);
                    else if (reg == 1)
                        val = (val >> 1) | ((val & 1) ? 0x80 : 0);
                }
                mem_write8(mem, seg + ea, val);
            }
        }
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // XCHG r/m8, r8 (0x86), XCHG r/m16, r16 (0x87)
    if (opcode == 0x86 || opcode == 0x87)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        if (mod == 3)
        {
            if (opcode == 0x86)
            {
                uint8_t tmp = *reg8_table[reg];
                *reg8_table[reg] = *reg8_table[rm];
                *reg8_table[rm] = tmp;
            }
            else
            {
                uint16_t tmp = *reg_table[reg];
                *reg_table[reg] = *reg_table[rm];
                *reg_table[rm] = tmp;
            }
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            ea = calc_ea(cpu, rm, disp);
            if (opcode == 0x86)
            {
                uint8_t tmp = mem_read8(mem, seg + ea);
                mem_write8(mem, seg + ea, *reg8_table[reg]);
                *reg8_table[reg] = tmp;
            }
            else
            {
                uint16_t tmp = mem_read16(mem, seg + ea);
                mem_write16(mem, seg + ea, *reg_table[reg]);
                *reg_table[reg] = tmp;
            }
        }
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // LEA r16, m (0x8D)
    if (opcode == 0x8D)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        int16_t disp = 0;
        if (mod == 0 && rm == 6)
        {
            disp = mem_read16(mem, addr + 2);
            instr_len += 2;
        }
        else if (mod == 1)
        {
            disp = (int8_t)mem_read8(mem, addr + 2);
            instr_len += 1;
        }
        else if (mod == 2)
        {
            disp = mem_read16(mem, addr + 2);
            instr_len += 2;
        }
        *reg_table[reg] = calc_ea(cpu, rm, disp);
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // TEST r/m8, r8 (0x84), TEST r/m16, r16 (0x85)
    if (opcode == 0x84 || opcode == 0x85)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        if (mod == 3)
        {
            if (opcode == 0x84)
            {
                uint8_t res = *reg8_table[rm] & *reg8_table[reg];
                set_zf(cpu, res);
                set_sf(cpu, res);
                set_pf(cpu, res);
            }
            else
            {
                uint16_t res = *reg_table[rm] & *reg_table[reg];
                set_zf(cpu, res);
                set_sf(cpu, res);
                set_pf(cpu, res);
            }
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            ea = calc_ea(cpu, rm, disp);
            if (opcode == 0x84)
            {
                uint8_t res = mem_read8(mem, seg + ea) & *reg8_table[reg];
                set_zf(cpu, res);
                set_sf(cpu, res);
                set_pf(cpu, res);
            }
            else
            {
                uint16_t res = mem_read16(mem, seg + ea) & *reg_table[reg];
                set_zf(cpu, res);
                set_sf(cpu, res);
                set_pf(cpu, res);
            }
        }
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // --- PUSH/POP segment registers (0x06, 0x0E, 0x16, 0x1E, 0x07, 0x17, 0x1F) ---
    if (opcode == 0x06)
    {
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->es);
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x0E)
    {
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->cs);
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x16)
    {
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->ss);
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x1E)
    {
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->ds);
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x07)
    {
        cpu->es = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x17)
    {
        cpu->ss = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x1F)
    {
        cpu->ds = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        cpu->ip += 1;
        return 1;
    }

    // PUSH r16 (0x50..0x57)
    if (opcode >= 0x50 && opcode <= 0x57)
    {
        uint8_t reg = opcode & 0x7;
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, *reg_table[reg]);
        cpu->ip += 1;
        return 1;
    }
    // PUSHA / POPA (0x60, 0x61)
    if (opcode == 0x60)
    { // PUSHA: push AX,CX,DX,BX,SP,BP,SI,DI (push original SP)
        uint16_t old_sp = cpu->sp;
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->ax);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->cx);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->dx);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->bx);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, old_sp);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->bp);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->si);
        cpu->sp -= 2; mem_write16(mem, cpu->sp, cpu->di);
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0x61)
    { // POPA: pop DI,SI,BP,SP(discard),BX,DX,CX,AX
        cpu->di = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->si = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->bp = mem_read16(mem, cpu->sp); cpu->sp += 2;
        /* pop into a temp and discard for SP */
        {
            uint16_t tmp = mem_read16(mem, cpu->sp);
            (void)tmp;
            cpu->sp += 2;
        }
        cpu->bx = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->dx = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->cx = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->ax = mem_read16(mem, cpu->sp); cpu->sp += 2;
        cpu->ip += 1;
        return 1;
    }
    // POP r16 (0x58..0x5F)
    if (opcode >= 0x58 && opcode <= 0x5F)
    {
        uint8_t reg = opcode & 0x7;
        *reg_table[reg] = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        cpu->ip += 1;
        return 1;
    }

    // INC r16 (0x40..0x47) and DEC r16 (0x48..0x4F)
    if ((opcode >= 0x40 && opcode <= 0x47) || (opcode >= 0x48 && opcode <= 0x4F))
    {
        uint8_t reg = opcode & 0x7;
        int is_dec = (opcode >= 0x48 && opcode <= 0x4F);
        uint16_t old = *reg_table[reg];
        uint16_t result = is_dec ? (old - 1) : (old + 1);
        *reg_table[reg] = result;
        /* INC/DEC affect SF, ZF, OF, AF but NOT CF */
        set_zf(cpu, result);
        set_sf(cpu, result);
        set_pf(cpu, result);
        if (is_dec)
            set_of_sub(cpu, old, 1, result);
        else
            set_of_add(cpu, old, 1, result);
        /* AF */
        if (is_dec)
        {
            if ((old & 0xF) < (1 & 0xF))
                cpu->flags |= 0x10;
            else
                cpu->flags &= ~0x10;
        }
        else
        {
            if (((old & 0xF) + (1 & 0xF)) > 0xF)
                cpu->flags |= 0x10;
            else
                cpu->flags &= ~0x10;
        }
        cpu->ip += 1;
        return 1;
    }

        // PUSH imm16 (0x68) and PUSH imm8 (0x6A)
        if (opcode == 0x68)
        {
            uint16_t imm16 = mem_read16(mem, addr + 1);
            cpu->sp -= 2;
            mem_write16(mem, cpu->sp, imm16);
            cpu->ip += 3; // opcode + imm16
            return 1;
        }
        if (opcode == 0x6A)
        {
            int8_t imm8 = (int8_t)mem_read8(mem, addr + 1);
            uint16_t val = (uint16_t)imm8;
            cpu->sp -= 2;
            mem_write16(mem, cpu->sp, val);
            cpu->ip += 2; // opcode + imm8
            return 1;
        }

    // --- Conditional jumps (short, 8-bit displacement) ---
    if (opcode == 0x74)
    { // JE/JZ
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_ZF)
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x75)
    { // JNE/JNZ
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_ZF))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x72)
    { // JC
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_CF)
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x73)
    { // JNC
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_CF))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x78)
    { // JS
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_SF)
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x79)
    { // JNS
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_SF))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7A)
    { // JP/JPE
        int8_t rel = mem_read8(mem, addr + 1);
        if (cpu->flags & FLAG_PF)
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7B)
    { // JNP/JPO
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_PF))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7C)
    { // JL/JNGE
        int8_t rel = mem_read8(mem, addr + 1);
        if (((cpu->flags & FLAG_SF) != 0) != ((cpu->flags & FLAG_OF) != 0))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7D)
    { // JGE/JNL
        int8_t rel = mem_read8(mem, addr + 1);
        if (((cpu->flags & FLAG_SF) != 0) == ((cpu->flags & FLAG_OF) != 0))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7E)
    { // JLE/JNG
        int8_t rel = mem_read8(mem, addr + 1);
        if ((cpu->flags & FLAG_ZF) || (((cpu->flags & FLAG_SF) != 0) != ((cpu->flags & FLAG_OF) != 0)))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x7F)
    { // JG/JNLE
        int8_t rel = mem_read8(mem, addr + 1);
        if (!(cpu->flags & FLAG_ZF) && (((cpu->flags & FLAG_SF) != 0) == ((cpu->flags & FLAG_OF) != 0)))
            cpu->ip += 2 + rel;
        else
            cpu->ip += 2;
        return 1;
    }

    // CALL near (0xE8), JMP near (0xE9), JMP short (0xEB)
    if (opcode == 0xE8)
    { // CALL rel16
        int16_t rel = (int16_t)mem_read16(mem, addr + 1);
        /* push return IP */
        cpu->sp -= 2;
        mem_write16(mem, cpu->sp, cpu->ip + 3);
        cpu->ip = (uint16_t)(cpu->ip + 3 + rel);
        return 1;
    }
    if (opcode == 0xE9)
    { // JMP rel16
        int16_t rel = (int16_t)mem_read16(mem, addr + 1);
        cpu->ip = (uint16_t)(cpu->ip + 3 + rel);
        return 1;
    }
    if (opcode == 0xEB)
    { // JMP short rel8
        int8_t rel = (int8_t)mem_read8(mem, addr + 1);
        cpu->ip = (uint16_t)(cpu->ip + 2 + rel);
        return 1;
    }

    // RET near (0xC3) and RET imm16 (0xC2)
    if (opcode == 0xC3)
    {
        cpu->ip = mem_read16(mem, cpu->sp);
        cpu->sp += 2;
        return 1;
    }
    if (opcode == 0xC2)
    {
        uint16_t popbytes = mem_read16(mem, addr + 1);
        cpu->ip = mem_read16(mem, cpu->sp);
        cpu->sp += 2 + popbytes;
        return 1;
    }

    // LOOPNZ (0xE0), LOOPZ (0xE1), LOOP (0xE2), JCXZ (0xE3)
    if (opcode == 0xE0 || opcode == 0xE1 || opcode == 0xE2 || opcode == 0xE3)
    {
        int8_t rel = (int8_t)mem_read8(mem, addr + 1);
        if (opcode == 0xE2)
        { // LOOP
            cpu->cx--;
            if (cpu->cx != 0)
                cpu->ip += 2 + rel;
            else
                cpu->ip += 2;
        }
        else if (opcode == 0xE0)
        { // LOOPNZ / LOOPNE
            cpu->cx--;
            if (cpu->cx != 0 && !(cpu->flags & FLAG_ZF))
                cpu->ip += 2 + rel;
            else
                cpu->ip += 2;
        }
        else if (opcode == 0xE1)
        { // LOOPZ / LOOPE
            cpu->cx--;
            if (cpu->cx != 0 && (cpu->flags & FLAG_ZF))
                cpu->ip += 2 + rel;
            else
                cpu->ip += 2;
        }
        else if (opcode == 0xE3)
        { // JCXZ
            if (cpu->cx == 0)
                cpu->ip += 2 + rel;
            else
                cpu->ip += 2;
        }
        return 1;
    }

    // --- MOV segment register, r/m16 and r/m16, segment register (0x8C, 0x8E) ---
    if (opcode == 0x8C || opcode == 0x8E)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint16_t *seg_regs[4] = {&cpu->es, &cpu->cs, &cpu->ss, &cpu->ds};
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        if (mod == 3)
        {
            if (opcode == 0x8C)
                *reg_table[rm] = *seg_regs[reg];
            else
                *seg_regs[reg] = *reg_table[rm];
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            ea = calc_ea(cpu, rm, disp);
            if (opcode == 0x8C)
            {
                // MOV r/m16, Sreg : write segment register value into memory
                uint16_t v = *seg_regs[reg];
                mem_write16(mem, seg + ea, v);
            }
            else
            {
                // MOV Sreg, r/m16 : load 16-bit from memory into segment register
                uint16_t v = mem_read16(mem, seg + ea);
                *seg_regs[reg] = v;
            }
        }
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // NOP (0x90)
    if (opcode == 0x90)
    {
        cpu->ip += 1;
        return 1;
    }

    // HLT (0xF4)
    if (opcode == 0xF4)
    {
        emu_puts("HLT encountered - stopping emulator.\n");
        emu_output_flush();
        return 0;
    }

    // WAIT/FWAIT (0x9B)
    if (opcode == 0x9B)
    {
        // no-op for single-threaded emulator
        cpu->ip += 1;
        return 1;
    }

    // LOCK prefix (0xF0) and ESC (0xD8-0xDF as escape opcodes) - treat as no-op/prefix
    if (opcode == 0xF0)
    {
        // LOCK prefix: ignored in single-threaded emulator
        cpu->ip += 1;
        return 1;
    }

    if (opcode >= 0xD8 && opcode <= 0xDF)
    {
        // ESC / coprocessor escape - not implemented, skip as 2-byte instr
        cpu->ip += 2;
        return 1;
    }

    // Flag manipulation: CLC (0xF8), STC (0xF9), CMC (0xF5)
    if (opcode == 0xF8)
    {
        cpu->flags &= ~FLAG_CF;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xF9)
    {
        cpu->flags |= FLAG_CF;
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xF5)
    {
        cpu->flags ^= FLAG_CF;
        cpu->ip += 1;
        return 1;
    }

    // Direction flag: CLD (0xFC), STD (0xFD)
    if (opcode == 0xFC)
    {
        cpu->flags &= ~0x400; // clear DF
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xFD)
    {
        cpu->flags |= 0x400; // set DF
        cpu->ip += 1;
        return 1;
    }

    // ADC (0x10/0x12/0x14/0x15 group for 8/16-bit forms) and SBB (0x18/0x1A/0x1C/0x1D)
    // Implement simple ADC/SBB for immediate/add to AL/AX (0x14/0x15 and 0x1C/0x1D)
    if (opcode == 0x14)
    { // ADC AL, imm8
        uint8_t imm8 = mem_read8(mem, addr + 1);
        uint8_t cf = (cpu->flags & FLAG_CF) ? 1 : 0;
        uint16_t res = (uint16_t)((uint8_t)((uint8_t *)&cpu->ax)[0]) + imm8 + cf;
        ((uint8_t *)&cpu->ax)[0] = (uint8_t)res;
        set_zf(cpu, (uint16_t)((uint8_t)res));
        set_sf(cpu, (uint16_t)((uint8_t)res));
        set_pf(cpu, (uint16_t)((uint8_t)res));
        if (res > 0xFF) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x15)
    { // ADC AX, imm16
        uint16_t imm16 = mem_read16(mem, addr + 1);
        uint32_t res = (uint32_t)cpu->ax + imm16 + ((cpu->flags & FLAG_CF) ? 1 : 0);
        cpu->ax = (uint16_t)res;
        set_zf(cpu, (uint16_t)res);
        set_sf(cpu, (uint16_t)res);
        set_pf(cpu, (uint16_t)res);
        if (res > 0xFFFF) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
        cpu->ip += 3;
        return 1;
    }
    if (opcode == 0x1C)
    { // SBB AL, imm8
        uint8_t imm8 = mem_read8(mem, addr + 1);
        uint8_t cf = (cpu->flags & FLAG_CF) ? 1 : 0;
        uint8_t al = ((uint8_t *)&cpu->ax)[0];
        uint16_t res = (uint16_t)al - imm8 - cf;
        ((uint8_t *)&cpu->ax)[0] = (uint8_t)res;
        set_zf(cpu, (uint16_t)((uint8_t)res));
        set_sf(cpu, (uint16_t)((uint8_t)res));
        set_pf(cpu, (uint16_t)((uint8_t)res));
        if ((uint16_t)al < (uint16_t)(imm8 + cf)) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x1D)
    { // SBB AX, imm16
        uint16_t imm16 = mem_read16(mem, addr + 1);
        uint32_t res = (uint32_t)cpu->ax - imm16 - ((cpu->flags & FLAG_CF) ? 1 : 0);
        cpu->ax = (uint16_t)res;
        set_zf(cpu, (uint16_t)res);
        set_sf(cpu, (uint16_t)res);
        set_pf(cpu, (uint16_t)res);
        if ((uint32_t)cpu->ax < (uint32_t)imm16 + ((cpu->flags & FLAG_CF) ? 1 : 0)) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
        cpu->ip += 3;
        return 1;
    }

    // NEG (0xF7 /3 or 0xF6 /3 for 8-bit). Implement simple NEG AL/AX immediate forms: 0xF6/0xF7 already handle groups; provide small AL/AX imm direct if used
    if (opcode == 0xF6 || opcode == 0xF7)
    {
        // existing MUL/DIV group handles reg values; extend group case 3 (NEG) if not already present
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        if (reg == 3)
        {
            int instr_len = 2;
            uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
            if (opcode == 0xF6)
            {
                // NEG r/m8
                if (mod == 3)
                {
                    uint8_t v = *reg8_table[rm];
                    uint8_t res = (uint8_t)(- (int8_t)v);
                    *reg8_table[rm] = res;
                    set_zf(cpu, res);
                    if (res & 0x80) cpu->flags |= FLAG_SF; else cpu->flags &= ~FLAG_SF;
                    // CF set if operand != 0
                    if (v != 0) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
                }
                else
                {
                    int16_t disp = 0;
                    uint32_t ea = 0;
                    if (mod == 0 && rm == 6) { ea = mem_read16(mem, addr + 2); instr_len += 2; }
                    else if (mod == 1) { disp = (int8_t)mem_read8(mem, addr + 2); instr_len += 1; }
                    else if (mod == 2) { disp = mem_read16(mem, addr + 2); instr_len += 2; }
                    ea = calc_ea(cpu, rm, disp);
                    uint8_t v = mem_read8(mem, seg + ea);
                    uint8_t res = (uint8_t)(- (int8_t)v);
                    mem_write8(mem, seg + ea, res);
                    set_zf(cpu, res);
                    if (res & 0x80) cpu->flags |= FLAG_SF; else cpu->flags &= ~FLAG_SF;
                    if (v != 0) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
                }
            }
            else
            {
                // NEG r/m16
                if (mod == 3)
                {
                    uint16_t v = *reg_table[rm];
                    uint16_t res = (uint16_t)(- (int16_t)v);
                    *reg_table[rm] = res;
                    set_zf(cpu, res);
                    set_sf(cpu, res);
                    if (v != 0) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
                }
                else
                {
                    int16_t disp = 0;
                    uint32_t ea = 0;
                    if (mod == 0 && rm == 6) { ea = mem_read16(mem, addr + 2); instr_len += 2; }
                    else if (mod == 1) { disp = (int8_t)mem_read8(mem, addr + 2); instr_len += 1; }
                    else if (mod == 2) { disp = mem_read16(mem, addr + 2); instr_len += 2; }
                    ea = calc_ea(cpu, rm, disp);
                    uint16_t v = mem_read16(mem, seg + ea);
                    uint16_t res = (uint16_t)(- (int16_t)v);
                    mem_write16(mem, seg + ea, res);
                    set_zf(cpu, res);
                    set_sf(cpu, res);
                    if (v != 0) cpu->flags |= FLAG_CF; else cpu->flags &= ~FLAG_CF;
                }
            }
            cpu->ip += instr_len;
            segment_override = 0;
            return 1;
        }
    }

    // Handle 0x80 / 0x82 (byte immediate) and 0x83 (sign-extended imm8 to 16-bit)
    if (opcode == 0x80 || opcode == 0x82 || opcode == 0x83)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint32_t ea = 0;

        if (mod == 3)
        { // register-direct
            if (opcode == 0x83)
            {
                int16_t imm = (int8_t)mem_read8(mem, addr + 2); // sign-extended
                instr_len += 1;
                uint16_t old = *reg_table[rm];
                uint16_t result = 0;
                switch (reg)
                {
                case 0: // ADD
                    *reg_table[rm] = add16(cpu, old, (uint16_t)imm);
                    break;
                case 1: // OR
                    result = old | (uint16_t)imm;
                    *reg_table[rm] = result;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 4: // AND
                    result = old & (uint16_t)imm;
                    *reg_table[rm] = result;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 5: // SUB
                    *reg_table[rm] = sub16(cpu, old, (uint16_t)imm);
                    break;
                case 6: // XOR
                    result = old ^ (uint16_t)imm;
                    *reg_table[rm] = result;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 7: // CMP
                    result = old - (uint16_t)imm;
                    set_zf(cpu, result);
                    set_sf(cpu, result);
                    set_cf_sub(cpu, old, (uint16_t)imm);
                    set_of_sub(cpu, old, (uint16_t)imm, result);
                    break;
                }
            }
            else
            {
                uint8_t imm8 = mem_read8(mem, addr + 2);
                instr_len += 1;
                uint8_t old8 = *reg8_table[rm];
                uint8_t res8 = 0;
                switch (reg)
                {
                case 0: // ADD
                {
                    uint16_t sum = (uint16_t)old8 + (uint16_t)imm8;
                    res8 = (uint8_t)sum;
                    *reg8_table[rm] = res8;
                    set_zf(cpu, res8);
                    /* SF for 8-bit */
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    if (sum > 0xFF)
                        cpu->flags |= FLAG_CF;
                    else
                        cpu->flags &= ~FLAG_CF;
                    /* OF for 8-bit signed overflow */
                    if (((old8 ^ res8) & (imm8 ^ res8) & 0x80) != 0)
                        cpu->flags |= FLAG_OF;
                    else
                        cpu->flags &= ~FLAG_OF;
                    /* AF */
                    if (((old8 & 0xF) + (imm8 & 0xF)) > 0xF)
                        cpu->flags |= 0x10;
                    else
                        cpu->flags &= ~0x10;
                }
                break;
                case 1: // OR
                    res8 = old8 | imm8;
                    *reg8_table[rm] = res8;
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 4: // AND
                    res8 = old8 & imm8;
                    *reg8_table[rm] = res8;
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 5: // SUB
                {
                    uint16_t result = (uint16_t)old8 - (uint16_t)imm8;
                    res8 = (uint8_t)result;
                    *reg8_table[rm] = res8;
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    if (old8 < imm8)
                        cpu->flags |= FLAG_CF;
                    else
                        cpu->flags &= ~FLAG_CF;
                    if (((old8 ^ imm8) & (old8 ^ res8) & 0x80) != 0)
                        cpu->flags |= FLAG_OF;
                    else
                        cpu->flags &= ~FLAG_OF;
                    if ((old8 & 0xF) < (imm8 & 0xF))
                        cpu->flags |= 0x10;
                    else
                        cpu->flags &= ~0x10;
                }
                break;
                case 6: // XOR
                    res8 = old8 ^ imm8;
                    *reg8_table[rm] = res8;
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 7: // CMP
                {
                    uint8_t result = old8 - imm8;
                    set_zf(cpu, result);
                    if (result & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, result);
                    if (old8 < imm8)
                        cpu->flags |= FLAG_CF;
                    else
                        cpu->flags &= ~FLAG_CF;
                    if (((old8 ^ imm8) & (old8 ^ result) & 0x80) != 0)
                        cpu->flags |= FLAG_OF;
                    else
                        cpu->flags &= ~FLAG_OF;
                }
                break;
                }
            }
        }
        else
        { // memory operand
            int16_t disp = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6))
                ea = calc_ea(cpu, rm, disp);

            if (opcode == 0x83)
            {
                int16_t imm = (int8_t)mem_read8(mem, addr + instr_len);
                instr_len += 1;
                uint16_t old = mem_read16(mem, ea);
                switch (reg)
                {
                case 0: // ADD
                {
                    uint16_t res = add16(cpu, old, (uint16_t)imm);
                    mem_write16(mem, ea, res);
                }
                break;
                case 1: // OR
                {
                    uint16_t res = old | (uint16_t)imm;
                    mem_write16(mem, ea, res);
                    set_zf(cpu, res);
                    set_sf(cpu, res);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                }
                break;
                case 4: // AND
                {
                    uint16_t res = old & (uint16_t)imm;
                    mem_write16(mem, ea, res);
                    set_zf(cpu, res);
                    set_sf(cpu, res);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                }
                break;
                case 5: // SUB
                {
                    uint16_t res = sub16(cpu, old, (uint16_t)imm);
                    mem_write16(mem, ea, res);
                }
                break;
                case 6: // XOR
                {
                    uint16_t res = old ^ (uint16_t)imm;
                    mem_write16(mem, ea, res);
                    set_zf(cpu, res);
                    set_sf(cpu, res);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                }
                break;
                case 7: // CMP
                {
                    uint16_t res = old - (uint16_t)imm;
                    set_zf(cpu, res);
                    set_sf(cpu, res);
                    set_cf_sub(cpu, old, (uint16_t)imm);
                    set_of_sub(cpu, old, (uint16_t)imm, res);
                }
                break;
                }
            }
            else
            {
                uint8_t imm8 = mem_read8(mem, addr + instr_len);
                instr_len += 1;
                uint8_t old8 = mem_read8(mem, (segment_override ? override_value : cpu->ds) << 4 + ea);
                /* perform byte operations similar to register case */
                uint8_t res8 = 0;
                switch (reg)
                {
                case 0: // ADD
                {
                    uint16_t sum = (uint16_t)old8 + (uint16_t)imm8;
                    res8 = (uint8_t)sum;
                    mem_write8(mem, ea, res8);
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    if (sum > 0xFF)
                        cpu->flags |= FLAG_CF;
                    else
                        cpu->flags &= ~FLAG_CF;
                    if (((old8 ^ res8) & (imm8 ^ res8) & 0x80) != 0)
                        cpu->flags |= FLAG_OF;
                    else
                        cpu->flags &= ~FLAG_OF;
                    if (((old8 & 0xF) + (imm8 & 0xF)) > 0xF)
                        cpu->flags |= 0x10;
                    else
                        cpu->flags &= ~0x10;
                }
                break;
                case 1: // OR
                    res8 = old8 | imm8;
                    mem_write8(mem, ea, res8);
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 4: // AND
                    res8 = old8 & imm8;
                    mem_write8(mem, ea, res8);
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 5: // SUB
                {
                    uint16_t result = (uint16_t)old8 - (uint16_t)imm8;
                    res8 = (uint8_t)result;
                    mem_write8(mem, ea, res8);
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    if (old8 < imm8)
                        cpu->flags |= FLAG_CF;
                    else
                        cpu->flags &= ~FLAG_CF;
                    if (((old8 ^ imm8) & (old8 ^ res8) & 0x80) != 0)
                        cpu->flags |= FLAG_OF;
                    else
                        cpu->flags &= ~FLAG_OF;
                    if ((old8 & 0xF) < (imm8 & 0xF))
                        cpu->flags |= 0x10;
                    else
                        cpu->flags &= ~0x10;
                }
                break;
                case 6: // XOR
                    res8 = old8 ^ imm8;
                    mem_write8(mem, ea, res8);
                    set_zf(cpu, res8);
                    if (res8 & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, res8);
                    cpu->flags &= ~(FLAG_CF | FLAG_OF);
                    break;
                case 7: // CMP
                {
                    uint8_t result = old8 - imm8;
                    set_zf(cpu, result);
                    if (result & 0x80)
                        cpu->flags |= FLAG_SF;
                    else
                        cpu->flags &= ~FLAG_SF;
                    set_pf(cpu, result);
                    if (old8 < imm8)
                        cpu->flags |= FLAG_CF;
                    else
                        cpu->flags &= ~FLAG_CF;
                    if (((old8 ^ imm8) & (old8 ^ result) & 0x80) != 0)
                        cpu->flags |= FLAG_OF;
                    else
                        cpu->flags &= ~FLAG_OF;
                }
                break;
                }
            }
            segment_override = 0;
        }
        cpu->ip += instr_len;
        return 1;
    }

    // --- MOV r/m16, imm16 (0xC7 /0) ---
    if (opcode == 0xC7)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint16_t imm = mem_read16(mem, addr + instr_len);
        instr_len += 2;
        if (mod == 3)
            *reg_table[rm] = imm;
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6))
                ea = calc_ea(cpu, rm, disp);
            mem_write16(mem, ea, imm);
        }
        cpu->ip += instr_len;
        return 1;
    }

    // --- MOV r/m8, imm8 (0xC6 /0) ---
    if (opcode == 0xC6)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint8_t imm8 = mem_read8(mem, addr + instr_len);
        instr_len += 1;
        if (mod == 3)
        {
            *reg8_table[rm] = imm8;
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            if (!(mod == 0 && rm == 6))
                ea = calc_ea(cpu, rm, disp);
            mem_write8(mem, ea, imm8);
        }
        cpu->ip += instr_len;
        return 1;
    }

    // --- I/O instructions (IN, OUT) ---
    if (opcode == 0xED)
    { // IN AX, DX
        char buf[64];
        snprintf(buf, sizeof(buf), "IN AX, DX (port %u) not implemented\n", cpu->dx);
        emu_puts(buf);
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xE6)
    { // OUT imm8, AL
        uint8_t port = mem_read8(mem, addr + 1);
        char buf[64];
        snprintf(buf, sizeof(buf), "OUT AL, port %u (value %02X) not implemented\n", port, ((uint8_t *)&cpu->ax)[0]);
        emu_puts(buf);
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0xE7)
    { // OUT imm8, AX
        uint8_t port = mem_read8(mem, addr + 1);
        char buf[64];
        snprintf(buf, sizeof(buf), "OUT AX, port %u (value %04X) not implemented\n", port, cpu->ax);
        emu_puts(buf);
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0xEE)
    { // OUT DX, AL
        char buf[64];
        snprintf(buf, sizeof(buf), "OUT DX, AL (port %u, value %02X) not implemented\n", cpu->dx, ((uint8_t *)&cpu->ax)[0]);
        emu_puts(buf);
        cpu->ip += 1;
        return 1;
    }
    if (opcode == 0xEF)
    { // OUT DX, AX
        char buf[64];
        snprintf(buf, sizeof(buf), "OUT DX, AX (port %u, value %04X) not implemented\n", cpu->dx, cpu->ax);
        emu_puts(buf);
        cpu->ip += 1;
        return 1;
    }
    // duplicate IRET stub removed (handled earlier)

    // MUL/IMUL/DIV/IDIV r/m8 (0xF6 /4-/7), r/m16 (0xF7 /4-/7)
    if (opcode == 0xF6 || opcode == 0xF7)
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        int is16 = (opcode == 0xF7);
        uint16_t val16 = 0;
        uint8_t val8 = 0;
        if (mod == 3)
        {
            if (is16)
                val16 = *reg_table[rm];
            else
                val8 = *reg8_table[rm];
        }
        else
        {
            int16_t disp = 0;
            uint32_t ea = 0;
            if (mod == 0 && rm == 6)
            {
                ea = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            else if (mod == 1)
            {
                disp = (int8_t)mem_read8(mem, addr + 2);
                instr_len += 1;
            }
            else if (mod == 2)
            {
                disp = mem_read16(mem, addr + 2);
                instr_len += 2;
            }
            ea = calc_ea(cpu, rm, disp);
            if (is16)
                val16 = mem_read16(mem, seg + ea);
            else
                val8 = mem_read8(mem, seg + ea);
        }
        if (!is16)
        {
            if (reg == 4)
            { // MUL AL, r/m8
                uint16_t res = ((uint8_t *)&cpu->ax)[0] * val8;
                cpu->ax = res;
            }
            else if (reg == 5)
            { // IMUL AL, r/m8
                int8_t al = ((uint8_t *)&cpu->ax)[0];
                int8_t v = (int8_t)val8;
                int16_t res = al * v;
                cpu->ax = (uint16_t)res;
            }
            else if (reg == 6)
            { // DIV AL, r/m8
                uint8_t al = ((uint8_t *)&cpu->ax)[0];
                uint8_t ah = ((uint8_t *)&cpu->ax)[1];
                uint16_t dividend = ((uint16_t)ah << 8) | al;
                if (val8 == 0)
                {
                    emu_puts("Divide by zero!\n");
                    emu_output_flush();
                    return 0;
                }
                ((uint8_t *)&cpu->ax)[0] = dividend / val8;
                ((uint8_t *)&cpu->ax)[1] = dividend % val8;
            }
            else if (reg == 7)
            { // IDIV AL, r/m8
                int8_t al = ((uint8_t *)&cpu->ax)[0];
                int8_t ah = ((uint8_t *)&cpu->ax)[1];
                int16_t dividend = ((int16_t)ah << 8) | (uint8_t)al;
                if (val8 == 0)
                {
                    emu_puts("Divide by zero!\n");
                    emu_output_flush();
                    return 0;
                }
                ((uint8_t *)&cpu->ax)[0] = dividend / (int8_t)val8;
                ((uint8_t *)&cpu->ax)[1] = dividend % (int8_t)val8;
            }
        }
        else
        {
            if (reg == 4)
            { // MUL AX, r/m16
                uint32_t res = cpu->ax * val16;
                cpu->dx = (res >> 16) & 0xFFFF;
                cpu->ax = res & 0xFFFF;
            }
            else if (reg == 5)
            { // IMUL AX, r/m16
                int16_t ax = cpu->ax;
                int16_t v = (int16_t)val16;
                int32_t res = (int32_t)ax * v;
                cpu->dx = (res >> 16) & 0xFFFF;
                cpu->ax = res & 0xFFFF;
            }
            else if (reg == 6)
            { // DIV AX, r/m16
                uint32_t dividend = ((uint32_t)cpu->dx << 16) | cpu->ax;
                if (val16 == 0)
                {
                    emu_puts("Divide by zero!\n");
                    emu_output_flush();
                    return 0;
                }
                cpu->ax = dividend / val16;
                cpu->dx = dividend % val16;
            }
            else if (reg == 7)
            { // IDIV AX, r/m16
                int32_t dividend = ((int32_t)cpu->dx << 16) | cpu->ax;
                if (val16 == 0)
                {
                    emu_puts("Divide by zero!\n");
                    emu_output_flush();
                    return 0;
                }
                cpu->ax = dividend / (int16_t)val16;
                cpu->dx = dividend % (int16_t)val16;
            }
        }
        cpu->ip += instr_len;
        segment_override = 0;
        return 1;
    }

    // DAA (0x27) - Decimal Adjust AL after Addition
    if (opcode == 0x27)
    {
        uint8_t *al = &((uint8_t *)&cpu->ax)[0];
        if (((*al & 0x0F) > 9) || (cpu->flags & 0x10))
        {
            *al += 6;
            cpu->flags |= 0x10; // AF
        }
        else
        {
            cpu->flags &= ~0x10;
        }
        if ((*al > 0x9F) || (cpu->flags & FLAG_CF))
        {
            *al += 0x60;
            cpu->flags |= FLAG_CF;
        }
        else
        {
            cpu->flags &= ~FLAG_CF;
        }
        cpu->ip += 1;
        return 1;
    }
    // DAS (0x2F) - Decimal Adjust AL after Subtraction
    if (opcode == 0x2F)
    {
        uint8_t *al = &((uint8_t *)&cpu->ax)[0];
        if (((*al & 0x0F) > 9) || (cpu->flags & 0x10))
        {
            *al -= 6;
            cpu->flags |= 0x10;
        }
        else
        {
            cpu->flags &= ~0x10;
        }
        if ((*al > 0x9F) || (cpu->flags & FLAG_CF))
        {
            *al -= 0x60;
            cpu->flags |= FLAG_CF;
        }
        else
        {
            cpu->flags &= ~FLAG_CF;
        }
        cpu->ip += 1;
        return 1;
    }
    // AAA (0x37) - ASCII Adjust after Addition
    if (opcode == 0x37)
    {
        uint8_t *al = &((uint8_t *)&cpu->ax)[0];
        uint8_t *ah = &((uint8_t *)&cpu->ax)[1];
        if (((*al & 0x0F) > 9) || (cpu->flags & 0x10))
        {
            *al += 6;
            (*ah)++;
            cpu->flags |= 0x10;
        }
        else
        {
            cpu->flags &= ~0x10;
        }
        *al &= 0x0F;
        cpu->ip += 1;
        return 1;
    }
    // AAS (0x3F) - ASCII Adjust after Subtraction
    if (opcode == 0x3F)
    {
        uint8_t *al = &((uint8_t *)&cpu->ax)[0];
        uint8_t *ah = &((uint8_t *)&cpu->ax)[1];
        if (((*al & 0x0F) > 9) || (cpu->flags & 0x10))
        {
            *al -= 6;
            (*ah)--;
            cpu->flags |= 0x10;
        }
        else
        {
            cpu->flags &= ~0x10;
        }
        *al &= 0x0F;
        cpu->ip += 1;
        return 1;
    }
    // SAL/SAR r/m8, 1 (0xD0 /4-/7), r/m8, CL (0xD2 /4-/7), r/m16, 1 (0xD1 /4-/7), r/m16, CL (0xD3 /4-/7)
    if ((opcode == 0xD0 || opcode == 0xD2 || opcode == 0xD1 || opcode == 0xD3))
    {
        uint8_t modrm = mem_read8(mem, addr + 1);
        uint8_t mod, reg, rm;
        decode_modrm(modrm, &mod, &reg, &rm);
        int instr_len = 2;
        int is16 = (opcode == 0xD1 || opcode == 0xD3);
        int count = (opcode == 0xD0 || opcode == 0xD1) ? 1 : ((cpu->cx) & 0xFF);
        uint32_t seg = (segment_override ? override_value : cpu->ds) << 4;
        if (reg == 4 || reg == 6)
        { // SAL/SHL or SAR
            if (mod == 3)
            {
                if (is16)
                {
                    uint16_t *dst = reg_table[rm];
                    while (count--)
                    {
                        if (reg == 4)
                            *dst <<= 1; // SAL/SHL
                        else if (reg == 6)
                            *dst = (int16_t)(*dst) >> 1; // SAR
                    }
                }
                else
                {
                    uint8_t *dst = reg8_table[rm];
                    while (count--)
                    {
                        if (reg == 4)
                            *dst <<= 1;
                        else if (reg == 6)
                            *dst = (int8_t)(*dst) >> 1;
                    }
                }
            }
            else
            {
                int16_t disp = 0;
                uint32_t ea = 0;
                if (mod == 0 && rm == 6)
                {
                    ea = mem_read16(mem, addr + 2);
                    instr_len += 2;
                }
                else if (mod == 1)
                {
                    disp = (int8_t)mem_read8(mem, addr + 2);
                    instr_len += 1;
                }
                else if (mod == 2)
                {
                    disp = mem_read16(mem, addr + 2);
                    instr_len += 2;
                }
                ea = calc_ea(cpu, rm, disp);
                if (is16)
                {
                    uint16_t val = mem_read16(mem, seg + ea);
                    while (count--)
                    {
                        if (reg == 4)
                            val <<= 1;
                        else if (reg == 6)
                            val = (int16_t)val >> 1;
                    }
                    mem_write16(mem, seg + ea, val);
                }
                else
                {
                    uint8_t val = mem_read8(mem, seg + ea);
                    while (count--)
                    {
                        if (reg == 4)
                            val <<= 1;
                        else if (reg == 6)
                            val = (int8_t)val >> 1;
                    }
                    mem_write8(mem, seg + ea, val);
                }
            }
            cpu->ip += instr_len;
            segment_override = 0;
            return 1;
        }
    }

    // --- Unknown/unsupported opcode handler ---
    // CMP AL, imm8 (0x3C) and CMP AX, imm16 (0x3D)
    if (opcode == 0x3C)
    {
        uint8_t imm8 = mem_read8(mem, addr + 1);
        uint8_t al = ((uint8_t *)&cpu->ax)[0];
        uint8_t res = al - imm8;
        set_zf(cpu, res);
        /* set SF for 8-bit */
        if (res & 0x80)
            cpu->flags |= FLAG_SF;
        else
            cpu->flags &= ~FLAG_SF;
        set_pf(cpu, res);
        if (al < imm8)
            cpu->flags |= FLAG_CF;
        else
            cpu->flags &= ~FLAG_CF;
        if (((al ^ imm8) & (al ^ res) & 0x80) != 0)
            cpu->flags |= FLAG_OF;
        else
            cpu->flags &= ~FLAG_OF;
        /* AF */
        if ((al & 0xF) < (imm8 & 0xF))
            cpu->flags |= 0x10;
        else
            cpu->flags &= ~0x10;
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x3D)
    {
        uint16_t imm16 = mem_read16(mem, addr + 1);
        uint16_t ax = cpu->ax;
        uint16_t res = ax - imm16;
        set_zf(cpu, res);
        set_sf(cpu, res);
        set_pf(cpu, res);
        set_cf_sub(cpu, ax, imm16);
        set_of_sub(cpu, ax, imm16, res);
        /* AF */
        if ((ax & 0xF) < (imm16 & 0xF))
            cpu->flags |= 0x10;
        else
            cpu->flags &= ~0x10;
        cpu->ip += 3;
        return 1;
    }

    // ADD AL, imm8 (0x04) and ADD AX, imm16 (0x05)
    if (opcode == 0x04)
    {
        uint8_t imm8 = mem_read8(mem, addr + 1);
        uint8_t al = ((uint8_t *)&cpu->ax)[0];
        uint16_t sum = (uint16_t)al + (uint16_t)imm8;
        ((uint8_t *)&cpu->ax)[0] = (uint8_t)sum;
        set_zf(cpu, (uint8_t)sum);
        if ((uint8_t)sum & 0x80)
            cpu->flags |= FLAG_SF;
        else
            cpu->flags &= ~FLAG_SF;
        set_pf(cpu, (uint8_t)sum);
        if (sum > 0xFF)
            cpu->flags |= FLAG_CF;
        else
            cpu->flags &= ~FLAG_CF;
        if (((al ^ (uint8_t)sum) & (imm8 ^ (uint8_t)sum) & 0x80) != 0)
            cpu->flags |= FLAG_OF;
        else
            cpu->flags &= ~FLAG_OF;
        if (((al & 0xF) + (imm8 & 0xF)) > 0xF)
            cpu->flags |= 0x10;
        else
            cpu->flags &= ~0x10;
        cpu->ip += 2;
        return 1;
    }
    if (opcode == 0x05)
    {
        uint16_t imm16 = mem_read16(mem, addr + 1);
        uint16_t old = cpu->ax;
        uint32_t sum = (uint32_t)old + (uint32_t)imm16;
        cpu->ax = (uint16_t)sum;
        set_zf(cpu, cpu->ax);
        set_sf(cpu, cpu->ax);
        set_pf(cpu, cpu->ax);
        set_cf_add(cpu, old, imm16);
        set_of_add(cpu, old, imm16, (uint16_t)sum);
        set_af_add(cpu, old, imm16);
        cpu->ip += 3;
        return 1;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "Unknown or unsupported opcode: %02X at CS:IP=%04X:%04X\n", opcode, cpu->cs, cpu->ip);
    emu_puts(msg);
    emu_output_flush();
    return 0;
}
