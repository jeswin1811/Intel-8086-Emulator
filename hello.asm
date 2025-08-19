org 100h
mov ah, 2        ; DOS print char function
mov dl, 'A'      ; Character to print
int 21h          ; Call DOS interrupt to print character in DL
mov ax, 4C00h    ; DOS terminate program
int 21h