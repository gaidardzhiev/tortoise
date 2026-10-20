tortoise
========

## NAME

tortoise - a self-hosting 32-bit computer system

## SYNOPSIS

```
./tortoise [-d] [-v] [-n] [-t] [-m MB] [-x N] [-r rom.bin]
           [-D disk.img] [-E disk2.img] [-i file] [-g port]
           [-T file] [-S file] [-L file] [program.bin]
./as [-c] [-I dir] [-o out] file
./tlink [-b] [-M] [-T addr] [-e sym] -o out file.o ...
./tcc0 [-S|-c] [-bare] [-I dir] [-D name[=value]] [-o out] file ...
./tdisk -o disk.img [-s sectors] [-b boot.bin] [-k kernel.tex] [entries ...]
```

## DESCRIPTION

*tortoise* emulates a 32-bit computer with eight general registers, paged virtual memory, supervisor and user modes, interrupts, and memory-mapped devices. A boot ROM loads an operating system from a virtual disk. The system runs processes with private address spaces, a file system, signals, pipes, a shell, and the usual file and process operations.

An assembler, linker, C compiler, and C library target the machine. The operating system runs the same assembler, linker, and compiler as ordinary programs; the compiler compiles itself and the other tools there. The host runs the emulator and holds the disk files; compilation and execution inside the emulated system use its own toolchain.

The processor starts in legacy mode, retaining the original 16-bit interrupt conventions and assembly programs. Setting EXT in CR0 selects extended mode, which uses a separate exception vector table, takes interrupts from the interrupt controller, and permits address translation and protection. Instructions introduced after the original instruction set, including 32-bit arithmetic and control flow, work in both modes. The processor counts executed instructions to drive timers and disk transfers; *-t* also derives the real-time clock from the instruction count.

## BUILDING

A C compiler and make are needed for the first build. The Makefile selects gcc, musl-gcc, tcc, or clang, in that order.

```
make
```

builds *tortoise*, the assembler *as*, the linker *tlink*, the C compiler *tcc0*, the libraries, and the unit tests. The operating-system image is built with

```
make os
```

which assembles the ROM and boot sector, compiles the kernel and programs with *tcc0*, and puts them on *disk.img*. The framebuffer and keyboard use SDL2 in a separate build:

```
make clean
make SDL=1
```

`make clean` removes the generated binaries and images. See TESTING for the build and boot checks.

## RUNNING

```
make run_os
```

builds the operating system and boots it with 16 MB of RAM. The equivalent command is

```
./tortoise -m 16 -r rom/rom.bin -D disk.img
```

The ROM loads sector 0, the boot sector loads the kernel, and the kernel starts */bin/init*, which starts a shell on the console. Text output goes to the terminal running *tortoise*. The shell accepts commands such as `ls`, `cat`, `ps`, and `sh`, with pipes, redirection, and background jobs.

To boot with the compiler and its tools installed inside the machine, build the self-hosting disk:

```
make selfhost.img
./tortoise -m 16 -r rom/rom.bin -D selfhost.img
```

The resulting shell can compile, link, and execute C programs without the host toolchain:

```
$ tcc0 /src/hello.c -o hello
$ ./hello
```

To run a bare machine program, assemble it and pass the flat binary to the emulator:

```
./as assembler/shift_test.asm shift.bin
./tortoise shift.bin
```

A file beginning with `TEX1` is an executable produced by the linker. Its text and data are loaded at the addresses in its header, its bss is cleared, and execution begins at its entry point. A flat binary is loaded at address 0, with the remaining RAM filled with 0xFF (HALT). With *-r* the emulator starts in ROM and the ROM chooses what to load.

## OPTIONS

```
-d          Run in the interactive debugger.
-m MB       RAM size in megabytes, from 1 to 256; the default is 1.
-x N        Stop after N instructions and exit with status 124.
-r rom.bin  Map a boot ROM at 0xFFFF0000 and start there.
-D disk.img Use disk.img for the first virtual disk.
-E disk.img Use disk.img for the second virtual disk.
-i file     Take UART input from file.
-t          Use an instruction-count-based clock.
-g port     Wait for a GDB remote connection on 127.0.0.1:port.
-T file     Write an instruction trace to file.
-S file     Save the machine state on exit.
-L file     Load a saved machine state at startup.
-n          Turn off the decoded-instruction cache.
-c          Print the executed instruction count on exit.
-v          Print the legacy instruction trace.
```

## ASSEMBLER

```
./as prog.asm prog.bin
./as -c file.s -o file.o
./as [-I dir] ...
```

The assembler reads one statement per line, an optional `label:` followed by an instruction or a directive; a semicolon starts a comment. Mnemonics and register names may be in either case; labels are case sensitive. Operands are registers R0 to R7, SP in memory operands, control registers CR0 to CR15, memory operands `[R1]`, `[R1 + 8]`, `[R1 - 4]`, `[SP + 12]`, and expressions. An expression may use decimal, 0x hex and 0b binary numbers, character constants such as 'A' or '\n', labels, `.` for the current location, the operators `+ - * / % & | ^ ~ << >>` with the precedence of C, and parentheses. Relative targets are written as the label; the assembler computes the offset.

| Directive | Meaning |
|---|---|
| .org addr | set the location counter |
| .byte v, ... | bytes (strings allowed) |
| .word v, ... | 16 bit words |
| .dword v, ... | 32 bit words |
| .ascii "s" | characters |
| .asciiz "s" | characters and a zero byte |
| .equ name, v | define a constant |
| .include "file" | read another file |
| .align n | pad to a multiple of n |
| .space n [, fill] | n bytes |
| .globl name | export a symbol |
| .text .data .bss | select a section (object files only) |
| .macro name a, b ... .endm | define a macro; \a in the body is an argument, \@ a unique number |

A label that begins with a dot is local to the preceding ordinary label.

Without -c the output is a flat image. Normally it starts at address 0 and a .org pads with zeros; if the first .org comes before any output and names an address of 0x10000000 or more, the image starts there instead, which is how the ROM is assembled at 0xFFFF0000. With -c the output is a TOF object file with text, data and bss sections, a symbol table and relocations (32 bit absolute, 32 bit PC relative, 16 bit absolute). Errors are reported as `file:line: error: message` and the assembler exits with status 1.

## OBJECT FILES AND LINKING

```
./tlink [-b] [-M] [-T addr] [-e sym] -o out file.o ...
```

The linker places the text of all objects at the -T address (default 0), the data at the next 4KB boundary, and the bss after the data. It defines `_etext`, `_edata` and `_end`. The entry point is the -e symbol, `_start` if it exists, or the start of the text. Duplicate global symbols and unresolved references are errors. The output is a TEX1 executable: a 32 byte header (magic, entry, text address, text size, data address, data size, bss size), the text and the data. With -b it is a flat image instead. With -M the linker prints a map of all symbols and their addresses. The output file is made executable.

