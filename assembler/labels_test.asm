LOAD R0, 5
LOAD R1, 0
loop_start:
ADD R1, 2
SUB R0, 1
JNZ loop_start
CMP R1, 10
JNZ label_fail
CALL print_success
HALT
print_success:
LOAD R2, 0x004C
OUT R2, 0xFF00
RET
label_fail:
LOAD R2, 0x0046
OUT R2, 0xFF00
HALT
