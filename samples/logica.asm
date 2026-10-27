; Operações da ULA com máscaras binárias, entradas em hexadecimal.
MOV R0, 0xF0
MOV R1, 0xCC
AND R2, R0, R1
STORE R2, 0
OR R2, R0, R1
STORE R2, 1
XOR R2, R0, R1
STORE R2, 2
HALT
