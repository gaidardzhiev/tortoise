.text
.globl _start
_start:
LOAD32 R1, msg
CALL32 puts
LOAD32 R1, counter
LDW R0, [R1]
CMP32 R0, 42
BNE bad
LEAPC R1, msg2
CALLREL puts
LOAD32 R7, 0xF0007000
LOAD32 R0, 0
STW [R7], R0
bad:
LOAD32 R7, 0xF0007000
LOAD32 R0, 1
STW [R7], R0
.data
msg:
.asciiz "hello from a\n"
.bss
buf:
.space 64
