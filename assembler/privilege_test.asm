LOAD R0, gp_handler
STORE R0, 0x011A
LOAD R0, syscall_handler
STORE R0, 0x0180
SYSRET
user_entry:
SYSCALL
CLI
HALT
syscall_handler:
LOAD R1, 1
SYSRET
gp_handler:
LOAD R2, 0x0050
OUT R2, 0xFF00
HALT