## C COMPILER

```
./tcc0 -S file.c -o file.s
./tcc0 -c file.c -o file.o
./tcc0 [-bare] file.c ... -o prog
./tcc0 -I dir -D name[=val] ...
```

*tcc0* compiles C to TORTOISE assembly and runs the assembler and the linker as separate programs, which it finds next to itself. It accepts C without floating point, long long, bitfields, variable length arrays and old-style function definitions: char (signed), short, int and long (32 bits) with their unsigned forms, pointers, arrays, structures, unions, enumerations, typedef, function pointers, every operator and statement, static and extern, full initializers for globals, variadic functions through `<stdarg.h>`, and a preprocessor with #include, #define with arguments, # and ##, #undef, #if, #ifdef, #ifndef, #elif, #else, #endif, defined() and #error. Unsupported constructs are reported with file and line.

## CALLING CONVENTION

Arguments are pushed from right to left, four bytes each, and removed by the caller; structures are copied. The result is returned in R0, and a function returning a structure receives a hidden pointer to the result as its first argument. R0 to R3 belong to the caller, R4 to R6 to the callee, and R7 is the frame pointer: a function begins with `PUSH32 R7`, `MOVSP R7`, `ADDSP -frame`, finds its return address at [R7 + 4] and its first argument at [R7 + 8], and ends with `SETSP R7`, `POP32 R7`, `RET32`. C names are used unchanged as assembly labels.

With -bare a program is linked at address 0 for the bare machine; without it, at 0x00400000 for the operating system.

## LIBRARY

The library in `lib/` is built twice from the same sources by `make libc`: for the bare machine in `lib/bare` and for the operating system in `lib/os`. It provides the usual functions of `<string.h>`, `<stdlib.h>` (malloc, free, calloc, realloc on a first fit free list, atoi, strtol, strtoul, abs, exit, qsort, rand, srand), `<stdio.h>` (putchar, getchar, puts, the printf family with %d %i %u %x %X %o %c %s %p %%, flags, width and precision, and FILE streams with fopen, fclose, fread, fwrite, fgets, fputs, fgetc, fputc), `<ctype.h>`, `<assert.h>`, `<unistd.h>`, `<fcntl.h>`, `<poll.h>`, `<utime.h>`, `<errno.h>`, `<signal.h>`, `<time.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<sys/wait.h>`, `<sys/utsname.h>` and `<sys/times.h>`, with headers in `lib/include`. getenv, setenv, unsetenv and putenv work on `environ`; execv passes it on and execvp searches PATH. signal installs a handler with SA_RESTART. memcpy and memset are written in assembly (`lib/memops.s`).

On the bare machine the start up code sets SP to the top of RAM, clears the bss and calls main; output and input go through the UART, exit writes POWEROFF, memory grows upward from `_end`, and fopen fails. Under the operating system the library calls the kernel.

## BOOT ROM

The ROM in *rom/rom.asm* is assembled at 0xFFFF0000 with `make rom`. At reset it sets SP to the top of RAM, prints `TORTOISE ROM`, and reads disk sector 0 to 0x00010000. If the sector ends with the bytes 0x55 0xAA it jumps there with the RAM size in R0. Otherwise it prints `no boot disk` and starts a monitor on the UART. The monitor prompts with `> ` and accepts, with numbers in hex,

| Command | Action |
|---|---|
| m addr [n] | dump n bytes, default 64 |
| w addr b1 b2 ... | write bytes |
| g addr | call addr; a program ending in RET32 returns to the monitor |
| b | try to boot again |
| r | print the RAM size |

Anything else prints `?`.

## OPERATING SYSTEM

The disk holds the boot sector in sector 0, the kernel as a TEX1 image from sector 1, and the file system from sector 1024. The boot sector loads the kernel and jumps to it. The kernel is linked at 0x00020000 and runs in supervisor mode with paging on.

## PROCESS MEMORY

Every process has its own page directory. The first 4MB of every address space map the same physical memory, for the supervisor only; the devices are mapped for the supervisor too. Page tables, kernel stacks and the kernel heap come from the memory between the kernel and 4MB. The kernel heap, kmalloc, serves requests of 16 to 2048 bytes from pages divided into power of two classes and larger requests in whole pages. Memory above 4MB is handed out in pages from a bitmap, and each page carries a reference count.

A process consists of regions: text and data, a heap, a stack and any number of anonymous mappings. A program's text and data start at 0x00400000 and are loaded when it starts. The heap follows the data and moves with sbrk. The stack ends at 0x7FFFF000 and may grow to 1MB. Pages of the heap, the stack and private mappings are allocated when they are first touched and are zero.

fork copies no memory. Parent and child share every page; private writable pages are marked read only and copy on write (bit 9 of the page table entry), and the first write to such a page by either process copies it unless no one else refers to it.

mmap creates anonymous mappings, private or shared, placed downward from 0x60000000 unless MAP_FIXED gives the address. Shared mappings are allocated immediately and stay shared between parent and child after fork. munmap removes any part of any region. An access outside every region, or a write to a read only region, is a fault.

## PROCESSES AND SIGNALS

There may be 64 processes, each with an 8KB kernel stack, 32 file descriptors, a current directory, a parent, a process group and a session. Process numbers increase from 1 and wrap at 32768, skipping numbers still used by a process, group or session. Sleeping processes wait on wait queues; timed sleeps use kernel timers driven by the tick, which is one timer interrupt every 10000 instructions (called 10 ms). The idle loop waits with WFI. Process 1 runs /bin/init, which runs /bin/sh and starts it again when it exits. A fault in the kernel prints the registers and powers off with status 99. Everything the kernel prints is also kept in a 16KB log. If /etc/kernel.conf contains the word selftest, the kernel tests its heap at boot.

The scheduler runs the runnable processes of the lowest nice value, from -20 to 19, in turn. A process runs for 20 minus nice/2 ticks unless a process of lower nice value becomes runnable; sleeping does not change its nice value. Each process collects user ticks and system ticks, the latter measured with the timer's COUNT register while it executes system calls, and the ticks of the children it has waited for.

