; Soma os inteiros de 1 a 5 e grava o resultado (15) na memória.
MOV R0, 5
MOV R1, 0
loop: ADD R1, R1, R0
ADDI R0, R0, -1
JNZ R0, loop
STORE R1, 0
LOAD R2, 0
HALT
