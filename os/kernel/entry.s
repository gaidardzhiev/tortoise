; Copyright (C) 2026 Ivan Gaydardzhiev
; Licensed under the GPL-3.0-only

.text
.globl _start
_start:
CLI
LOAD32 R1, bootstack_top
LDW R1, [R1]
SETSP R1
PUSH32 R0
CALL32 kmain
.halt:
HALT
JMPREL .halt
.globl alltraps
alltraps:
PUSH32 R0
PUSH32 R1
PUSH32 R2
PUSH32 R3
PUSH32 R4
PUSH32 R5
PUSH32 R6
PUSH32 R7
MOVSP R0
PUSH32 R0
CALL32 trap
ADDSP 4
.globl trapret
trapret:
POP32 R7
POP32 R6
POP32 R5
POP32 R4
POP32 R3
POP32 R2
POP32 R1
POP32 R0
IRET
.globl forkret
forkret:
JMP32 trapret
.globl first_start
first_start:
CALL32 init_entry
JMP32 trapret
.globl swtch
swtch:
LDW R0, [SP + 4]
LDW R1, [SP + 8]
PUSH32 R4
PUSH32 R5
PUSH32 R6
PUSH32 R7
MOVSP R2
STW [R0], R2
SETSP R1
POP32 R7
POP32 R6
POP32 R5
POP32 R4
RET32
.globl rd
rd:
LDW R0, [SP + 4]
LDW R0, [R0]
RET32
.globl wr
wr:
LDW R0, [SP + 4]
LDW R1, [SP + 8]
STW [R0], R1
RET32
.globl get_cr
get_cr:
LDW R0, [SP + 4]
CMP32 R0, 2
BEQ .c2
CMP32 R0, 7
BEQ .c7
CMP32 R0, 3
BEQ .c3
MFCR R0, CR0
RET32
.c2:
MFCR R0, CR2
RET32
.c7:
MFCR R0, CR7
RET32
.c3:
MFCR R0, CR3
RET32
.globl set_cr2
set_cr2:
LDW R0, [SP + 4]
MTCR CR2, R0
RET32
.globl set_ptbase
set_ptbase:
LDW R0, [SP + 4]
MTCR CR3, R0
RET32
.globl set_ksp
set_ksp:
LDW R0, [SP + 4]
MTCR CR4, R0
RET32
.globl set_evt
set_evt:
LDW R0, [SP + 4]
MTCR CR1, R0
RET32
.globl set_ctrl
set_ctrl:
LDW R0, [SP + 4]
MTCR CR0, R0
RET32
.globl tlbflush
tlbflush:
TLBFLUSH
RET32
.globl invlpg
invlpg:
LDW R0, [SP + 4]
INVLPG R0
RET32
.globl cpu_idle
cpu_idle:
WFI
STI
NOP
CLI
RET32
