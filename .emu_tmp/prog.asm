; hello8086.asm
; NASM syntax, COM format program

org 0x100                ; .COM programs always start at 100h

section .data
msg db "Hello, World from 8086!$", 0

section .text
start:
    mov dx, msg          ; Load offset of string into DX
    mov ah, 9            ; DOS print string function
    int 21h

    mov ah, 0x4C         ; DOS terminate program
    int 21h
