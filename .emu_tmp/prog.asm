; hello8086_nasm.asm
; Conventional structure with NASM directives
; This builds a .COM program

org 100h                 ; .COM programs load at offset 100h

section .text
start:
    ; Initialize DS = CS (conventional setup for COM files)
    mov ax, cs
    mov ds, ax

    ; Print the string
    mov dx, msg
    mov ah, 9
    int 21h

    ; Exit to DOS
    mov ah, 4Ch
    int 21h

section .data
msg db "Hello, World from NASM with directives!$"