Signals are numbered as on other Unix systems: HUP 1, INT 2, QUIT 3, ILL 4, TRAP 5, ABRT 6, BUS 7, FPE 8, KILL 9, USR1 10, SEGV 11, USR2 12, PIPE 13, ALRM 14, TERM 15, CHLD 17, CONT 18, STOP 19, TSTP 20, TTIN 21 and TTOU 22. A set of signals is a 32 bit mask. By default CHLD and CONT are ignored, STOP, TSTP, TTIN and TTOU stop the process, CONT continues it, and every other signal ends it. KILL and STOP cannot be caught, blocked or ignored, and init receives only the signals it catches. Before every return to user mode the kernel delivers the lowest pending signal that is not blocked. For a caught signal it pushes the trap frame, the signal mask to restore, the signal number and the return address `sa_restorer` on the user stack and starts the handler with the signal and `sa_mask` blocked; the library's restorer `__sigtramp` calls sigreturn, which restores the frame and the mask. A frame that cannot be pushed ends the process with SEGV. A system call that sleeps and is interrupted by a caught signal fails with EINTR, or starts again if the handler has SA_RESTART (pause, sleep and poll always fail). A signal that is not caught never interrupts a call.

Faults become signals: a page or protection fault SEGV, an invalid opcode ILL, a divide error FPE, a bus error BUS and BRK TRAP. If the program does not catch the signal, the kernel prints `pid N: fault V at 0xPC` and ends it. A handler may repair the cause, for example by mapping the missing page, and the instruction runs again.

A wait status is the exit code times 256, the signal number for a process ended by a signal, the stop signal times 256 plus 0x7F for a stopped process, and 0xFFFF for a continued one; `<sys/wait.h>` decodes it. A process that ends sends CHLD to its parent and passes its children to init. When the parent ignores CHLD, ended children are removed at once. A process group that loses its last member whose parent is in another group of the same session is orphaned; if it has stopped members they receive HUP and then CONT, and the stop signals TSTP, TTIN and TTOU are discarded in it.

exec replaces the program. execve passes up to 62 arguments and 64 environment strings, 8KB in all; the old exec, syscall 8, passes no environment. At entry R1 is argc, R2 argv and R3 the environment, which the start up code stores in `environ`. Descriptors marked close on exec are closed, caught signals return to their default action, ignored ones stay ignored and the signal mask is kept. A file that begins with `#!` names an interpreter and at most one argument on a line of up to 128 bytes; the interpreter runs with the argument and the script path in front of the remaining arguments. The set user id and set group id bits of the program set the effective ids.

## SYSTEM CALLS

A program enters the kernel with SYSCALL, the call number in R0 and arguments in R1 to R5; the result returns in R0, negative on error. The library sets errno and returns -1. The kernel checks that every pointer refers to the caller's own memory.

| No | Call | No | Call |
|---|---|---|---|
| 1 | exit(status) | 44 | kill(pid, sig) |
| 2 | fork() | 45 | sigaction(sig, act, old) |
| 3 | read(fd, buf, n) | 46 | sigprocmask(how, set, old) |
| 4 | write(fd, buf, n) | 47 | sigreturn() |
| 5 | open(path, flags) | 48 | sigpending(set) |
| 6 | close(fd) | 49 | pause() |
| 7 | wait(status) | 50 | alarm(ticks) |
| 8 | exec(path, argv) | 51 | setpgid(pid, pgid) |
| 9 | sbrk(incr) | 52 | getpgid(pid) |
| 10 | getpid() | 53 | setsid() |
| 11 | unlink(path) | 54 | getsid(pid) |
| 12 | mkdir(path) | 55 | fcntl(fd, cmd, arg) |
| 13 | chdir(path) | 56 | ioctl(fd, req, arg) |
| 14 | readdir(fd, dirent) | 57 | poll(fds, n, timeout) |
| 15 | dup(fd) | 58 | mmap(addr, len, prot, flags, fd) |
| 16 | pipe(fds) | 59 | munmap(addr, len) |
| 17 | kill(pid), sends KILL | 60 | mount(source, target, type) |
| 18 | sleep(ticks) | 61 | umount(target) |
| 19 | time() | 62 | execve(path, argv, envp) |
| 20 | shutdown(code) | 63 | truncate(path, len) |
| 21 | stat(path, st), old form | 64 | ftruncate(fd, len) |
| 22 | procinfo(index, info) | 65 | utime(path, times) |
| 23 | waitpid(pid, status, options) | 66 | uname(buf) |
| 24 | getppid() | 67 | clock_gettime(clock, ts) |
| 25 | lseek(fd, off, whence) | 68 | nice(inc) |
| 26 | fstat(fd, st) | 69 | sethostname(name, len) |
| 27 | dup2(old, new) | 84 | stat2(path, st) |
| 28 | rename(old, new) | 85 | lstat2(path, st) |
| 29 | rmdir(path) | 86 | sync() |
| 30 | getcwd(buf, size) | 87 | times(tms) |
| 31 | link(old, new) | 92 | fchmod(fd, mode) |
| 32 | symlink(target, path) | | |
| 33 | readlink(path, buf, size) | | |
| 34 | chmod(path, mode) | | |

open takes O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_EXCL, O_TRUNC, O_APPEND, O_NONBLOCK, O_DIRECTORY, O_NOFOLLOW and O_CLOEXEC. fcntl duplicates descriptors and reads or sets FD_CLOEXEC, O_APPEND and O_NONBLOCK; a non blocking read or write that would wait fails with EAGAIN. poll waits on pipes, the console and files, with a timeout in ticks. unlink, the old call 11, also removes empty directories. stat2, lstat2 and fstat return the POSIX fields device, inode, mode with the file type, links, owner, group, size, three times and blocks; the library's stat, lstat and fstat use them.

waitpid takes a process number, -1 for any child, 0 for the caller's group or minus a group number, and the options WNOHANG 1, WUNTRACED 2 and WCONTINUED 8. kill sends to one process, to the caller's group (0), to all processes it may signal except init (-1) or to a group (below -1); signal 0 only checks. alarm sends ALRM after the given number of ticks and returns the ticks left of the previous alarm. sigprocmask takes SIG_BLOCK 0, SIG_UNBLOCK 1 and SIG_SETMASK 2. uname fills five fields of 32 bytes: TORTOISE, the host name (tortoise until sethostname changes it), the release 3.0, the build date and tortoise. clock_gettime reads clock 0, the boot time from the real time clock plus the ticks, or clock 1, the ticks since boot, in seconds and nanoseconds in steps of 10 ms. nice lowers or, for the super user, raises the priority and returns the new value. times returns the ticks since boot.

## CONSOLE

File descriptors 0, 1 and 2 of init are the console. Output goes to the UART. Input is collected a line at a time with echo and backspace, and a read returns at the end of a line; Control D at the start of a line is end of file. The kernel takes input only while a program is reading the console, so typing ahead waits in the UART until it is wanted.

## FILE SYSTEMS

