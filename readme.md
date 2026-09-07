# TORTOISE CPU Emulator

## Overview
TORTOISE is a CPU emulator implemented in C designed for educational purposes and to demonstrate the fundamentals of CPU architecture and instruction execution. The emulator models a 32bit register architecture with a flags register, 1MB memory space, supervisor/user privilege modes, interrupt vector table, interactive debugger, virtual disk and text buffer peripherals, and a two-pass assembler supporting named labels and data directives.

## Architecture

### CPU Structure
- **Registers:** 8 general purpose 32bit registers (R0 through R7).
- **Program Counter (PC):** 32bit register pointing to the current instruction in memory, addressing up to 1MB of memory space.
- **Stack Pointer (SP):** 32bit register managing the call stack in memory, initialized to top of memory (1MB).
- **Flags Register:** 8bit register updated by arithmetic and logical instructions, carrying Zero (Z), Carry (C), Sign (S), Overflow (V), Interrupt (I), and User Mode (U) flags.
- **Memory:** 1MB byte addressable RAM space (1048576 bytes).
- **Privilege Modes:** Supervisor mode (Ring 0) with full hardware access and User mode (Ring 3) subject to General Protection Fault (vector 13) on privileged operations.
- **Interrupt Vector Table (IVT):** Base address at 0x0100 storing 16bit interrupt service routine addresses.
- **Halted Flag:** Indicates whether the CPU is halted.

### Instruction Set

| Opcode | Mnemonic      | Description                                                    |
|--------|---------------|----------------------------------------------------------------|
| 0x00   | NOP           | No operation                                                   |
| 0x01   | LOAD          | Load immediate 16bit value into register                       |
| 0x02   | ADD           | Add immediate 16bit value to register                          |
| 0x03   | STORE         | Store register value into direct 16bit memory address          |
| 0x04   | JMP           | Unconditional jump to memory address                           |
| 0x05   | JZ            | Jump if zero flag is set                                       |
| 0x06   | AND           | Bitwise AND immediate with register                            |
| 0x07   | OR            | Bitwise OR immediate with register                             |
| 0x08   | XOR           | Bitwise XOR immediate with register                            |
| 0x09   | NOT           | Bitwise NOT of register                                        |
| 0x0A   | CALL          | Call subroutine at memory address                              |
| 0x0B   | RET           | Return from subroutine                                         |
| 0x0C   | IN            | Input from I/O port                                            |
| 0x0D   | OUT           | Output to I/O port                                             |
| 0x0E   | SUB           | Subtract immediate 16bit value from register                   |
| 0x0F   | CMP           | Compare register against immediate, set flags, discard result  |
| 0x10   | JC            | Jump if carry flag is set                                      |
| 0x11   | JN            | Jump if sign flag is set (negative)                            |
| 0x12   | JO            | Jump if overflow flag is set                                   |
| 0x13   | JNZ           | Jump if zero flag is not set                                   |
| 0x14   | SHL           | Shift register left by immediate count                         |
| 0x15   | SHR           | Shift register right by immediate count                        |
| 0x16   | MUL           | Multiply register by immediate 16bit value                     |
| 0x17   | DIV           | Divide register by immediate value, fault on zero              |
| 0x18   | INT           | Trigger software interrupt vector                              |
| 0x19   | IRET          | Return from interrupt service routine, restore flags and PC    |
| 0x1A   | CLI           | Clear interrupt enable flag                                    |
| 0x1B   | STI           | Set interrupt enable flag                                      |
| 0x20   | LOAD32        | Load immediate 32bit value into register                       |
| 0x21   | STORE32       | Store 32bit register value into direct 32bit memory address    |
| 0x22   | ADD32         | Add immediate 32bit value to register                          |
| 0x23   | SUB32         | Subtract immediate 32bit value from register                   |
| 0x24   | MUL32         | Multiply register by immediate 32bit value                     |
| 0x25   | DIV32         | Divide register by immediate 32bit value                       |
| 0x26   | SHL32         | Shift 32bit register left by immediate count                   |
| 0x27   | SHR32         | Shift 32bit register right by immediate count                  |
| 0x28   | AND32         | Bitwise AND immediate 32bit value with register                |
| 0x29   | OR32          | Bitwise OR immediate 32bit value with register                 |
| 0x2A   | XOR32         | Bitwise XOR immediate 32bit value with register                |
| 0x2B   | CMP32         | Compare register against immediate 32bit value, set flags      |
| 0x30   | MOV           | Copy value from source register to destination register        |
| 0x31   | ADD (reg)     | Add source register to destination register                    |
| 0x32   | SUB (reg)     | Subtract source register from destination register             |
| 0x33   | CMP (reg)     | Compare destination register against source register           |
| 0x34   | AND (reg)     | Bitwise AND source register with destination register          |
| 0x35   | OR (reg)      | Bitwise OR source register with destination register           |
| 0x36   | XOR (reg)     | Bitwise XOR source register with destination register          |
| 0x37   | MUL (reg)     | Multiply destination register by source register               |
| 0x38   | DIV (reg)     | Divide destination register by source register                 |
| 0x39   | PUSH          | Push register onto stack                                       |
| 0x3A   | POP           | Pop register from stack                                        |
| 0x3B   | LOAD (indir)  | Load 16bit value from memory address [base + offset]           |
| 0x3C   | STORE (indir) | Store 16bit register into memory address [base + offset]       |
| 0x3D   | LOAD32 (ind)  | Load 32bit value from memory address [base + offset]           |
| 0x3E   | STORE32 (ind) | Store 32bit register into memory address [base + offset]       |
| 0x3F   | SYSCALL       | Trap into supervisor mode and jump to syscall handler          |
| 0x40   | SYSRET        | Return from syscall handler to user mode instruction           |
| 0xFF   | HALT          | Stop execution                                                 |

