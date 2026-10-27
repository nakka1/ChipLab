; Em 8 bits, 255 + 1 = 0 e 0 - 1 = 255.
; Em 16 ou 32 bits, 255 + 1 = 256.
MOV R0, 255
MOV R1, 1
ADD R2, R0, R1
SUB R3, R2, R1
STORE R2, 0
STORE R3, 1
HALT