The kernel reaches every file through a virtual file system. A vnode stands for one file of one mounted file system and carries a reference count; open files, current directories and mount points refer to vnodes. Each file system type supplies a table of operations: lookup, create, mkdir, unlink, rmdir, rename, link, symlink, readlink, read, write, readdir, getattr, setattr, truncate, ioctl and poll. A file that is removed while it is open stays readable and writable through its descriptors and disappears at the last close.

Path names are resolved one component at a time, crossing mount points in both directions: a component that names a mount point leads to the root of the mounted file system, and `..` at that root leads to the parent of the mount point. At most 8 symbolic links are followed; more is ELOOP. Every directory on the way must be searchable.

At boot the kernel mounts TFS from disk 0 on `/`, devfs on `/dev`, procfs on `/proc` and a ramfs on `/tmp`, creating those directories if needed. mount and umount, for the super user, attach and detach `tfs` (from `/dev/disk0` or `/dev/disk1`), `ramfs`, `dev` and `proc`. A file system in use cannot be unmounted.

TFS keeps 512 byte blocks. Block 0 is the superblock (magic `TFS2`, sizes, the places of the inode table, bitmap and data, and the counts of free blocks and inodes). 8192 inodes of 128 bytes follow, then the block bitmap and the data. An inode holds a type (file, directory, symbolic link, socket or fifo), link count, permission bits, owner, group, size, access, modification and change times in seconds, twelve direct blocks and single, double and triple indirect blocks, enough for files of about 1GB. A directory is a file of 32 byte entries, an inode number and a name of up to 27 characters; every directory has `.` and `..`, and the root is inode 1. A symbolic link keeps its target in its data. The kernel caches 64 blocks and writes through to the disk. On every disk the file system starts at sector 1024.

ramfs keeps its files in memory pages, at most a quarter of the user memory, and loses them at shutdown. devfs is a fixed directory of the devices described under DEVICE FILES. procfs is mounted on `/proc`.

The host program tdisk builds and examines images:

```
./tdisk -o disk.img [-s sectors] [-b boot.bin] [-k kernel.tex] [entries ...]
./tdisk -l disk.img
./tdisk -ll disk.img
./tdisk -x disk.img /path hostfile
```

An entry is `hostfile:/path[:mode[:uid:gid]]` for a file (mode 0644, or 0755 when the host file is executable), `-d /dir[:mode[:uid:gid]]` for a directory or `-L target:/path` for a symbolic link; missing directories on the way are made with mode 0755. -l lists files and sizes, -ll adds mode, owner, group and link count. tdisk writes TFS2 and reads both TFS2 and the older TFS1; -1 writes a TFS1 image.

## PROGRAMS AND SHELL

/bin holds init, sh, echo, cat, ls (-l shows type and size), mkdir, rm, wc, hello, ps, kill, sleep, shutdown, mount, umount, and crash, which faults on purpose. kill sends TERM, or the signal given as `-N` or `-NAME`; `kill -l` lists the names.

The shell prompts with `$ `. It splits lines at blanks, keeps text between double quotes together, and treats `<`, `>`, `>>`, `|` and `&` as separate words. It runs cd and exit itself and looks up other commands as given when they contain a slash and in /bin otherwise. A command line may be a pipeline of any length with redirections and a trailing `&` to run it in the background; the shell then prints the process number, and later `[pid] done, status N` when it collects the job. The status shown is the exit code, or 128 plus the signal number for a command ended by a signal; a foreground command with a status of 128 or more is reported as terminated.

## SELF HOSTING

`make selfhost.img` builds an image that also holds the assembler, the linker and tcc0, compiled by tcc0, with the library in /lib. The resulting image boots through the same ROM and contains the compiler, assembler, linker, headers, and libraries. A program compiled with *tcc0* from the shell runs as another process in the operating system. See RUNNING for the commands.

## DEVICE FILES

`/dev` holds the devices. stat reports them as character devices whose device number is the major number times 256 plus the minor number.

| Name | Major, minor | Device |
|---|---|---|
| console | 4, 1 | the system console |
| tty | 4, 0 | the terminal of the calling process |
| null | 1, 3 | reads give end of file, writes vanish |
| zero | 1, 5 | reads give zero bytes |
| random, urandom | 1, 8 and 1, 9 | xorshift128 numbers seeded from the clock at boot |
| kmsg | 1, 11 | the kernel log, mode 0600 |
| disk0, disk1 | 8, 0 and 8, 1 | the disks as arrays of bytes, mode 0600 |
| fb0 | 29, 0 | the framebuffer, mode 0660; without SDL it reads end of file |
| kbd | 13, 0 | keyboard characters, mode 0660 |

## REGISTERS

The processor has eight general registers, R0 to R7, of 32 bits each, a 32 bit program counter PC, a 32 bit stack pointer SP, and a flags register. The stack grows downward. At reset PC is 0 and SP holds the size of RAM.

The flags are

| Bit | Name | Meaning |
|---|---|---|
| 0 | Z | result was zero |
| 1 | C | unsigned carry out of an addition, or unsigned borrow of a subtraction |
| 2 | S | most significant bit of the result |
| 3 | V | signed overflow |
| 4 | I | interrupts enabled |
| 5 | U | user mode |

Supervisor mode (U clear) may execute every instruction. In user mode a set of privileged instructions raises a protection fault, vector 13.

Nine control registers, CR0 to CR8, are reached through MFCR and MTCR, both privileged.

| CR | Name | Meaning |
|---|---|---|
| 0 | CTRL | bit 0 EXT (extended mode), bit 1 PROT (base and limit), bit 2 PG (paging) |
| 1 | EVT | address of the exception vector table, 64 entries of 32 bits |
| 2 | FADDR | faulting address of the last bus error, protection fault or page fault |
| 3 | PTBASE | physical address of the page directory |
| 4 | KSP | stack pointer loaded on an exception taken in user mode |
| 5 | BASE | base for PROT mode |
| 6 | LIMIT | limit for PROT mode |
| 7 | CAUSE | bits 0 to 7 vector of the last exception, bits 8 to 15 error code |
| 8 | RAMSIZE | size of RAM in bytes, read only |

## EXECUTION MODES

The machine was first defined as a simple 16 bit oriented processor, and programs written for that definition still run unchanged. The original behaviour is called legacy mode and is in force while EXT in CR0 is clear. Setting EXT selects extended mode, which changes how interrupts and exceptions are delivered and makes address translation possible. Instructions with opcodes 0x41 and above work in both modes.

In legacy mode an interrupt pushes the flags and a 16 bit return address and jumps through the interrupt vector table at address 0x0100, which holds 16 bit handler addresses; vector 1 is the timer, vector 13 the protection fault and vector 64 (table offset 0x80) the system call entry. SYSCALL switches to supervisor mode and remembers the return address; SYSRET returns to it in user mode. In user mode CLI, STI, IRET, IN and OUT raise vector 13. An unknown opcode or a bus error stops the emulator with exit status 2.