### Flags Register
Arithmetic and logical instructions update flags after execution:
- **Z (Zero):** Set when the result is 0.
- **C (Carry):** Set on unsigned borrow in subtraction, or unsigned overflow in addition and shift out.
- **S (Sign):** Set when the most significant bit of the result is 1.
- **V (Overflow):** Set when signed arithmetic overflows the representable range.
- **I (Interrupt):** Set when interrupts are enabled (controlled by STI and CLI).
- **U (User Mode):** Set when the CPU is running in user privilege mode.

### I/O Ports and Peripherals
- **0xFF00:** Console Data (character input and output).
- **0xFF01:** Console Status (ready bits).
- **0xFF02:** Hardware Timer (reading returns tick count; writing triggers timer interrupt vector 1).
- **0xFF03:** Random Number Generator (returns pseudo random 16bit number).
- **0xFF04:** System Control (halt trigger).
- **0xFF10:** Disk Sector Index (0 to 255).
- **0xFF11:** Disk Command (1 = Read 512-byte sector to buffer, 2 = Write 512-byte sector from buffer).
- **0xFF12:** Disk Status (1 = ready).
- **0xFF13:** Disk Memory Buffer Address (pointer to RAM buffer).
- **0x000B8000:** Video text screen buffer (2000 bytes representing 80x25 characters).

## Project Structure

`src/` contains the CPU emulator source files. `cpu.c` implements CPU registers, flags, memory, peripheral devices, debugger, disassembler, and instruction execution logic. `main.c` is the entrypoint initializing the CPU, handling command line arguments, and starting execution or debugger mode.

`include/` contains header files exposing the CPU API, data structures, opcode constants, port addresses, and flag definitions.

`tests/` contains unit tests. `test_cpu.c` covers comprehensive test suites including basic ops, extended ALU, shifts, mul/div, interrupts, 32bit registers, reg-reg ALU, indirect addressing, push/pop, and disassembly.

`assembler/` contains the two-pass assembler source and all assembly test programs. `assembler.c` converts assembly source files into binary machine code. The `.asm` files are the assembly source programs.

