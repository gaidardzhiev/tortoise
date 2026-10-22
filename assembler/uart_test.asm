; Copyright (C) 2026 Ivan Gaydardzhiev
; Licensed under the GPL-3.0-only

.include "ext_inc.asm"
.equ UART, 0xF0002000
CLI
LOAD32 R0, EVT
MTCR CR1, R0
setvec 33, h_rx
LOAD32 R0, 1
MTCR CR0, R0
LOAD32 R6, PIC
LOAD32 R0, 2
STW [R6 + 4], R0
LOAD32 R5, 0
LOAD32 R4, 0
LOAD32 R7, UART
LDW R0, [R7 + 4]
TESTI R0, 2
BEQ fail
TESTI R0, 1
BEQ fail
LOAD32 R0, 1
STW [R7 + 8], R0
STI
loop:
CMP32 R4, 0
BNE got
WFI
JMP32 loop
got:
CLI
CMP32 R5, 17
BNE fail
LDW R0, [R7 + 4]
TESTI R0, 1
BNE fail
LDW R0, [R7]
CMP32 R0, 0
BNE fail
LOAD32 R0, 2
STW [R7 + 8], R0
LOAD32 R0, 4
STW [R6 + 8], R0
LOAD32 R0, 'U'
STW [R7], R0
LDW R0, [R6]
TESTI R0, 4
BEQ fail
poweroff 0
fail:
LOAD32 R7, UART
LOAD32 R0, '!'
STW [R7], R0
poweroff 1
h_rx:
LDW R0, [R7 + 4]
TESTI R0, 1
BEQ .done
LDW R0, [R7]
STW [R7], R0
ADD32 R5, 1
CMP32 R0, 10
BNE h_rx
LOAD32 R4, 1
.done:
LOAD32 R0, 2
STW [R6 + 8], R0
IRET
