# TORTOISE CPU Emulator

## Overview

TORTOISE is a simple CPU emulator implemented in C designed for educational purposes and to demonstrate the fundamentals of CPU architecture and instruction execution. The emulator models a minimalist 16 bit CPU with a small set of instructions, a fixed size memory space, and general purpose registers.

---

## Architecture

### CPU Structure

- **Registers:** 8 general purpose 16 bit registers (R0–R7).
- **Program Counter (PC):** 16 bit register pointing to the current instruction in memory.
- **Memory:** 64KB byteaddressable memory representing RAM.
- **Halted Flag:** Indicates whether the CPU is halted.

### Instruction Set

The emulator supports a small set of instructions facilitating data movement, arithmetic, control flow, and program termination:

| Opcode  | Mnemonic | Description                                   |
|---------|----------|-----------------------------------------------|
| 0x00    | NOP      | No operation                                  |
| 0x01    | LOAD     | Load immediate 16 bit value into register     |
| 0x02    | ADD      | Add immediate 16 bit value to register        |
| 0x03    | STORE    | Store register value into memory              |
| 0x04    | JMP      | Unconditional jump to memory address          |
| 0x05    | JZ       | Jump if register is zero                      |
| 0xFF    | HALT     | Stop execution                                |

### Fetch Decode Execute Cycle

The CPU operates by repeatedly performing the following steps:

1. **Fetch:** The instruction byte is read from memory at the address indicated by the PC.
2. **Decode:** The opcode and operands (register indices, immediate values, addresses) are parsed.
3. **Execute:** The instruction affects CPU state modifies registers, memory, or PC.
4. **Advance:** The PC is updated to point to the next instruction unless altered by control flow.

Instructions use fixed size formats to simplify decoding.

---

## Project Structure

- `src/`  
  Contains the CPU emulator source files:
  - `cpu.c`: CPU logic and instruction execution
  - `main.c`: Entry point, initializes CPU and starts execution

- `include/`  
  Header files exposing CPU API and data structures:
  - `cpu.h`

- `tests/`  
  Contains test programs and unit tests:
  - `test_cpu.c`: Basic tests for CPU instructions and state

- `Makefile`  
  Builds the project and tests.

---

## Usage

Build the emulator with:

```
make
```

Run the emulator binary `tortoise` to start execution of a loaded program.

Run tests with:

```
make run_test
```


---

## Extending TORTOISE

This emulator was designed to be a foundation for experimentation:
- Add more instructions (e.g., logical operations, subroutine calls).
- Implement I/O and interrupt handling.
- Introduce a simple assembler to write humanreadable programs.
- Extend memory size or support different data widths.

---

## Learning Goals

- Understand core CPU components and their interactions.
- Gain practical skills in emulator development and software CPU architecture.
- Explore instruction set design and implementation.
- Observe real time instruction flow and state mutation.

---

## License

This project is provided under the GPL3 License.

---
