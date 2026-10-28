; PUSH, CALL, acesso relativo a ESP e RET: EAX = 12.
bits 32
global _start
section .text
_start:
    push 5
    push 7
    call somar
    add esp, 8
    hlt
somar:
    mov eax, [esp + 4]
    add eax, [esp + 8]
    ret