`Makefile` provides build targets for the emulator, assembler, unit tests, debugger, and individual assembly test suites.

## Usage

Build everything (emulator, assembler, unit tests):
```
make
```

Run the emulator with a binary program:
```
./tortoise program.bin
```

Run in interactive debugger mode:
```
./tortoise -d program.bin
```

Run the C unit tests:
```
make run_test
```

Build the assembler:
```
make assembler
```

Run all assembler tests:
```
make run_asm_test
```

Run individual test targets:
```
make run_shift_test
make run_mul_div_test
make run_labels_test
make run_interrupt_test
make run_io_test
make run_reg32_test
make run_indirect_test
make run_directives_test
make run_privilege_test
make run_peripherals_test
make run_multitask_test
make run_debugger_test
```

## Assembler

The TORTOISE assembler is a two-pass command line tool written in C that converts human readable assembly language programs into binary machine code.

### Pass 1 and Pass 2
- **Pass 1:** Scans the source file, records label positions and data directive allocations in a symbol table, and computes precise instruction byte offsets.
- **Pass 2:** Emits machine code bytes into the binary buffer, resolving label targets, indirect addresses, and immediate values.

### Directives
- `.org <addr>`: Sets the assembly location counter to an absolute address.
- `.byte <val1>, <val2>...`: Emits raw 8bit bytes.
- `.word <val1>, <val2>...`: Emits 16bit words in little endian format.
- `.dword <val1>, <val2>...`: Emits 32bit double words in little endian format.
- `.ascii "string"`: Emits ASCII string characters without null terminator.
- `.asciiz "string"`: Emits ASCII string characters with a null terminator.

### Addressing Modes
- **Immediate:** `LOAD R0, 0x1234`
- **Register to Register:** `MOV R1, R0`, `ADD R1, R0`, `SUB R1, R0`, `CMP R1, R0`
- **Register Indirect:** `LOAD R0, [R1]`, `STORE [R1], R0`
- **Base Plus Offset:** `LOAD R0, [R1 + 4]`, `STORE [R1 + 8], R0`
- **32bit Indirect:** `LOAD32 R0, [R1 + 0]`, `STORE32 [R1 + 0], R0`
- **Stack:** `PUSH R0`, `POP R0`

## Interactive Debugger

The emulator features a built-in step debugger invoked with the `-d` flag:
```
./tortoise -d program.bin
```

### Commands
- `s` or `<Enter>`: Single step next instruction.
- `c`: Continue execution until a breakpoint is hit or the CPU halts.
- `b <addr>`: Set breakpoint at hex address (for example `b 0x00010`).
- `r`: Display all registers (R0 to R7), PC, SP, FLAGS, and privilege mode.
- `d <addr> <n>`: Disassemble `n` instructions starting from `addr`.
- `x <addr> <n>`: Hex dump `n` bytes of memory starting from `addr`.
- `q`: Quit debugger.

## Assembly Test Programs

### Logic Test
`assembler/logic.asm` tests the core bitwise instructions in sequence (AND, OR, XOR, NOT).

### Truth Table Test
`assembler/truth.asm` verifies truth tables across all four 2-bit combinations.

### Bitwise Masking Test
`assembler/bitwise_masking.asm` verifies AND and XOR chained together.

### OR Chain Test
`assembler/or_chain.asm` tests consecutive OR operations accumulating bit sets.

### Bitwise NOT Toggle Test
`assembler/toggle_bits.asm` verifies flipping all 16 bits with NOT.

### XOR Flip Flop Test
`assembler/xor_flip_flop.asm` tests XOR against a full 0xFFFF mask.

### Flags Test
`assembler/flags_test.asm` exercises all conditional jump instructions (JC, JN, JO, JNZ, JZ) and verifies the flags register under loop countdown, signed overflow, and comparisons.

### Peano Arithmetic Test
`assembler/peano.asm` proves the predecessor axiom of Peano arithmetic by decrementing starting values N=1 through N=8 down to zero and confirming with CMP traps.