In extended mode the processor uses the exception vector table at CR1, takes hardware interrupts from the interrupt controller, and enforces the privilege rules described under PRIVILEGE.

## INSTRUCTION FORMATS

Legacy instructions have their own encodings, shown under LEGACY INSTRUCTIONS. In register bytes, the high four bits name the first operand and the low four bits the second.

Instructions with opcodes 0x41 and above use five formats. All multi byte quantities are little endian.

| Format | Bytes | Layout |
|---|---|---|
| N | 1 | opcode |
| R | 2 | opcode, register byte A<<4 \| B |
| RI | 6 | opcode, register byte, imm32 |
| I | 6 | opcode, register byte, imm32 |
| M | 6 | opcode, register byte, signed disp32 |

In the M format and in LEA, base register number 8 means SP. A relative target is the address of the following instruction plus the signed immediate. Any register number an instruction does not allow raises an invalid opcode fault.

## LEGACY INSTRUCTIONS

These are the instructions of the original machine. Their 16 bit forms compute 16 bit results and flags; the 32 bit forms compute 32 bit results. CALL and RET move 16 bit return addresses, and PUSH and POP move 16 bits.

| Opcode | Mnemonic | Bytes | Description |
|---|---|---|---|
| 0x00 | NOP | 1 | no operation |
| 0x01 | LOAD r, imm16 | 4 | load immediate 16 bit value into register |
| 0x02 | ADD r, imm16 | 4 | add immediate 16 bit value to register, 16 bit result |
| 0x03 | STORE r, addr16 | 4 | store low 16 bits of register at a 16 bit address |
| 0x04 | JMP addr16 | 3 | unconditional jump |
| 0x05 | JZ r, addr16 | 4 | jump if the register is zero |
| 0x06 | AND r, imm16 | 4 | bitwise AND with immediate |
| 0x07 | OR r, imm16 | 4 | bitwise OR with immediate |
| 0x08 | XOR r, imm16 | 4 | bitwise XOR with immediate |
| 0x09 | NOT r | 2 | bitwise NOT of the low 16 bits |
| 0x0A | CALL addr16 | 3 | push 16 bit return address and jump |
| 0x0B | RET | 1 | pop 16 bit return address |
| 0x0C | IN r, port | 4 | read an I/O port |
| 0x0D | OUT r, port | 4 | write an I/O port |
| 0x0E | SUB r, imm16 | 4 | subtract immediate, 16 bit result |
| 0x0F | CMP r, imm16 | 4 | compare register with immediate, set flags, discard result |
| 0x10 | JC addr16 | 3 | jump if carry |
| 0x11 | JN addr16 | 3 | jump if sign (negative) |
| 0x12 | JO addr16 | 3 | jump if overflow |
| 0x13 | JNZ addr16 | 3 | jump if Z is clear |
| 0x14 | SHL r, n | 4 | shift left by immediate count |
| 0x15 | SHR r, n | 4 | shift right by immediate count |
| 0x16 | MUL r, imm16 | 4 | multiply by immediate |
| 0x17 | DIV r, imm16 | 4 | divide by immediate, divide fault on zero |
| 0x18 | INT n | 2 | software interrupt |
| 0x19 | IRET | 1 | return from interrupt, restore flags and PC |
| 0x1A | CLI | 1 | clear interrupt enable |
| 0x1B | STI | 1 | set interrupt enable |
| 0x20 | LOAD32 r, imm32 | 6 | load immediate 32 bit value |
| 0x21 | STORE32 r, addr32 | 6 | store register at a 32 bit address |
| 0x22 | ADD32 r, imm32 | 6 | add immediate |
| 0x23 | SUB32 r, imm32 | 6 | subtract immediate |
| 0x24 | MUL32 r, imm32 | 6 | multiply by immediate |
| 0x25 | DIV32 r, imm32 | 6 | unsigned divide by immediate |
| 0x26 | SHL32 r, n | 6 | shift left |
| 0x27 | SHR32 r, n | 6 | logical shift right |
| 0x28 | AND32 r, imm32 | 6 | bitwise AND |
| 0x29 | OR32 r, imm32 | 6 | bitwise OR |
| 0x2A | XOR32 r, imm32 | 6 | bitwise XOR |
| 0x2B | CMP32 r, imm32 | 6 | compare with immediate (Z, C and S only) |
| 0x30 | MOV rd, rs | 2 | copy register |
| 0x31 | ADD rd, rs | 2 | add registers, 16 bit result |
| 0x32 | SUB rd, rs | 2 | subtract registers, 16 bit result |
| 0x33 | CMP rd, rs | 2 | compare registers, 16 bit flags |
| 0x34 | AND rd, rs | 2 | bitwise AND, 32 bits |
| 0x35 | OR rd, rs | 2 | bitwise OR, 32 bits |
| 0x36 | XOR rd, rs | 2 | bitwise XOR, 32 bits |
| 0x37 | MUL rd, rs | 2 | multiply registers, 16 bit result |
| 0x38 | DIV rd, rs | 2 | divide registers, 16 bit result |
| 0x39 | PUSH r | 2 | push low 16 bits |
| 0x3A | POP r | 2 | pop 16 bits |
| 0x3B | LOAD rd, [rb + off] | 4 | load 16 bits from base plus signed 16 bit offset |
| 0x3C | STORE [rb + off], rs | 4 | store 16 bits at base plus offset |
| 0x3D | LOAD32 rd, [rb + off] | 4 | load 32 bits from base plus offset |
| 0x3E | STORE32 [rb + off], rs | 4 | store 32 bits at base plus offset |
| 0x3F | SYSCALL | 1 | enter the supervisor (vector 48 in extended mode) |
| 0x40 | SYSRET | 1 | return from a system call (like IRET in extended mode) |
| 0xFF | HALT | 1 | stop the machine |

## EXTENDED INSTRUCTIONS

These instructions use the formats under INSTRUCTION FORMATS. Their flags follow one set of rules. Z is set for a zero result and S copies bit 31. Additions set C on unsigned carry and V on signed overflow; subtractions, including NEG, set C on unsigned borrow. Logic operations clear C and V. Shifts and rotates use the count modulo 32; a count of zero leaves the value, C and V alone, otherwise C holds the last bit shifted out and V is cleared. IMUL sets C and V when the signed product does not fit in 32 bits. Divisions clear C and V and raise a divide error on division by zero or on dividing 0x80000000 by -1. Loads, stores, moves, LEA, jumps and calls leave the flags unchanged.

Control transfer:

