; Saida virtual Linux int 0x80: sys_write e sys_exit.
bits 32
global _start

section .data
mensagem db 'Ola do ChipLab!', 10
tamanho equ $ - mensagem

section .text
_start:
    mov eax, 4
    mov ebx, 1
    mov ecx, mensagem
    mov edx, tamanho
    int 0x80
    mov eax, 1
    xor ebx, ebx
    int 0x80