### Shift Instructions Test
`assembler/shift_test.asm` tests SHL and SHR ALU instructions, verifies shift carry flag detection on overflow, and prints S on success.

### Multiplication and Division Test
`assembler/mul_div_test.asm` tests MUL and DIV instructions, confirming arithmetic results with CMP and printing M on success.

### Named Labels Test
`assembler/labels_test.asm` verifies two-pass assembler symbol resolution with backward loop jumps and forward subroutine CALLs to named labels, printing L on success.

### Interrupt Handling Test
`assembler/interrupt_test.asm` registers an interrupt handler in the IVT at 0x0104, enables interrupts with STI, triggers software interrupt INT 2, verifies context restore via IRET, and prints I on success.

### Extended I/O Test
`assembler/io_test.asm` tests port communication across Console Status (0xFF01), Hardware Timer (0xFF02), and Random Number Generator (0xFF03), printing O on success.

### 32bit Registers and Memory Test
`assembler/reg32_test.asm` exercises 32bit instructions (LOAD32, STORE32, ADD32, SUB32, CMP32) addressing memory beyond the 64KB boundary at 0x00020000, printing R on success.

### Indirect Addressing and Reg-Reg ALU Test
`assembler/indirect_test.asm` verifies MOV, register to register arithmetic (ADD, SUB, CMP), stack operations (PUSH, POP), and base plus offset indirect memory access ([R3 + 4]), printing N on success.

### Assembler Directives Test
`assembler/directives_test.asm` tests `.byte`, `.word`, `.dword`, and `.asciiz` data directives, verifying embedded tables and strings loaded via indirect addressing, printing D on success.

### Privilege and Protection Test
`assembler/privilege_test.asm` transitions to user mode via SYSRET, executes SYSCALL, handles user to supervisor transition, attempts an unauthorized privileged CLI instruction in user mode, catches the resulting General Protection Fault (vector 13), and prints P on success.

### Peripherals Test
`assembler/peripherals_test.asm` writes to and reads from the memory mapped video screen buffer at 0x000B8000, and performs sector write and read operations via the virtual disk controller (ports 0xFF10 to 0xFF13), printing V on success.

### Preemptive Multitasking Test
`assembler/multitask.asm` tests preemptive context switching driven by the hardware timer interrupt (vector 1), verifying state save and restore across task boundaries, printing K on success.

## Extending TORTOISE

The emulator is structured to encourage experimentation and learning. All core and advanced extensions have been implemented:

- ~~Developing a simple assembler and loader for human readable program writing.~~
- ~~Fix compiler warnings.~~
- ~~Flags register with SUB, CMP, and conditional jumps (JC, JN, JO, JNZ).~~
- ~~Adding shift instructions (SHL, SHR) to complete the ALU.~~
- ~~Adding multiplication and division instructions.~~
- ~~Implementing a two-pass assembler to support named labels in assembly source.~~
- ~~Implementing interrupt handling and I/O devices beyond simple simulated input/output.~~
- ~~Supporting larger memory sizes or different data widths (32bit registers).~~
- ~~Register to register ALU operations and indirect base plus offset addressing.~~
- ~~Assembler directives (.org, .byte, .word, .dword, .ascii, .asciiz).~~
- ~~Privilege levels (Supervisor and User modes), system calls (SYSCALL, SYSRET), and fault protection.~~
- ~~Interactive step debugger and disassembler.~~
- ~~Hardware peripherals: text-mode video buffer and virtual block disk storage.~~
- ~~Preemptive multitasking scheduler.~~

## Learning Goals

Gain understanding of CPU architecture components and their interaction.
Practical experience writing an emulator and instruction decoding.
Insight into instruction set design including arithmetic, flags, logical, and control flow operations.
Observe real time instruction execution and CPU state mutation debugging.
Understand how flags registers drive conditional execution in real hardware.
Experience implementing privilege rings, trap handling, system calls, and peripheral devices.

## License

This project is provided under the GPL3 License.