| Op | Instruction | Fmt | Operation |
|---|---|---|---|
| 0x41 | JMPR rs | R | PC = rs |
| 0x42 | CALLR rs | R | push return address, PC = rs |
| 0x43 | JMPREL label | I | relative jump |
| 0x44 | CALLREL label | I | relative call |
| 0x45 to 0x52 | Bcc label | I | relative branch on condition, below |
| 0x53 | JMP32 addr | I | absolute jump |
| 0x54 | CALL32 addr | I | absolute call |
| 0x86 | RET32 | N | pop 32 bit return address |

The new calls push a 32 bit return address, to be popped by RET32. The branch conditions are

| Op | Branch | Taken when |
|---|---|---|
| 0x45 | BEQ | Z |
| 0x46 | BNE | not Z |
| 0x47 | BLT | S != V (signed less) |
| 0x48 | BGE | S == V |
| 0x49 | BLE | Z or S != V |
| 0x4A | BGT | not Z and S == V |
| 0x4B | BLO | C (unsigned lower) |
| 0x4C | BHS | not C |
| 0x4D | BLS | C or Z |
| 0x4E | BHI | not C and not Z |
| 0x4F | BMI | S |
| 0x50 | BPL | not S |
| 0x51 | BVS | V |
| 0x52 | BVC | not V |

Arithmetic and logic on registers (R format, rd is A, rs is B):

| Op | Instruction | Operation |
|---|---|---|
| 0x55 | ADC rd, rs | rd + rs + C |
| 0x56 | SBB rd, rs | rd minus rs minus C |
| 0x57 | SARV rd, rs | arithmetic shift right by rs |
| 0x58 | SHLV rd, rs | shift left by rs |
| 0x59 | SHRV rd, rs | logical shift right by rs |
| 0x5A | ROLV rd, rs | rotate left by rs |
| 0x5B | RORV rd, rs | rotate right by rs |
| 0x5C | IMUL rd, rs | signed multiply, low 32 bits |
| 0x5D | IDIV rd, rs | signed divide, truncating toward zero |
| 0x5E | UMOD rd, rs | unsigned remainder |
| 0x5F | IMOD rd, rs | signed remainder, sign of the dividend |
| 0x60 | NEG rd | 0 minus rd |
| 0x61 | TEST rd, rs | flags of rd AND rs, result discarded |
| 0x62 | MULHU rd, rs | high 32 bits of the unsigned product |
| 0x63 | MULHS rd, rs | high 32 bits of the signed product |
| 0x89 | ADD32 rd, rs | 32 bit add |
| 0x8A | SUB32 rd, rs | 32 bit subtract |
| 0x8B | CMP32 rd, rs | 32 bit compare |
| 0x8C | MUL32 rd, rs | unsigned multiply, C and V set if the high half is not zero |
| 0x8D | DIV32 rd, rs | unsigned divide |

`AND32`, `OR32` and `XOR32` with two register operands assemble to the legacy opcodes 0x34 to 0x36, which already work on 32 bits.

Arithmetic with an immediate (RI format, rd is A):

| Op | Instruction | Operation |
|---|---|---|
| 0x64 | SAR rd, n | arithmetic shift right |
| 0x65 | ROL rd, n | rotate left |
| 0x66 | ROR rd, n | rotate right |
| 0x67 | ADCI rd, imm | add with carry |
| 0x68 | SBBI rd, imm | subtract with borrow |
| 0x69 | IMULI rd, imm | signed multiply |
| 0x6A | IDIVI rd, imm | signed divide |
| 0x6B | UMODI rd, imm | unsigned remainder |
| 0x6C | IMODI rd, imm | signed remainder |
| 0x6D | TESTI rd, imm | flags of rd AND imm |

Memory and stack:

| Op | Instruction | Fmt | Operation |
|---|---|---|---|
| 0x6E | LDB rd, [rb + d] | M | load byte, zero extended |
| 0x6F | LDBS rd, [rb + d] | M | load byte, sign extended |
| 0x70 | LDH rd, [rb + d] | M | load 16 bits, zero extended |
| 0x71 | LDHS rd, [rb + d] | M | load 16 bits, sign extended |
| 0x72 | LDW rd, [rb + d] | M | load 32 bits |
| 0x73 | STB [rb + d], rs | M | store low byte |
| 0x74 | STH [rb + d], rs | M | store low 16 bits |
| 0x75 | STW [rb + d], rs | M | store 32 bits |
| 0x76 | LEA rd, [rb + d] | M | rd = rb + d |
| 0x77 | LEAPC rd, label | I | rd = address of label, PC relative |
| 0x78 | MOVSP rd | R | rd = SP |
| 0x79 | SETSP rs | R | SP = rs |
| 0x7A | ADDSP imm | I | SP = SP + imm |
| 0x83 | PUSHF | N | push flags |
| 0x84 | POPF | N | pop flags; in user mode I and U are kept |
| 0x87 | PUSH32 rs | R | push 32 bits |
| 0x88 | POP32 rd | R | pop 32 bits |

RAM accesses need no alignment. Device registers must be read and written as aligned 32 bit words.

System:

| Op | Instruction | Fmt | Operation |
|---|---|---|---|
| 0x7C | BRK | N | breakpoint trap, vector 3 |
| 0x7D | MFCR rd, crN | R | read a control register (privileged) |
| 0x7E | MTCR crN, rs | R | write a control register (privileged) |
| 0x7F | TLBFLUSH | N | discard all cached translations (privileged) |
| 0x80 | INVLPG rs | R | discard the translation of one page (privileged) |
| 0x81 | WFI | N | wait for an interrupt (privileged) |
| 0x82 | CPUID rd | R | rd = 2 |
| 0x85 | RDCYC rd | R | rd = low 32 bits of the executed instruction count |

Opcode 0x7B and every other unassigned opcode raises an invalid opcode fault.

## EXCEPTIONS AND INTERRUPTS

In extended mode the vectors are

| Vector | Cause | Kind |
|---|---|---|
| 0 | divide error | fault |
| 3 | BRK | trap |
| 4 | invalid opcode | fault |
| 5 | bus error | fault |
| 6 | double fault | abort |
| 13 | protection fault | fault |
| 14 | page fault | fault |
| 32 to 39 | interrupt lines 0 to 7 | interrupt |
| 48 | SYSCALL | trap |
| 48 to 63 | software interrupts allowed in user mode | trap |

A fault saves the address of the faulting instruction, so the handler may repair the cause and return to retry it; a trap saves the address of the next instruction. On entry from user mode the processor loads SP from KSP and pushes the old SP; then it pushes the flags and the saved PC, records the vector in CAUSE, clears U and I, and jumps to the handler named in the vector table. IRET reverses this, popping SP as well when the restored flags have U set.

