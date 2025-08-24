# Intel 8086 Emulator

**Author:** Jeswin Thampichan Joseph  
**GitHub:** [github.com/jeswin1811/Intel-8086-Emulator](https://github.com/jeswin1811/Intel-8086-Emulator)

---

## Overview

This project is a minimal but functional Intel 8086 emulator with:

- **Emulator core:** C (CPU + Memory)
- **Memory helpers:** `memory.c` / `memory.h`
- **Server:** TCP server (`emu_server.exe`) for running emulation remotely
- **GUI frontend:** Python PyQt5 (`emu_gui.py`) for editing, assembling, and running programs
- **Installer:** Windows installer generated with Inno Setup (`Emu8086Installer.exe`)

### Execution Model

- `.COM` programs are loaded at physical address `0x100` (CS:IP = 0000:0100).
- The CPU loop (`cpu_step(cpu, mem)`) executes instructions until exit or error.
- The server listens on port `5555`, receives a length-prefixed payload, runs emulation, and returns the output.

---

## Memory Subsystem

- **Memory size:** 1 MiB (`MEMORY_SIZE = 0x100000`)
- **Functions:**
  - `mem_read8` / `mem_write8` — read/write a byte with bounds check
  - `mem_read16` / `mem_write16` — little-endian, via two 8-bit operations
- Out-of-range reads return `0xFF`.

---

## CPU Core

- Registers: `AX, BX, CX, DX, SI, DI, BP, SP, IP, Flags, CS, DS, ES, SS`
- Helpers:
  - ModR/M decode, EA calculation (addressing modes like BX+SI, BP+DI, etc.)
  - Flag helpers: ZF, SF, PF, AF, CF, OF
  - Arithmetic helpers (`add16`, `sub16`) update relevant flags
- Output buffer: `emu_output[]` with helpers `emu_putchar`, `emu_puts`, `emu_output_flush` for INT 21h handling

---

## Interrupts & DOS Support

- **INT 21h implemented:**  
  - AH=02: write DL (char)  
  - AH=09: write `$`-terminated string  
  - AH=01: read char (stub: returns 'A')  
  - AH=4C: exit  
  - AH=3D/3E/3F/40/48/49/4A: stub messages `[DOS] ... not implemented`  
- **INT 10h / 16h:** stub messages  
- **File system:** none (text output only)

---

## Instruction Coverage

### Implemented

- MOV (immediate, register/memory)
- ADD/SUB, AND/OR/XOR, CMP
- PUSH/POP, CALL, RET, JMP, conditional jumps
- String ops: MOVSB, MOVSW, LODSB, STOSB, etc.
- Shifts/rotates: D0–D3, C0/C1
- Misc: NOP, HLT, WAIT, CLI/STI, DAA/DAS/AAA/AAS
- Basic OUT/IN stubs (no real port I/O)

### Partial / Missing

- Two-byte opcodes (`0x0F ...`)
- LES/LDS, ENTER/LEAVE, INT3/INTO
- Precise AF/OF/CF semantics for some operations
- Far CALL/JMP/RET edge cases
- Full DOS/BIOS interrupts

---

## Server Protocol

- TCP port: `5555`
- Client → server: 4-byte little-endian payload length + payload bytes
- Server → client: 4-byte little-endian output length + output bytes
- Server logs `emu_out_pos` and debug info to `stderr`

---

## GUI Features

- ASM editor with Assemble & Run
- Open `.asm` / `.com` and execute
- Start/Stop server automatically
- Splash screen with `startup.png`
- Frameless window with custom title bar
- Sanitizes non-printable output bytes to `.`

---

## Installer

- **Installer file:** `Emu8086Installer.exe`
- Installs emulator, server, and GUI to `{Program Files}\Intel 8086 Emulator`
- Creates desktop and Start Menu shortcuts
- Includes `emu_gui.exe`, `emu_server.exe`, and necessary files

**Download & install the release from:** [GitHub Releases](https://github.com/jeswin1811/Intel-8086-Emulator/releases)