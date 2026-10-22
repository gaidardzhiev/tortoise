; Copyright (C) 2026 Ivan Gaydardzhiev
; Licensed under the GPL-3.0-only

.equ EVT, 0x1000
.equ SYS, 0xF0007000
.equ PIC, 0xF0000000
.equ TIMER, 0xF0001000
.macro setvec n, h
LOAD32 R0, \h
LOAD32 R7, EVT
STW [R7 + 4*\n], R0
.endm
.macro putc c
LOAD R2, \c
OUT R2, 0xFF00
.endm
.macro poweroff code
LOAD32 R6, SYS
LOAD32 R0, \code
STW [R6], R0
HALT
.endm