A fault while the processor is delivering an exception becomes a double fault, vector 6. A fault while delivering a double fault is a triple fault: the emulator prints `triple fault` and exits with status 3.

## PRIVILEGE

In extended user mode CLI, STI, HALT, IN, OUT, IRET, SYSRET, MFCR, MTCR, TLBFLUSH, INVLPG, WFI, and INT with a vector outside 48 to 63 raise a protection fault. SYSCALL and BRK are allowed.

## ADDRESS TRANSLATION

With PROT set and PG clear, a user address a below LIMIT refers to physical address BASE + a, and any other user address raises a protection fault with the address in FADDR. The supervisor uses physical addresses.

With PG set, addresses are translated through two levels of 4KB pages. Address bits 31 to 22 select an entry of the page directory at PTBASE, bits 21 to 12 an entry of the page table it names, and bits 11 to 0 are the offset. A directory entry holds the frame of a page table and a present bit. A page table entry has these bits:

| Bit | Name | Meaning |
|---|---|---|
| 0 | P | present |
| 1 | W | writable, in either mode |
| 2 | U | accessible from user mode |
| 3 | X | executable, in either mode |
| 4 | A | set by the processor on any access |
| 5 | D | set by the processor on a write |

A failed check raises a page fault, vector 14, with the address in FADDR and an error code in CAUSE bits 8 to 15: bit 0 the page was present, bit 1 the access was a write, bit 2 it came from user mode, bit 3 it was an instruction fetch.

The processor keeps the translations it has made. It forgets them only when CR0 or CR3 is written, on TLBFLUSH, and, for one page, on INVLPG. Software that changes a page table must flush.

## MEMORY MAP

| Range | Content |
|---|---|
| 0x00000000 to RAM size minus 1 | RAM, 1 to 256MB |
| 0x00000100 | legacy interrupt vector table |
| 0x000B8000 | legacy text buffer, 80 by 25 characters |
| 0xF0000000 to 0xF000FFFF | devices, 4KB each |
| 0xFFFF0000 to 0xFFFFFFFF | boot ROM, read only, present with -r |

An access to any other address, a write to the ROM, or a device access that is not an aligned 32 bit word is a bus error.

## HARDWARE DEVICES

| Address | Device | Registers |
|---|---|---|
| 0xF0000000 | interrupt controller | PENDING, MASK, ACK, CLAIM |
| 0xF0001000 | timer | COUNT, PERIOD, CTRL, PRESCALE |
| 0xF0002000 | UART | DATA, STATUS, CTRL |
| 0xF0003000 | disk | SECTOR, COUNT, ADDR, CMD, STATUS, SIZE |
| 0xF0004000 | keyboard | DATA, STATUS, CTRL |
| 0xF0005000 | real time clock | SECONDS |
| 0xF0006000 | framebuffer | WIDTH, HEIGHT, ADDR, CTRL |
| 0xF0007000 | system control | POWEROFF, RAMSIZE, VERSION |
| 0xF0009000 | second disk | SECTOR, COUNT, ADDR, CMD, STATUS, SIZE |

Registers lie at offsets 0, 4, 8 and so on, in the order shown.

The interrupt controller collects eight lines: 0 timer, 1 UART receive, 2 UART transmit, 3 disk, 4 keyboard, 6 second disk. A device sets its PENDING bit; MASK enables lines; writing a mask to ACK clears those pending bits; CLAIM reads the lowest pending enabled line or 0xFFFFFFFF. Before each instruction, in extended mode with I set, the processor takes vector 32 + n for the lowest pending enabled line n. The handler must acknowledge it.

The timer counts ticks of PRESCALE instructions (default 1000) and, when CTRL bit 0 is set and PERIOD is not zero, raises line 0 every PERIOD ticks.

The UART writes the low byte stored in DATA to the host's standard output and returns the next input byte when DATA is read, or 0 if there is none. STATUS bit 0 is set when a byte is waiting, bit 1 (transmit ready) always. CTRL bit 0 enables the receive interrupt and bit 1 the transmit interrupt. Input comes from the host's standard input, put in raw mode when it is a terminal and restored on every exit, or from the file named with -i, whose next byte becomes available as soon as the previous one is read.

The disk transfers COUNT sectors of 512 bytes between sector SECTOR and the physical RAM address ADDR. Writing 1 to CMD reads from the disk and 2 writes to it. STATUS shows busy (bit 0) until 1000 instructions per sector have passed; then the transfer happens at once, done (bit 2) or error (bit 1) is set, and line 3 is raised. Writing STATUS clears done and error. SIZE is the number of sectors. With -D the disk is a host file, written through immediately; otherwise it is 256 sectors of memory. A second controller with the same registers lives at 0xF0009000 and raises line 6; it is backed by the file named with -E, and without one its SIZE reads 0.

The real time clock returns host time in seconds, or, with -t, executed instructions divided by ten million.

The framebuffer (640 by 480 pixels of 32 bits, 0x00RRGGBB, at ADDR, shown while CTRL bit 0 is set, with the text buffer drawn over it when bit 1 is set) and the keyboard exist only in the build made with `make SDL=1`. In the default build they read as zero and ignore writes.

The system control device exits the emulator with the value written to POWEROFF and reports the RAM size and the version, 2.

WFI waits until a line that is enabled in MASK becomes pending. The emulator moves the instruction count forward to the next device event instead of executing anything; if no enabled line can ever become pending it reports `WFI deadlock` and exits with status 4.

## LEGACY PORTS

IN and OUT reach the original devices, which remain available:

| Port | Device |
|---|---|
| 0xFF00 | console data; shares the UART streams, reads 'A' when no input is waiting |
| 0xFF01 | console status, always 3 |
| 0xFF02 | timer: reading counts, writing raises interrupt vector 1 |
| 0xFF03 | pseudo random 16 bit number |
| 0xFF04 | system control: writing halts |
| 0xFF10 | disk sector |
| 0xFF11 | disk command: 1 reads a sector into the buffer, 2 writes it |
| 0xFF12 | disk status, always 1 |
| 0xFF13 | disk buffer address |

The legacy disk ports act on the same disk as the disk device, synchronously.

## EMULATOR

The exit status is 0 after HALT, the value written to POWEROFF, 1 for a fatal emulator error such as a bad option or file, 2 for an invalid opcode or bus error in legacy mode, 3 for a triple fault, 4 for a WFI deadlock and 124 when the -x limit is reached.

A trace line holds the instruction count, the PC, the disassembled instruction and every register the instruction changed:

```
3 0000000A CMP32 R1, 0x0000000A           FLAGS=11
```

