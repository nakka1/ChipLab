; Memoria byte a byte, enderecamento e little endian.
bits 32
global _start
section .text
_start:
    lea esi, [dados]
    movzx eax, byte [esi + 1]
    mov [esi + 2], al
    mov edx, 0x12345678
    mov [saida], edx    ; Bytes na memoria: 78 56 34 12
    hlt
section .data
dados db 20, 10, 30
saida dd 0
