; Copyright (C) 2026 Ivan Gaydardzhiev
; Licensed under the GPL-3.0-only

.equ UART, 0xF0002000
.equ DISK, 0xF0003000
.equ SYSC, 0xF0007000
.equ LOADADDR, 0x00010000
.equ LINEBUF, 0x00000700
.equ LINEMAX, 120
.org 0xFFFF0000
reset:
CLI
LOAD32 R1, SYSC
LDW R0, [R1 + 4]
SETSP R0
LEAPC R1, banner
CALL32 puts
CALL32 tryboot
noboot:
LEAPC R1, nobootmsg
CALL32 puts
monitor:
LEAPC R1, prompt
CALL32 puts
CALL32 readline
LOAD32 R1, LINEBUF
CALL32 skipsp
LDB R5, [R1]
CMP32 R5, 0
BEQ monitor
ADD32 R1, 1
CMP32 R5, 'm'
BEQ cmd_m
CMP32 R5, 'w'
BEQ cmd_w
CMP32 R5, 'g'
BEQ cmd_g
CMP32 R5, 'b'
BEQ cmd_b
CMP32 R5, 'r'
BEQ cmd_r
bad:
LEAPC R1, qmsg
CALL32 puts
JMP32 monitor
cmd_m:
CALL32 gethex
CMP32 R2, 0
BEQ bad
MOV R3, R0
CALL32 gethex
LOAD32 R4, 0x40
CMP32 R2, 0
BEQ .go
MOV R4, R0
.go:
LOAD32 R6, 0
.loop:
CMP32 R6, R4
BHS .end
TESTI R6, 15
BNE .byte
CMP32 R6, 0
BEQ .hdr
LOAD32 R0, 10
CALL32 putc
.hdr:
MOV R0, R3
ADD32 R0, R6
CALL32 puthex32
LOAD32 R0, ':'
CALL32 putc
.byte:
LOAD32 R0, ' '
CALL32 putc
MOV R1, R3
ADD32 R1, R6
LDB R0, [R1]
CALL32 puthex8
ADD32 R6, 1
JMP32 .loop
.end:
CMP32 R4, 0
BEQ monitor
LOAD32 R0, 10
CALL32 putc
JMP32 monitor
cmd_w:
CALL32 gethex
CMP32 R2, 0
BEQ bad
MOV R3, R0
.loop:
CALL32 gethex
CMP32 R2, 0
BEQ monitor
STB [R3], R0
ADD32 R3, 1
JMP32 .loop
cmd_g:
CALL32 gethex
CMP32 R2, 0
BEQ bad
CALLR R0
JMP32 monitor
cmd_b:
CALL32 tryboot
JMP32 noboot
cmd_r:
LOAD32 R1, SYSC
LDW R0, [R1 + 4]
CALL32 puthex32
LOAD32 R0, 10
CALL32 putc
JMP32 monitor
tryboot:
LOAD32 R7, DISK
LDW R0, [R7 + 0x14]
CMP32 R0, 0
BEQ .fail
LOAD32 R0, 0
STW [R7], R0
STW [R7 + 0x10], R0
LOAD32 R0, 1
STW [R7 + 4], R0
LOAD32 R0, LOADADDR
STW [R7 + 8], R0
LOAD32 R0, 1
STW [R7 + 0x0C], R0
.wait:
LDW R0, [R7 + 0x10]
TESTI R0, 1
BNE .wait
TESTI R0, 2
BNE .fail
LOAD32 R1, LOADADDR
LDB R0, [R1 + 510]
CMP32 R0, 0x55
BNE .fail
LDB R0, [R1 + 511]
CMP32 R0, 0xAA
BNE .fail
LOAD32 R1, SYSC
LDW R0, [R1 + 4]
LOAD32 R1, LOADADDR
JMPR R1
.fail:
RET32
putc:
PUSH32 R7
LOAD32 R7, UART
STW [R7], R0
POP32 R7
RET32
getc:
PUSH32 R7
LOAD32 R7, UART
.wait:
LDW R0, [R7 + 4]
TESTI R0, 1
BEQ .wait
LDW R0, [R7]
POP32 R7
RET32
puts:
LDB R0, [R1]
CMP32 R0, 0
BEQ .done
CALL32 putc
ADD32 R1, 1
JMP32 puts
.done:
RET32
hexdig:
CMP32 R0, 10
BHS .alpha
ADD32 R0, '0'
JMP32 putc
.alpha:
ADD32 R0, 'A' - 10
JMP32 putc
puthex8:
PUSH32 R2
MOV R2, R0
SHR32 R0, 4
AND32 R0, 15
CALL32 hexdig
MOV R0, R2
AND32 R0, 15
CALL32 hexdig
POP32 R2
RET32
puthex32:
PUSH32 R2
PUSH32 R1
MOV R2, R0
LOAD32 R1, 8
.loop:
ROL R2, 4
MOV R0, R2
AND32 R0, 15
CALL32 hexdig
SUB32 R1, 1
BNE .loop
POP32 R1
POP32 R2
RET32
skipsp:
LDB R7, [R1]
CMP32 R7, ' '
BEQ .next
CMP32 R7, 9
BNE .done
.next:
ADD32 R1, 1
JMP32 skipsp
.done:
RET32
gethex:
CALL32 skipsp
LOAD32 R0, 0
LOAD32 R2, 0
.loop:
LDB R7, [R1]
CMP32 R7, '0'
BLO .end
CMP32 R7, '9'
BLS .dig
OR32 R7, 0x20
CMP32 R7, 'a'
BLO .end
CMP32 R7, 'f'
BHI .end
SUB32 R7, 'a' - 10
JMP32 .add
.dig:
SUB32 R7, '0'
.add:
SHL32 R0, 4
OR32 R0, R7
ADD32 R2, 1
ADD32 R1, 1
JMP32 .loop
.end:
RET32
readline:
LOAD32 R6, LINEBUF
LOAD32 R5, 0
.loop:
CALL32 getc
CMP32 R0, 13
BEQ .end
CMP32 R0, 10
BEQ .end
CMP32 R0, 8
BEQ .bs
CMP32 R0, 127
BEQ .bs
CMP32 R5, LINEMAX
BHS .loop
MOV R1, R6
ADD32 R1, R5
STB [R1], R0
ADD32 R5, 1
CALL32 putc
JMP32 .loop
.bs:
CMP32 R5, 0
BEQ .loop
SUB32 R5, 1
LEAPC R1, bsmsg
CALL32 puts
JMP32 .loop
.end:
MOV R1, R6
ADD32 R1, R5
LOAD32 R0, 0
STB [R1], R0
LOAD32 R0, 10
CALL32 putc
RET32
banner:
.asciiz "TORTOISE ROM\n"
nobootmsg:
.asciiz "no boot disk\n"
prompt:
.asciiz "> "
qmsg:
.asciiz "?\n"
bsmsg:
.asciiz "\b \b"