A state file holds the registers, flags, control registers, RAM and device registers and the instruction count, but not the disk. It is valid only for the build that wrote it.

The emulator caches decoded instructions for each page of RAM and forgets a page's cache when anything writes to the page, so self-modifying code works. The -n option turns the cache off; results are identical either way.

## DEBUGGER

With -d the emulator shows the next instruction and reads commands:

| Command | Action |
|---|---|
| s or an empty line | execute one instruction |
| c | continue until the breakpoint or a halt |
| b addr | set the breakpoint (hex) |
| r | show registers, flags, mode and control registers |
| cr | show control registers |
| t | show the instruction count |
| d addr n | disassemble n instructions |
| x addr n | dump n bytes |
| save file | save the machine state |
| load file | load a machine state |
| q | quit |

## GDB REMOTE

```
./tortoise -g 1234 prog.bin
```

The emulator waits for a connection. In `gdb`, without loading a file and without setting an architecture, type `target remote localhost:1234`. gdb does not know the TORTOISE registers, so read them raw with `maint packet g`, which returns R0 to R7, PC, SP and FLAGS as 32 bit little endian words. Memory is read with `x/16xb addr`; `maint packet Z0,addr,1` sets a breakpoint and `maint packet c` and `maint packet s` continue and step. The stub understands the packets `?`, `g`, `G`, `m`, `M`, `c`, `s`, `Z0`, `z0`, `k`, `D` and `qSupported`. Breakpoints are kept by the emulator, not written into memory.

## TESTING

```
make check
```

builds and runs the unit, assembler, linker, ROM, operating-system, disk, debugger, C compiler and self-hosting tests. It finishes with `ALL TESTS PASSED` after the test targets have completed. The decoded-instruction cache can be excluded from program execution with

```
make check_nocache
```

which repeats the program-level tests with *-n*. The original assembly programs remain byte-for-byte compatible with the original assembler and emulator.

## ORIGINAL ASSEMBLY TESTS

The programs of the original machine remain in `assembler/` and are run by `make run_asm_test`, or one at a time by the targets named below. `make run_legacy_check` assembles each of them and verifies that the binary and the console output are identical to those of the original assembler and emulator.

| Program | Target | Prints | What it shows |
|---|---|---|---|
| logic.asm | | | AND, OR, XOR and NOT in sequence |
| truth.asm | | | truth tables over the four combinations of two bits |
| bitwise_masking.asm | | | AND and XOR chained |
| or_chain.asm | | | consecutive ORs accumulating bits |
| toggle_bits.asm | | | NOT flipping 16 bits |
| xor_flip_flop.asm | | | XOR against 0xFFFF |
| flags_test.asm | | 111ABCD | JC, JN, JO, JNZ under countdown, overflow and comparison |
| peano.asm | | PPPPPPPP | the predecessor axiom: N decremented to zero for N from 1 to 8 |
| shift_test.asm | run_shift_test | S | SHL, SHR and the carry out of a shift |
| mul_div_test.asm | run_mul_div_test | M | MUL and DIV |
| labels_test.asm | run_labels_test | L | backward and forward references to labels |
| interrupt_test.asm | run_interrupt_test | I | a handler in the legacy vector table, INT 2 and IRET |
| io_test.asm | run_io_test | O | console status, timer and random number ports |
| reg32_test.asm | run_reg32_test | R | 32 bit instructions and memory above 64KB |
| indirect_test.asm | run_indirect_test | N | MOV, register arithmetic, PUSH, POP and [R3 + 4] |
| directives_test.asm | run_directives_test | D | .byte, .word, .dword and .asciiz data |
| privilege_test.asm | run_privilege_test | P | SYSRET to user mode, SYSCALL, and the fault on CLI |
| peripherals_test.asm | run_peripherals_test | V | the text buffer and the legacy disk ports |
| multitask.asm | run_multitask_test | K | state preserved across the timer interrupt |

`make run_debugger_test` drives the debugger with a few commands.

## EXTENDED ASSEMBLY TESTS

Each prints one letter and powers off with status 0 when it succeeds.

| Program | Target | Prints | What it shows |
|---|---|---|---|
| sysctl_test.asm | run_sysctl_test | Y | RAMSIZE and VERSION of the system control device |
| branch_test.asm | run_branch_test | B | every branch condition, taken and not taken |
| alu2_test.asm | run_alu2_test | A | multiword arithmetic, shifts, rotates, signed multiply and divide |
| mem2_test.asm | run_mem2_test | W | byte and halfword loads, SP relative addressing, LEA, LEAPC |
| call2_test.asm | run_call2_test | C | CALLR through a table, CALLREL, CALL32 above 64KB |
| ext_test.asm | run_ext_test | E | every exception vector reached and returned from |
| pic_test.asm | run_pic_test | Q | timer interrupts counted while a loop runs |
| wfi_test.asm | run_wfi_test | Z | WFI waking on the timer |
| prot_test.asm | run_prot_test | G | base and limit protection |
| paging_test.asm | run_paging_test | H | paging, a user page, a page fault repaired and retried |
| uart_test.asm | run_uart_test | U | interrupt driven echo of tests/uart_input.txt |
| dma_test.asm | run_dma_test | F | disk transfers, completion interrupts and errors |

## OTHER TESTS

| Target | Test |
|---|---|
| run_test | unit tests in tests/ for every part of the emulator, assembler and linker |
| run_exit_test | the exit statuses of the emulator |
| run_rom_test | booting a prepared disk and a scripted monitor session |
| run_link_test | two objects linked at 0x8000 and run |
| run_c_test | the C programs in tests/c, which print OK |
| run_cc_errors | diagnostics of the compiler |
| run_tdisk_test | creating, listing and extracting disk images |
| run_tdisk2_test | TFS2 modes, owners, links, triple indirect files and reading TFS1 |
| run_os_test | a scripted shell session compared with tests/os/expected.txt |
| run_os_tests | every session tests/os/NAME.in, on a fresh 32MB image with the extra files of NAME.files, compared with NAME.expected |
| run_gdb_test | a scripted exchange with the GDB stub |
| run_selfhost_test | compiling and running a program inside the system |

## FILES

| Directory | Contents |
|---|---|
| src, include | the emulator |
| assembler | the assembler and the assembly test programs |
| tools | the linker and tdisk |
| cc | the C compiler |
| lib | the C library |
| rom | the boot ROM |
| os/boot, os/kernel, os/bin | boot sector, kernel and programs |
| os/test | test programs installed as /test/NAME in the session images |
| tests | tests and their data |

## LICENSE

Copyright (C) 2026 Ivan Gaydardzhiev. *tortoise* is distributed under the GNU General Public License, version 3 only. See *COPYING*.
