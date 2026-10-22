; Copyright (C) 2026 Ivan Gaydardzhiev
; Licensed under the GPL-3.0-only

.text
.globl _start
_start:
LOAD32 R0, environ
STW [R0], R3
PUSH32 R2
PUSH32 R1
CALL32 main
ADDSP 8
PUSH32 R0
CALL32 exit
.hang:
JMPREL .hang
.globl __syscall
__syscall:
PUSH32 R7
MOVSP R7
PUSH32 R4
PUSH32 R5
LDW R0, [R7 + 8]
LDW R1, [R7 + 12]
LDW R2, [R7 + 16]
LDW R3, [R7 + 20]
LDW R4, [R7 + 24]
LDW R5, [R7 + 28]
SYSCALL
POP32 R5
POP32 R4
POP32 R7
RET32
.globl __sigtramp
__sigtramp:
LOAD32 R0, 47
SYSCALL
.hang2:
JMPREL .hang2

