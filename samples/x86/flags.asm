; Observe AL/AH/AX como partes de EAX e flags CF/ZF/SF/OF.
bits 32
global _start
section .text
_start:
    mov al, 255
    add al, 1          ; AL=0, CF=1, ZF=1
    mov ecx, 0
    adc ecx, 0         ; ECX=1, usando o carry anterior
    mov dl, 127
    add dl, 1          ; DL=128, OF=1, SF=1
    jno falha
    mov ebx, 123
    hlt
falha:
    mov ebx, 999
    hlt
