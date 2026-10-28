; NASM real: macro, constante, .bss e entrada _start apos uma funcao.
bits 32
global _start
%define VALOR 7
%macro dobro 1
    add %1, %1
%endmacro
section .text
funcao:
    dobro eax
    ret
_start:
    mov eax, VALOR
    call funcao
    mov [resultado], eax
    hlt
section .bss
resultado resd 1
