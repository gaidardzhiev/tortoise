# TORTOISE CPU Emulator

## Overview
TORTOISE is a simple CPU emulator implemented in C designed for educational purposes and to demonstrate the fundamentals of CPU architecture and instruction execution. The emulator models a minimalist 16bit CPU with a small set of instructions, a fixed size memory space, and general purpose registers.

---

## Architecture

### CPU Structure
- **Registers:** 8 general-purpose 16bit registers (R0–R7).
- **Program Counter (PC):** 32bit register pointing to the current instruction in memory, allowing addressing 64KB memory space.
- **Stack Pointer (SP):** 32bit register managing the call stack in memory.
- **Memory:** 64KB byte addressable memory representing RAM.
- **Halted Flag:** Indicates whether the CPU is halted.

### Instruction Set
The emulator supports a set of instructions facilitating data movement, arithmetic, logical operations, control flow, and program termination:

| Opcode  | Mnemonic | Description                               |
|---------|----------|-------------------------------------------|
| 0x00    | NOP      | No operation                              |
| 0x01    | LOAD     | Load immediate 16bit value into register  |
| 0x02    | ADD      | Add immediate 16bit value to register     |
| 0x03    | STORE    | Store register value into memory          |
| 0x04    | JMP      | Unconditional jump to memory address      |
| 0x05    | JZ       | Jump if register is zero                  |
| 0x06    | AND      | Bitwise AND immediate with register       |
| 0x07    | OR       | Bitwise OR immediate with register        |
| 0x08    | XOR      | Bitwise XOR immediate with register       |
| 0x09    | NOT      | Bitwise NOT of register                   |
| 0x0A    | CALL     | Call subroutine at memory address         |
| 0x0B    | RET      | Return from subroutine                    |
| 0x0C    | IN       | Input from device (simulated)             |
| 0x0D    | OUT      | Output to device (simulated)              |
| 0xFF    | HALT     | Stop execution                            |

---

### Fetch Decode Execute Cycle

The CPU operates by repeatedly performing the following steps:

1. **Fetch:** The instruction byte is read from memory at the address indicated by the PC.
2. **Decode:** The opcode and operands (register indices, immediate values, addresses) are parsed.
3. **Execute:** The instruction modifies CPU state by changing registers, memory, or program counter.
4. **Advance:** The PC is updated to point to the next instruction unless altered by control flow instructions (e.g., JMP, CALL, RET).

Instructions use fixed size formats for simplicity of decoding.

---

## Project Structure

- `src/`  
  Contains the CPU emulator source files:  
  - `cpu.c`: Implementation of CPU registers, memory, and instruction execution logic  
  - `main.c`: Entrypoint initializing the CPU and starting execution  
- `include/`  
  Header files exposing CPU API and data structures:  
  - `cpu.h`  
- `tests/`  
  Contains test programs and unit tests:  
  - `test_cpu.c`: Basic and extended instruction tests including arithmetic, logical, and control flow operations
- `assembler/`  
  Contains the assembler source and test assembly files:  
  - `assembler.c`: A simple assembler converting human readable assembly into binary machine code compatible with TORTOISE CPU  
  - `.asm` files containing assembly source programs  
- `Makefile`  
  Builds the emulator, assembler, and tests 

---

## Usage

Build the emulator using:
```
make
```

Run the emulator starting the `tortoise` executable with your program loaded.

Run comprehensive CPU tests with:

```
make run_test
```
The tests include:
- A simple test verifying `LOAD`, `ADD`, and `HALT` instructions.
- An extended test covering logical bitwise operations (`AND`, `OR`, `XOR`, `NOT`), subroutine call/return, and correct register and memory behavior.

Build the assembler:
```
make assembler
```

Run assembler test:
```
make run_asm_test
```

---

## Assembler

- The TORTOISE assembler is a command line tool written in C that converts human readable assembly language programs into binary machine code executable by the TORTOISE CPU emulator.
- It supports the full instruction set of the emulator, including data movement (`LOAD`, `STORE`), arithmetic (`ADD`), logical operations (`AND`, `OR`, `XOR`, `NOT`), control flow (`JMP`, `JZ`, `CALL`, `RET`), I/O instructions (`IN`, `OUT`), and program termination (`HALT`). 
- The assembler reads assembly source files line by line, ignoring comments and whitespace, and parses instructions and operands which can be registers or immediate numeric values (in decimal or hexadecimal).
- It outputs machine code in a compact binary format ready for loading into emulator memory.
- This allows writing readable assembly programs for TORTOISE, which can then be assembled and executed on the emulator, providing a complete flow from source code to CPU execution.
- The assembler is designed to be simple, easy to understand, and easily extensible for adding new instructions or features.

## Extending TORTOISE

The emulator is structured to encourage experimentation and learning. Possible extensions include:

- Adding more instructions such as multiplication, division, or bit shifts.
- Implementing interrupt handling and I/O devices beyond simple simulated input/output.
- Developing a simple assembler and loader for human readable program writing. [DONE]
- Supporting larger memory sizes or different data widths (e.g., 32bit registers).

---

## Learning Goals

- Gain understanding of CPU architecture components and their interaction.
- Practical experience writing an emulator and instruction decoding.
- Insight into instruction set design including logical and control flow operations.
- Observe real time instruction execution and CPU state mutation debugging.

---

## License

This project is provided under the GPL3 License.

---
