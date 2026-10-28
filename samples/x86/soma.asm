; ChipLab x86-32 / NASM: EAX = 15 e resultado = 15.
bits 32
global _start

section .text
_start:
    xor eax, eax
    mov ecx, 5
.somar:
    add eax, ecx
    dec ecx
    jnz .somar
    mov [resultado], eax
    hlt

section .data
resultado dd 0
