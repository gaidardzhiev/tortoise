tortoise
========

## NAME

tortoise - a self-hosting 32-bit computer system

## SYNOPSIS

```
./tortoise [-d] [-v] [-n] [-t] [-c] [-m MB] [-x N] [-r rom.bin]
           [-D disk.img] [-E disk2.img] [-i file] [-g port]
           [-N none|loop|udp:LPORT:HOST:RPORT] [-M mac] [-W file.pcap]
           [-T file] [-S file] [-L file] [program.bin]
./as [-c] [-I dir] [-o out] file
./tlink [-b] [-M] [-T addr] [-e sym] -o out file.o ... [-l lib.o ...]
./tcc0 [-S|-c] [-bare] [-I dir] [-L libdir] [-D name[=value]] [-o out] file ...
./tdisk -o disk.img [-s sectors] [-b boot.bin] [-k kernel.tex] [entries ...]
./tdisk -l|-ll disk.img
./tdisk -x disk.img /path hostfile
./tswitch [-v] PORT
```

## DESCRIPTION

*tortoise* emulates a 32-bit computer with eight general registers, paged virtual memory, supervisor and user modes, interrupts, and memory-mapped devices, among them two disks and a network controller. A boot ROM loads an operating system from a virtual disk. The system runs processes with private address spaces, copy on write and demand paging, a virtual file system with several file system types, users and permissions, signals, sessions and job control, terminals and pseudo terminals, pipes, Unix and Internet sockets on its own TCP/IP stack, a POSIX style shell, and about ninety programs.

An assembler, linker, C compiler, and C library target the machine. The operating system runs the same assembler, linker, and compiler as ordinary programs; the compiler compiles itself and the other tools there, and the whole system, kernel and programs included, can be rebuilt from its own sources inside the machine with byte for byte the same result as on the host. The host runs the emulator and holds the disk files; compilation and execution inside the emulated system use its own toolchain.

The processor starts in legacy mode, retaining the original 16-bit interrupt conventions and assembly programs. Setting EXT in CR0 selects extended mode, which uses a separate exception vector table, takes interrupts from the interrupt controller, and permits address translation and protection. Instructions introduced after the original instruction set, including 32-bit arithmetic and control flow, work in both modes. The processor counts executed instructions to drive timers, disk transfers and network transmission; *-t* also derives the real-time clock from the instruction count, so that a session runs the same way every time.

## BUILDING

A C compiler and make are needed for the first build. The Makefile selects gcc, musl-gcc, tcc, or clang, in that order.

```
make
```

builds *tortoise*, the assembler *as*, the linker *tlink*, the switch *tswitch*, the C compiler *tcc0*, the libraries, and the unit tests. The operating-system image is built with

```
make os
```

which assembles the ROM and boot sector, compiles the kernel and programs with *tcc0*, and puts them on *disk.img*. The image with the compiler and the complete sources is built with

```
make srcimg
```

which writes *src.img*; see SELF HOSTING. The framebuffer and keyboard use SDL2 in a separate build:

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

The ROM loads sector 0, the boot sector loads the kernel, and the kernel starts */bin/init*. init runs */etc/rc* and then *getty* on the console, which logs in the user named in */etc/autologin*, or runs *login* when there is none. Text output goes to the terminal running *tortoise*. The shell accepts commands such as `ls`, `cat`, `ps`, `grep` and `sh`, with pipes, redirection, variables, control structures and job control.

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

To connect two machines, start the switch and give each emulator its own MAC address and an image whose */etc/net.conf* names its address:

```
./tswitch 9000 &
./tortoise -m 32 -N udp:9001:127.0.0.1:9000 -M 02:00:00:00:00:0a -r rom/rom.bin -D a.img
./tortoise -m 32 -N udp:9002:127.0.0.1:9000 -M 02:00:00:00:00:0b -r rom/rom.bin -D b.img
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
-N backend  Network backend: none (the default; sent frames vanish), loop
            (sent frames come back), or udp:LPORT:HOST:RPORT (each frame
            is a UDP datagram to HOST:RPORT; datagrams arriving on LPORT
            are received). HOST is a dotted address or localhost.
-M mac      The MAC address of the network controller; the default is
            02:00:00:00:00:01.
-W file     Write every sent and received frame to a pcap file, with
            one microsecond per executed instruction.
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
./tlink [-b] [-M] [-T addr] [-e sym] -o out file.o ... [-l lib.o ...]
```

The linker places the text of all objects at the -T address (default 0), the data at the next 4KB boundary, and the bss after the data. It defines `_etext`, `_edata` and `_end`. The entry point is the -e symbol, `_start` if it exists, or the start of the text. Duplicate global symbols and unresolved references are errors. The output is a TEX1 executable: a 32 byte header (magic, entry, text address, text size, data address, data size, bss size), the text and the data. With -b it is a flat image instead. With -M the linker prints a map of all symbols and their addresses. The output file is made executable.

Objects after -l form a library, as an archive does elsewhere: one of them is linked only when it defines a symbol that an object already linked needs, so a program carries only the parts of the library it uses.

## C COMPILER

```
./tcc0 -S file.c -o file.s
./tcc0 -c file.c -o file.o
./tcc0 [-bare] file.c ... -o prog
./tcc0 -I dir -D name[=val] -L libdir ...
```

*tcc0* compiles C to TORTOISE assembly and runs the assembler and the linker as separate programs, which it finds next to itself. It accepts C without floating point, long long, bitfields, variable length arrays and old-style function definitions: char (signed), short, int and long (32 bits) with their unsigned forms, pointers, arrays, structures, unions, enumerations, typedef, function pointers, every operator and statement, static and extern, full initializers for globals, variadic functions through `<stdarg.h>`, and a preprocessor with #include, #define with arguments, # and ##, #undef, #if, #ifdef, #ifndef, #elif, #else, #endif, defined() and #error. Unsupported constructs are reported with file and line. Global objects of four bytes or more are aligned to four.

The library and its headers are taken from the lib directory next to the compiler, or from the directory given with -L, which must contain `include`, `os` and `bare`. The library objects are passed to the linker after -l.

## CALLING CONVENTION

Arguments are pushed from right to left, four bytes each, and removed by the caller; structures are copied. The result is returned in R0, and a function returning a structure receives a hidden pointer to the result as its first argument. R0 to R3 belong to the caller, R4 to R6 to the callee, and R7 is the frame pointer: a function begins with `PUSH32 R7`, `MOVSP R7`, `ADDSP -frame`, finds its return address at [R7 + 4] and its first argument at [R7 + 8], and ends with `SETSP R7`, `POP32 R7`, `RET32`. C names are used unchanged as assembly labels.

With -bare a program is linked at address 0 for the bare machine; without it, at 0x00400000 for the operating system.

## LIBRARY

The library in `lib/` is built twice from the same sources by `make libc`: for the bare machine in `lib/bare` and for the operating system in `lib/os`, with headers in `lib/include`. Each source file becomes one object.

`<string.h>` has the mem and str functions with strtok, strtok_r, strspn, strcspn, strpbrk, strnlen, strndup, strcasecmp, strncasecmp and strerror, which knows a message for every error number. `<stdlib.h>` has malloc, free, calloc and realloc on a first fit free list, atoi, strtol and strtoul in every base, abs, labs, div, ldiv, qsort, bsearch, rand, srand, exit, atexit, abort, getenv, setenv, unsetenv, putenv, system and mkstemp. `<stdio.h>` has the printf family with %d %i %u %x %X %o %c %s %p %%, the l, h and z sizes, flags, width and precision, also given as `*`; the scanf family with %d %i %u %x %o %s %c %[...] %n %%, widths and `*`; and FILE streams with fopen, fdopen, freopen, tmpfile, fclose, fread, fwrite, fgets, getline, fputs, fgetc, fputc, ungetc, fseek, ftell, rewind, setvbuf, setbuf, fileno and perror.

`<unistd.h>` has the system calls of the operating system and execv, execvp, execl, execlp, execle, getopt, sleep in seconds, usleep, brk, isatty, ttyname, the user and group id calls, gethostname and poweroff. `<dirent.h>` has opendir, readdir, rewinddir and closedir, and getdirent for system call 14. `<time.h>` has time, clock_gettime, gmtime, localtime (always UTC), mktime, strftime with %Y %m %d %H %M %S %a %b %j %%, asctime and ctime. `<fnmatch.h>` and `<glob.h>` match shell patterns with * ? and [...]; glob looks in one directory and sorts what it finds. `<pwd.h>` and `<grp.h>` read the account files and provide initgroups and crypt_t. The socket calls and network functions are described under NETWORKING. The remaining headers are `<ctype.h>`, `<assert.h>`, `<errno.h>`, `<limits.h>`, `<stdarg.h>`, `<stddef.h>`, `<stdint.h>`, `<fcntl.h>`, `<poll.h>`, `<utime.h>`, `<signal.h>`, `<termios.h>`, `<sys/stat.h>`, `<sys/types.h>`, `<sys/wait.h>`, `<sys/mman.h>`, `<sys/time.h>`, `<sys/utsname.h>`, `<sys/times.h>` and `<sys/ioctl.h>`.

The start up code stores the environment in `environ`; execv passes it on and execvp searches PATH. signal installs a handler with SA_RESTART. memcpy and memset are written in assembly (`lib/memops.s`).

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

The disk holds the boot sector in sector 0, the kernel as a TEX1 image from sector 1, and the file system from sector 1024. The boot sector loads the kernel, taking its size from the header, and jumps to it. The kernel is linked at 0x00020000 and runs in supervisor mode with paging on.

At boot the kernel mounts the file systems, sets up the network interfaces, and starts process 1, */bin/init*, with the console as its standard input, output and error. init reads */etc/inittab*, whose lines have the form `id:action:program args`; lines beginning with `#` are comments. It first runs the `sysinit` entries one after another, waiting for each, then starts the `once` and `respawn` entries and starts a `respawn` entry again whenever it ends. Without */etc/inittab* it runs */bin/sh* and restarts it. The images built by make have

```
rc:sysinit:/bin/sh /etc/rc
con:respawn:/bin/getty /dev/console
```

*/etc/rc* configures eth0 from */etc/net.conf* when that file exists; its lines are `addr A`, `netmask M`, `gateway G` and `hostname H`.

getty starts a session, opens its terminal as the controlling terminal and standard input, output and error, and runs */bin/login*. When */etc/autologin* exists it instead runs */bin/sh* at once as the user named in that file, with the ids, groups, HOME and SHELL of its */etc/passwd* entry, USER, LOGNAME and PATH=/bin, in the directory `/`. Every image made by the Makefile has */etc/autologin* with root.

A fault in the kernel prints the registers and powers off with status 99. Everything the kernel prints is also kept in a 16KB log, readable as */dev/kmsg*. If */etc/kernel.conf* contains the word selftest, the kernel tests its heap at boot.

## PROCESS MEMORY

Every process has its own page directory. The first 4MB of every address space map the same physical memory, for the supervisor only; the devices are mapped for the supervisor too. Page tables, kernel stacks, network buffers and the kernel heap come from the memory between the kernel and 4MB. The kernel heap, kmalloc, serves requests of 16 to 2048 bytes from pages divided into power of two classes and larger requests in whole pages. Memory above 4MB is handed out in pages from a bitmap, and each page carries a reference count.

A process consists of regions: text and data, a heap, a stack and any number of anonymous mappings. A program's text and data start at 0x00400000 and are loaded when it starts. The heap follows the data and moves with sbrk. The stack ends at 0x7FFFF000 and may grow to 1MB. Pages of the heap, the stack and private mappings are allocated when they are first touched and are zero.

fork copies no memory. Parent and child share every page; private writable pages are marked read only and copy on write (bit 9 of the page table entry), and the first write to such a page by either process copies it unless no one else refers to it.

mmap creates anonymous mappings, private or shared, placed downward from 0x60000000 unless MAP_FIXED gives the address. Shared mappings are allocated immediately and stay shared between parent and child after fork. munmap removes any part of any region. An access outside every region, or a write to a read only region, is a fault.

## PROCESSES AND SIGNALS

There may be 64 processes, each with an 8KB kernel stack, 32 file descriptors, a current directory, a parent, a process group, a session, a controlling terminal, real, effective and saved user and group ids, up to 16 supplementary groups and a file creation mask. Process numbers increase from 1 and wrap at 32768, skipping numbers still used by a process, group or session. Sleeping processes wait on wait queues; timed sleeps use kernel timers driven by the tick, which is one timer interrupt every 10000 instructions (called 10 ms). The idle loop waits with WFI.

The scheduler runs the runnable processes of the lowest nice value, from -20 to 19, in turn. A process runs for 20 minus nice/2 ticks unless a process of lower nice value becomes runnable; sleeping does not change its nice value. Each process collects user ticks and system ticks, the latter measured with the timer's COUNT register while it executes system calls, and the ticks of the children it has waited for.

Signals are numbered as on other Unix systems: HUP 1, INT 2, QUIT 3, ILL 4, TRAP 5, ABRT 6, BUS 7, FPE 8, KILL 9, USR1 10, SEGV 11, USR2 12, PIPE 13, ALRM 14, TERM 15, CHLD 17, CONT 18, STOP 19, TSTP 20, TTIN 21 and TTOU 22. A set of signals is a 32 bit mask. By default CHLD and CONT are ignored, STOP, TSTP, TTIN and TTOU stop the process, CONT continues it, and every other signal ends it. KILL and STOP cannot be caught, blocked or ignored, and init receives only the signals it catches. Before every return to user mode the kernel delivers the lowest pending signal that is not blocked. For a caught signal it pushes the trap frame, the signal mask to restore, the signal number and the return address `sa_restorer` on the user stack and starts the handler with the signal and `sa_mask` blocked; the library's restorer `__sigtramp` calls sigreturn, which restores the frame and the mask. A frame that cannot be pushed ends the process with SEGV. A system call that sleeps and is interrupted by a caught signal fails with EINTR, or starts again if the handler has SA_RESTART (pause, sleep and poll always fail). A signal that is not caught never interrupts a call.

Faults become signals: a page or protection fault SEGV, an invalid opcode ILL, a divide error FPE, a bus error BUS and BRK TRAP. If the program does not catch the signal, the kernel prints `pid N: fault V at 0xPC` and ends it. A handler may repair the cause, for example by mapping the missing page, and the instruction runs again.

A wait status is the exit code times 256, the signal number for a process ended by a signal, the stop signal times 256 plus 0x7F for a stopped process, and 0xFFFF for a continued one; `<sys/wait.h>` decodes it. A process that ends sends CHLD to its parent and passes its children to init. When the parent ignores CHLD, ended children are removed at once. A process group that loses its last member whose parent is in another group of the same session is orphaned; if it has stopped members they receive HUP and then CONT, and the stop signals TSTP, TTIN and TTOU are discarded in it. When a session leader ends, the foreground group of its terminal receives HUP and CONT.

exec replaces the program. execve passes up to 62 arguments and 64 environment strings, 8KB in all; the old exec, syscall 8, passes no environment. At entry R1 is argc, R2 argv and R3 the environment, which the start up code stores in `environ`. Descriptors marked close on exec are closed, caught signals return to their default action, ignored ones stay ignored and the signal mask is kept. A file that begins with `#!` names an interpreter and at most one argument on a line of up to 128 bytes; the interpreter runs with the argument and the script path in front of the remaining arguments. The set user id and set group id bits of the program set the effective ids.

## NOTES

A note is a string of up to 127 bytes sent to a process, the way Plan 9 interrupts a program. Writing it to /proc/N/note sends it to process N, and writing it to /proc/N/notepg sends it to every process of N's group that the writer may signal.

| No | Call | Effect |
|---|---|---|
| 106 | notify(fn) | installs fn as the note handler, or removes it when fn is 0 |
| 107 | noted(how) | ends the handling of a note: NCONT 0 resumes where the process was, NDFLT 1 takes the default action |

The handler is called as fn(ureg, msg), with the saved registers and the note on the user stack, and leaves through noted; a handler that returns, or a fault while it runs, ends the process. Up to five notes wait while one is being handled; more are dropped. A system call that a note interrupts fails with EINTR. exec removes the handler; fork passes it on.

Notes and signals are kept apart for programs that never call notify: a note written to such a process is delivered as a signal, `interrupt` as INT, `hangup` as HUP, `kill` as KILL, `alarm` as ALRM and any other note as TERM, and signals behave exactly as under PROCESSES AND SIGNALS. When a process has a handler, a signal sent to it arrives as the note `sys: signal NAME`, such as `sys: signal INT`, unless a sigaction handler is set for it, which takes precedence; KILL and STOP are never notes, signals that are ignored stay ignored, and those ignored by default, such as CHLD, are not turned into notes. A fault arrives as `sys: trap: fault read addr=0x00000000` (or write). NDFLT acts as the matching signal would by default: a note named after a signal, or a fault, takes that signal's action, `interrupt` ends the process with INT, `hangup` with HUP, `alarm` with ALRM, and any other note with TERM. The calls are notify and noted in `<plan9.h>`.

## SYSTEM CALLS

A program enters the kernel with SYSCALL, the call number in R0 and arguments in R1 to R5; the result returns in R0, negative on error. The library sets errno and returns -1. The kernel checks that every pointer refers to the caller's own memory.

| No | Call | No | Call |
|---|---|---|---|
| 1 | exit(status) | 44 | kill(pid, sig) |
| 2 | fork() | 45 | sigaction(sig, act, old) |
| 3 | read(fd, buf, n) | 46 | sigprocmask(how, set, old) |
| 4 | write(fd, buf, n) | 47 | sigreturn() |
| 5 | open(path, flags, mode) | 48 | sigpending(set) |
| 6 | close(fd) | 49 | pause() |
| 7 | wait(status) | 50 | alarm(ticks) |
| 8 | exec(path, argv) | 51 | setpgid(pid, pgid) |
| 9 | sbrk(incr) | 52 | getpgid(pid) |
| 10 | getpid() | 53 | setsid() |
| 11 | unlink(path) | 54 | getsid(pid) |
| 12 | mkdir(path, mode) | 55 | fcntl(fd, cmd, arg) |
| 13 | chdir(path) | 56 | ioctl(fd, req, arg) |
| 14 | readdir(fd, dirent) | 57 | poll(fds, n, timeout) |
| 15 | dup(fd) | 58 | mmap(addr, len, prot, flags, fd) |
| 16 | pipe(fds) | 59 | munmap(addr, len) |
| 17 | kill(pid), sends KILL | 60 | mount(source, target, type) |
| 18 | sleep(ticks) | 61 | umount(target) |
| 19 | time() | 62 | execve(path, argv, envp) |
| 20 | shutdown(code), poweroff in the library | 63 | truncate(path, len) |
| 21 | stat(path, st), old form | 64 | ftruncate(fd, len) |
| 22 | procinfo(index, info) | 65 | utime(path, times) |
| 23 | waitpid(pid, status, options) | 66 | uname(buf) |
| 24 | getppid() | 67 | clock_gettime(clock, ts) |
| 25 | lseek(fd, off, whence) | 68 | nice(inc) |
| 26 | fstat(fd, st) | 69 | sethostname(name, len) |
| 27 | dup2(old, new) | 70 to 81 | the socket calls, under NETWORKING |
| 28 | rename(old, new) | 84 | stat2(path, st) |
| 29 | rmdir(path) | 85 | lstat2(path, st) |
| 30 | getcwd(buf, size) | 86 | sync() |
| 31 | link(old, new) | 87 | times(tms) |
| 32 | symlink(target, path) | 88 | setreuid(ruid, euid) |
| 33 | readlink(path, buf, size) | 89 | setregid(rgid, egid) |
| 34 | chmod(path, mode) | 90 | getgroups(n, list) |
| 35 | chown(path, uid, gid) | 91 | setgroups(n, list) |
| 36 | umask(mask) | 92 | fchmod(fd, mode) |
| 37 | access(path, mode) | 93 | fchown(fd, uid, gid) |
| 38 | getuid() | 100 to 104 | the namespace calls, under NAMESPACES |
| | | 105 | errstr(buf, n), under CONTROL FILES |
| | | 106, 107 | notify(fn) and noted(how), under NOTES |
| 39 | geteuid() | | |
| 40 | getgid() | | |
| 41 | getegid() | | |
| 42 | setuid(uid) | | |
| 43 | setgid(gid) | | |

open takes O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_EXCL, O_TRUNC, O_APPEND, O_NONBLOCK, O_DIRECTORY, O_NOFOLLOW and O_CLOEXEC. The mode of open and mkdir counts only when bit 31 is set, as the library does; otherwise new files get 0666 and new directories 0777, before the file creation mask. fcntl duplicates descriptors and reads or sets FD_CLOEXEC, O_APPEND and O_NONBLOCK; a non blocking read or write that would wait fails with EAGAIN. poll waits on pipes, terminals, sockets and files, with a timeout in ticks. unlink, the old call 11, also removes empty directories. stat2, lstat2 and fstat return the POSIX fields device, inode, mode with the file type, links, owner, group, size, three times and blocks; the library's stat, lstat and fstat use them.

waitpid takes a process number, -1 for any child, 0 for the caller's group or minus a group number, and the options WNOHANG 1, WUNTRACED 2 and WCONTINUED 8. kill sends to one process, to the caller's group (0), to all processes it may signal except init (-1) or to a group (below -1); signal 0 only checks. alarm sends ALRM after the given number of ticks and returns the ticks left of the previous alarm. sigprocmask takes SIG_BLOCK 0, SIG_UNBLOCK 1 and SIG_SETMASK 2. uname fills five fields of 32 bytes: TORTOISE, the host name (tortoise until sethostname changes it), the release 3.0, the build date and tortoise. clock_gettime reads clock 0, the boot time from the real time clock plus the time since boot, or clock 1, the time since boot, in seconds and nanoseconds in steps of 10 ms. Both count with the timer's COUNT register, which unlike the tick count loses nothing while the kernel runs with interrupts off; file times come from clock 0. nice lowers or, for the super user, raises the priority and returns the new value. times returns the ticks since boot. The user and group calls are described under USERS AND PERMISSIONS.

## CONSOLE

File descriptors 0, 1 and 2 of init are the console, the first terminal; output goes to the UART. The kernel takes input only while a program reads or polls the console, so typing ahead waits in the UART until it is wanted.

## TERMINALS

The console and the pseudo terminals share one line discipline, controlled with the termios structure (four flag words and 16 control characters) through ioctl TCGETS, TCSETS and TCSETSW, or the library's tcgetattr and tcsetattr. Initially ICRNL turns a carriage return into a newline, OPOST is set without ONLCR so output is sent unchanged, and ICANON, ECHO, ECHOE and ISIG are on.

In canonical mode input is collected a line at a time and echoed. DEL and Control H erase a character, Control U the whole line, and a read returns at the end of a line. Control D ends a read without a newline; at the start of a line it is end of file. Without ICANON a read waits for VMIN characters; VTIME, in tenths of a second (10 ticks), limits the wait for the first character when VMIN is 0 and the gap between characters otherwise; with both 0 a read takes what has arrived. With ISIG, Control C sends INT, Control \ sends QUIT and Control Z sends TSTP to the foreground process group; the character is not echoed and discards the line being typed.

A session leader makes a terminal its controlling terminal with TIOCSCTTY; its children inherit it and `/dev/tty` names it. TIOCGPGRP and TIOCSPGRP (tcgetpgrp, tcsetpgrp) read and set the foreground process group. A process of a background group that reads the terminal receives TTIN, or fails with EIO if it ignores TTIN or its group is orphaned; one that changes the settings or the foreground group receives TTOU unless it ignores it. TIOCGWINSZ reports 25 rows of 80 columns.

A pseudo terminal is a pair. Opening */dev/ptmx* creates one and TIOCGPTN gives its number N; */dev/pts/N* is the terminal side, with the line discipline above. What is written to the master is input for the terminal side, and what the terminal side writes, echo included, is read from the master. When the master closes, the foreground group of the terminal side receives HUP and CONT, reads give end of file and writes fail with EIO; when the terminal side has closed, the master reads end of file. There are eight pairs.

## FILE SYSTEMS

The kernel reaches every file through a virtual file system. A vnode stands for one file of one mounted file system and carries a reference count; open files, current directories and mount points refer to vnodes. Each file system type supplies a table of operations: lookup, create, mkdir, unlink, rmdir, rename, link, symlink, readlink, read, write, readdir, getattr, setattr, truncate, ioctl, poll, release and mknod. A file that is removed while it is open stays readable and writable through its descriptors and disappears at the last close.

Path names are resolved one component at a time through the namespace of the calling process, described under NAMESPACES: a component that names a mount point leads to what is mounted or bound there, and `..` goes back the way the path came in, to the parent of that point. At most 8 symbolic links are followed; more is ELOOP. Every directory on the way must be searchable.

At boot the kernel mounts TFS from disk 0 on `/`, devfs on `/dev`, procfs on `/proc` and a ramfs on `/tmp`, creating those directories if needed. mount and umount, for the super user, attach and detach `tfs` (from `/dev/disk0` or `/dev/disk1`), `ramfs`, `dev` and `proc`. mount adds the file system to the caller's namespace; umount removes it from every namespace. A file system in use cannot be unmounted.

TFS keeps 512 byte blocks. Block 0 is the superblock (magic `TFS2`, sizes, the places of the inode table, bitmap and data, and the counts of free blocks and inodes). 8192 inodes of 128 bytes follow, then the block bitmap and the data. An inode holds a type (file, directory, symbolic link, socket or fifo), link count, permission bits, owner, group, size, access, modification and change times in seconds, twelve direct blocks and single, double and triple indirect blocks, enough for files of about 1GB. A directory is a file of 32 byte entries, an inode number and a name of up to 27 characters; every directory has `.` and `..`, and the root is inode 1. A symbolic link keeps its target in its data. The kernel caches 64 blocks in 16 sets of 4 and writes through to the disk. On every disk the file system starts at sector 1024.

ramfs keeps its files in memory pages, at most a quarter of the user memory, and loses them at shutdown. devfs is a directory of the devices described under DEVICE FILES. procfs is described under PROCESS FILE SYSTEM.

The host program tdisk builds and examines images:

```
./tdisk -o disk.img [-s sectors] [-b boot.bin] [-k kernel.tex] [entries ...]
./tdisk -l disk.img
./tdisk -ll disk.img
./tdisk -x disk.img /path hostfile
```

An entry is `hostfile:/path[:mode[:uid:gid]]` for a file (mode 0644, or 0755 when the host file is executable), `-d /dir[:mode[:uid:gid]]` for a directory or `-L target:/path` for a symbolic link; missing directories on the way are made with mode 0755. -l lists files and sizes, -ll adds mode, owner, group and link count. tdisk writes TFS2 and reads both TFS2 and the older TFS1; -1 writes a TFS1 image.

## NAMESPACES

Every process sees the files through a namespace, a table of entries each naming a directory or file, the place, and the list of directories or files bound or mounted on it. At boot there is one namespace, holding the mounts of `/dev`, `/proc` and `/tmp` and the devices `#e` bound on `/env`, `#s` on `/srv` and `#I` and `#l` on `/net`; every process shares it until it asks for its own, and paths resolve in it exactly as before. A child made by fork shares its parent's namespace, so a change made by one is seen by the other.

| No | Call | Effect |
|---|---|---|
| 100 | rfork(flags) | with RFPROC 0x10 a new process, otherwise a change to the caller; RFNAMEG 0x01 copies the namespace, RFCNAMEG 0x400 starts a clean one holding only the root, RFFDG 0x04 copies the descriptors, RFCFDG 0x1000 starts without any, RFNOWAIT 0x40 makes the child a child of init. RFENVG 0x02 copies the environment group and RFCENVG 0x800 starts an empty one; RFNOTEG 0x08 is accepted, notes of a group going to the process group. fork() is rfork(RFPROC\|RFFDG) |
| 101 | bind9(new, old, flags) | makes old stand for new; new and old must both be directories or both files |
| 102 | mount9(fd, afd, old, flags, aname) | mounts a 9P server reached through fd, described under 9P |
| 103 | unmount(new, old) | removes new from old, or the whole entry when new is null |
| 104 | fd2path(fd, buf, n) | the path under which fd was opened, as the caller's namespace names it; ERANGE if it does not fit |

The flags of bind are MREPLACE 0, MBEFORE 1 and MAFTER 2, any of them or-ed with MCREATE 4. MREPLACE hides old behind new. MBEFORE and MAFTER make old a union directory, putting new in front of or behind what is there. A lookup in a union tries its members in order and takes the first that has the name; a listing gives every name once, from the first member that holds it. A file is created in the member that already has the name, else in the first member bound with MCREATE, and fails with EROFS if there is none. A file system mounted by mount counts as bound with MCREATE.

A bind or mount names the place itself, so a second bind on a place that has an entry changes that entry rather than what it shows. Mount points inside a bound directory are crossed as anywhere else. A path that enters a bound directory and then goes `..` comes back to the parent of the place it entered by, and getcwd, chdir and /proc/N/cwd give the name a directory was reached by. A place with an entry cannot be removed or renamed (EBUSY).

The boot namespace may be changed only by the super user. A set user id or set group id program always runs in the boot namespace: exec moves the process there, keeping its current directory if the same path names one there and taking `/` otherwise. A user who binds a file of their own over /etc/passwd sees it, but su, login and passwd do not, so a private namespace cannot lend a privileged program files of the user's choosing. Plan 9 had no such programs; a change of identity went through the authentication server, whose key file system ran in a namespace of its own, and this rule gives the set user id programs the same independence.

/proc/N/ns gives the namespace of process N as commands that rebuild it, one line per member, in the order the entries were made:

```
mount -t dev dev /dev
mount -t proc proc /proc
mount -t ramfs ramfs /tmp
bind -c '#e' /env
bind -c '#s' /srv
bind '#I' /net
bind -a '#l' /net
bind -bc /nsw /nsu
bind -a /nsv /nsu
bind /etc/motd /etc/hosts
```

The programs are `bind [-b|-a|-c] new old`, `unmount [new] old` and `ns [pid]`, which prints /proc/N/ns of the caller or of pid; mount also takes the form `mount -t type source target` used by these lines. Running the text in a clean namespace (`rfork N`) gives the same namespace and the same text again. The calls are declared in `<plan9.h>`. The library calls 101 bind9, as it calls 102 mount9, because bind is the socket call; no two library functions share a name, and tlink refuses a link in which a symbol of a library file is also defined by another file.

## 9P

The kernel speaks 9P2000, the file protocol of Plan 9, over any descriptor that carries a byte stream both ways: a socket pair, a Unix or TCP socket, a `#|` pipe or a descriptor posted in /srv. The messages are those of the protocol definition (Tversion, Tauth, Tattach, Twalk, Topen, Tcreate, Tread, Twrite, Tclunk, Tremove, Tstat, Twstat, Tflush, their replies and Rerror), little endian, with msize at most 8192.

`mount9(fd, afd, old, flags, aname)` starts a session on fd with Tversion, attaches to the tree aname as the caller's user id written in decimal, and binds the root of that tree on old with the flags of bind. afd must be -1; there is no authentication. A second mount9 of the same descriptor, for instance after opening the same /srv file again, uses the session already open and only attaches again. From then on the mount driver turns every operation below old into messages: a lookup is a Twalk of one name, open is a Twalk that clones the fid and a Topen, read and write are Tread and Twrite of at most msize less 24 bytes, a listing reads the directory and unpacks its stat entries, stat and chmod, chown, utime and truncate are Tstat and Twstat, rename is a Twstat of the name, and removal is Tremove. The qid path is the inode number and the decimal uid and gid strings are the owner. A file with DMSYMLINK 0x02000000 in its mode, the bit 9P2000.u uses, is a symbolic link whose target is its content; creating one is a Tcreate with that bit and a Twrite of the target.

9P2000 has neither hard links nor a way to move a file to another directory, so link fails with EPERM and rename between directories with EXDEV, and a file always shows one link (a directory two). An error reply sets errno from the message (`file does not exist` is ENOENT, `permission denied` EACCES, `file already exists` EEXIST, and so on, otherwise EIO) and errstr gives the message itself. When the server goes away every file of the mount fails with EIO and errstr says `mount driver: server hung up`. A process interrupted while waiting for a reply sends Tflush and gets EINTR; replies that come late are recognised by their tag and dropped. While the rest of a reply is being read, signals wait.

/srv is the device `#s`, bound on /srv in the boot namespace with MCREATE. Creating /srv/name and writing a descriptor number into the new file posts that descriptor; opening /srv/name afterwards gives the posted descriptor itself, whatever the mode, and removing the file drops the post. A post keeps its connection open even when the poster has gone, and a mount keeps working after its post is removed. In /proc/N/ns a mount of a posted descriptor appears as `mount [-b|-a|-c] '#s/name' old [aname]`, which mount accepts; other mounts show the descriptor's name and cannot be made again from the text.

## FILE SERVERS

The library lib9p, `<9p.h>`, holds convS2M, convM2S and sizeS2M, which pack and unpack messages into the structure Fcall, convD2M, convM2D, sizeD2M and nulldir9 for the stat structure Stat9, and serve9, a server loop. A program fills a Srv9 with the descriptors to read and write and with handlers for attach, walk1 (one name), clone, open, create, read, write, remove, stat, wstat, clunk and dirent (the stat of the nth entry of a directory); serve9 keeps the fids, answers Tversion, Tflush and Tauth, checks open modes, packs directory reads from dirent and replies with Rerror when a handler returns a message. readbuf9 answers a read from a buffer. A walk that stops partway leaves the original fid as it was, as the protocol requires.

| Program | Use |
|---|---|
| `ramfs9 [-D] [-s name] [-m mtpt]` | a file system in the memory of a user process: posted as /srv/name, mounted on mtpt with MCREATE, or served on standard input and output; -D prints each message on standard error |
| `exportfs [-D] [-r root] [-d fd] [-s] [-l]` | serves the directory root (`/` by default) of its own namespace, using the ordinary system calls with its own permissions; on standard input and output, on descriptor fd, with -s after reading the directory to serve as a first line and answering `OK`, or with -l on TCP port 17007, one process per call |
| `import [-b\|-a\|-c] host path [mtpt]` | dials port 17007 of host, asks for path and mounts it on mtpt, by default path itself |
| `srv name tcp!host!port` | dials and posts the connection as /srv/name |
| `mount [-b\|-a\|-c] /srv/name old [aname]` | mounts a posted server; the forms of FILE SYSTEMS keep working |

For example

```
rfork n
ramfs9 -s scratch
mount -c /srv/scratch /n/scratch
exportfs -l &
import beta /var/www /n/beta
```

## ENVIRONMENT GROUPS

Besides the environment strings that execve passes and the library keeps in environ, every process belongs to an environment group, a set of named values that the device `#e` serves as one file per variable. The boot namespace binds it on /env with MCREATE. Creating /env/name makes a variable, writing sets its value, removing the file deletes it; a value may hold any bytes, so rc keeps a list as words each followed by a zero byte, and is at most 16384 bytes (EFBIG beyond). There are at most 64 variables in a group. `#e` always shows the group of the process that looks, so one /env serves every group.

fork shares the group, so a change made by a child is seen by its parent. rfork with RFENVG gives a copy and with RFCENVG an empty group; the shell's `rfork e` and `rfork E` do the same for the shell. getenv, setenv, unsetenv, environ and execve behave as POSIX describes and neither read nor change /env.

## DEVICE NAMES

A path that begins with `#` and a letter names a kernel device directly, outside any namespace, as in Plan 9. Such paths work wherever a path does, in particular in bind, so a clean namespace can be furnished with `bind '#c' /dev`. The shell takes `#` for a comment, so the name is quoted. In the boot namespace /dev, /proc and /tmp stay where they were; `#e` is bound on /env, `#s` on /srv and `#I` and `#l` on /net.

| Name | Device |
|---|---|
| `#c` | the devices of /dev, described under DEVICE FILES |
| `#p` | the process file system of /proc |
| `#D` | disk0 and disk1, the same files as /dev/disk0 and /dev/disk1 |
| `#l` | ether0, holding ctl and clone, the network controller, described under /net |
| `#I` | tcp, udp, icmp, cs, arp, ipifc/0 (lo) and ipifc/1 (eth0), each holding ctl, and iproute, described under /net |
| `#\|` | a new pipe at every use, a directory holding data and data1 |
| `#s` | the posted descriptors of /srv, described under 9P |
| `#e` | the environment group of the caller, described under ENVIRONMENT GROUPS |

A pipe of `#|` carries bytes both ways: what is written on data is read from data1 and the other way round. Each end may be opened once or more; once an end is closed for good, reads of the other give end of file and writes fail with EPIPE. A name such as `#|/data` that is opened directly gives a pipe whose other end cannot be reached; bind `#|` somewhere and open both ends there. The mount driver, `#M` in Plan 9, is reached only through mount9. Other letters fail with ENOENT.

## CONTROL FILES

A control file takes one text command per write; a trailing newline is ignored. A command that fails sets errno and a message that errstr returns. Every command does exactly what an existing call does, and the call keeps working.

| File | Commands | The same as |
|---|---|---|
| /dev/consctl | rawon, rawoff, echo on, echo off, isig on, isig off, vmin N, vtime N | TCSETS on the console: rawon clears ICANON and ECHO, rawoff sets them |
| /dev/pts/Nctl | the commands of consctl | TCSETS on /dev/pts/N |
| /proc/N/ctl | kill, stop, start, nice N, hang, close N | kill with KILL, STOP and CONT; setting the nice value; stopping at the next exec; closing descriptor N of the process |
| /net/ether0/ctl | up, down, promiscuous on, promiscuous off | SIOCSIFFLAGS on eth0 with IFF_UP and IFF_PROMISC 0x100 |
| /net/ipifc/N/ctl | add addr mask, remove addr mask, up, down | SIOCSIFADDR and SIOCSIFNETMASK, SIOCSIFFLAGS |
| /net/iproute | add dst mask gw, remove dst mask | SIOCADDRT and SIOCDELRT |

Reading /net/ether0/ctl gives the MAC address as twelve hex digits, /net/ipifc/N/ctl gives `name addr mask up|down`, and /net/iproute gives the routes as /proc/net/route does. The permissions are those of the calls: interfaces and routes are changed by the super user, and a process is controlled by those who may signal it. remove takes the address and mask the interface has, else EINVAL.

| No | Call | Effect |
|---|---|---|
| 105 | errstr(buf, n) | exchanges the caller's error string with the string in buf, so a later errstr gives back what buf held |

The kernel sets the error string when it has a message of its own, such as `unknown control message 'bogus'` or `unknown device '#x/foo'`, and empties it at any other failing call. The library's errstr, in `<plan9.h>` with ERRMAX 128, then fills in strerror(errno).

## PROCESS FILE SYSTEM

procfs, mounted on `/proc`, describes the system in read only text files whose contents are made when they are read. Numbers are decimal unless stated.

| Path | Content |
|---|---|
| meminfo | `MemTotal: N kB`, `MemFree: N kB` (user memory), `KernelPool: N kB`, `KernelPoolFree: N kB` (the low pool), one per line |
| uptime | `ticks seconds.hundredths` |
| loadavg | `runnable processes last-pid` |
| mounts | `source target type` for each mounted file system, in the order of mounting |
| version | `TORTOISE 3.0 date` |
| cpuinfo | `cpuid: 2`, `ram:` the RAM size in bytes, `ticks_per_second: 100` |
| self | a symbolic link to N, the caller's own process number |
| N/status | `Name`, `State` (R running or ready, S sleeping, T stopped, Z zombie), `Pid`, `PPid`, `Pgid`, `Sid`, `Uid` and `Gid` (real and effective), `Nice`, `VmSize` and `VmRSS` in kB, `Utime` and `Stime` in ticks, and the pending, blocked and ignored signals `SigPnd`, `SigBlk`, `SigIgn` as 8 hex digits; each name ends with a colon and a tab |
| N/cmdline | the arguments, each followed by a zero byte |
| N/environ | the environment, each string followed by a zero byte; mode 0400 |
| N/cwd, N/exe | symbolic links to the current directory, under the name it has in the process's namespace, and the program |
| N/ns | the namespace of the process, as described under NAMESPACES |
| N/ctl, N/note, N/notepg | written, not read: control commands, described under CONTROL FILES, and notes, described under NOTES. Mode 0200 |
| N/fdtab | a line `fd mode offset name` for each open descriptor, mode being r, w or rw and name as in N/fd; mode 0400 |
| N/args | the arguments joined by blanks, ending in a newline |
| N/maps | a line `start-end perms kind` for each region, with 8 hex digit addresses, perms such as `rwxp` or `rw-s`, and kind text, data, heap, stack or mmap |
| N/fd/ | a symbolic link for each open descriptor to the path it was opened by, `pipe:[id]` or `socket:[id]` |
| net/dev | `iface rx_frames tx_frames drops` for lo and eth0 |
| net/tcp, net/udp | `local_addr:port remote_addr:port state rx_queued tx_queued pid` for each socket or connection |
| net/unix | `path type state` for each Unix socket |
| net/arp | `addr mac iface` for each resolved address |
| net/route | `dst mask gateway iface` for each route |

A file of /proc is made when it is first read after being opened, and later reads of the same open file continue in that text, so a reader sees one consistent version. The files of process N belong to its real user and group. A listing of /proc holds an entry for every process, zombies included. ps, top and lsof read /proc.

## USERS AND PERMISSIONS

A process has a real, effective and saved user id and group id, up to 16 supplementary groups and a file creation mask (initially 022). New files take the mode asked for less the mask, the effective user id, and the effective group id or, in a directory with the set group id bit, the directory's group. A process with effective user id 0 passes every permission check, except that only files with at least one execute bit can be executed. For others the owner, group or other bits of the file apply. Each directory of a path must be searchable; creating, removing and renaming need write and search permission on the directory, and in a directory with the sticky bit only the owner of a file, the owner of the directory or the super user may remove or rename it. Only the owner or the super user may change a file's mode; only the super user may change its owner, which clears the set user and group id bits, mount or unmount, raise a priority, set the host name, set the groups, configure the network or bind ports below 1024. A signal may be sent when the sender's real or effective user id equals the target's real or saved one, or by the super user. access checks with the real ids. A program with the set user id or set group id bit runs with its owner or group as the effective id. setuid, setgid, setreuid and setregid follow POSIX.

Accounts are kept in */etc/passwd*, with lines `name:hash:uid:gid:comment:home:shell`, and groups in */etc/group*, with lines `name:x:gid:member,member`. An empty hash means no password. Otherwise it is `$t$` followed by 8 hex digits of a 32 bit FNV-1a hash of the user name followed by the password, as computed by the library's crypt_t. This hash is not secure: it is short, fast to compute and easy to reverse by trying passwords, and it protects nothing against anyone who can read */etc/passwd*. The images built by make have root (uid 0, no password), user (uid 1000, group users 100, password `user`) and none (uid 65534, group none 65534, with the hash `*`, which no password matches), and the groups root 0, wheel 10, users 100 and none 65534.

login prompts `login: ` and, for an account with a password, `Password: ` without echo; it sets the ids, groups and environment, changes to the home directory (or `/` if it is missing) and runs the shell, or says `Login incorrect` and asks again. su runs a shell as another user, root by default, asking for that user's password unless the caller is the super user. passwd changes the caller's password, or any account's for the super user; it, su and ping are set user id root.

## NETWORKING

Sockets are files: read, write, close, poll, dup, fcntl with O_NONBLOCK and ioctl work on them. The socket calls are

| No | Call | No | Call |
|---|---|---|---|
| 70 | socket(domain, type, protocol) | 76 | recvfrom(fd, buf, n, flags, addr) |
| 71 | bind(fd, addr) | 77 | shutdown(fd, how) |
| 72 | listen(fd, backlog) | 78 | setsockopt(fd, level, name, val, len) |
| 73 | accept(fd, addr) | 79 | getsockname(fd, addr) |
| 74 | connect(fd, addr) | 80 | getpeername(fd, addr) |
| 75 | sendto(fd, buf, n, flags, addr) | 81 | socketpair(domain, type, protocol, fds) |

with the domains AF_UNIX 1 and AF_INET 2, the types SOCK_STREAM 1, SOCK_DGRAM 2 and SOCK_RAW 3 (ICMP only, for the super user), the flags MSG_PEEK 2 and MSG_DONTWAIT 0x40, and the options SO_REUSEADDR and SO_RCVTIMEO (a receive timeout in ticks) at level SOL_SOCKET and TCP_NODELAY, which is accepted, at IPPROTO_TCP. An address is a `struct sockaddr_in` (family, port, address, all in network byte order, and eight bytes of zero) or a `struct sockaddr_un` (family and a path of up to 107 characters); its length follows from the family. Writing to a stream whose peer has gone raises PIPE and fails with EPIPE.

A Unix socket is bound to a path, where a socket file appears. Stream sockets listen, accept and connect as TCP does; datagram sockets keep their message boundaries and report the sender's path. socketpair gives two connected stream sockets.

The Internet sockets run on a small IPv4 stack. There are two interfaces: lo, 127.0.0.1/8 and always up, and eth0 on the network controller, which starts down and without an address and sends a gratuitous ARP request when it comes up with one. Interfaces and routes are changed through the control files of /net, described under CONTROL FILES, or with ioctl on any socket (SIOCGIFCONF, SIOCGIFADDR, SIOCSIFADDR, SIOCGIFNETMASK, SIOCSIFNETMASK, SIOCGIFFLAGS, SIOCSIFFLAGS with IFF_UP and IFF_PROMISC, SIOCGIFHWADDR, SIOCADDRT and SIOCDELRT, using `struct ifreq` and `struct rtentry`). Up to eight routes are kept and the longest matching prefix wins; the route to eth0's own network follows its address and mask. ARP keeps 16 entries for 6000 ticks and asks three times, 100 ticks apart, holding up to four packets meanwhile. IP packets carry no options and are never fragmented; fragments that arrive are dropped. ICMP answers echo requests and reports unreachable UDP ports, which a sending socket sees as ECONNREFUSED. UDP checks and sets checksums. TCP follows the states of RFC 793, sends the MSS option (1460 on eth0, 4096 on lo), keeps 8KB send and receive buffers and advertises the free space of the latter, retransmits after 100 ticks, doubling up to six times before giving up with ETIMEDOUT, and holds a closed connection in TIME_WAIT for 200 ticks. There is no congestion control, delayed acknowledgement or Nagle algorithm, and segments that arrive out of order are dropped. Initial sequence numbers come from the random generator. Ports from 49152 upward are given to unbound sockets.

The library has the socket calls in `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<sys/un.h>`, `<netdb.h>`, `<net/if.h>` and `<net/route.h>`; htons, htonl, ntohs, ntohl, inet_addr, inet_aton and inet_ntoa; gethostbyname, which reads dotted addresses and */etc/hosts*; and getservbyname, which reads */etc/services*.

## NETWORK SETUP

The network controller of the emulator reaches only other TORTOISE machines: it is connected to nothing, to itself, or through UDP to the switch tswitch. There is no way out to the host's network or the internet.

The loopback interface lo, 127.0.0.1, is always up, so sockets and the network programs work on one machine without any setup:

```
$ httpd -p 80 /var/www &
$ browse http://localhost/
```

Without -N the controller exists but every frame it sends vanishes. With `-N loop` its frames come back to it, which is enough to try eth0 alone:

```
./tortoise -m 32 -N loop -r rom/rom.bin -D disk.img
```

```
$ ifconfig eth0 10.0.0.1 netmask 255.255.255.0 up
$ ping -c 2 10.0.0.1
```

To connect two or more machines, run tswitch on the host and start each emulator with a UDP port of its own, a MAC address of its own and an image of its own; two emulators must never share an image file. Run each tortoise in a terminal of its own:

```
make disk.img
cp disk.img a.img
cp disk.img b.img
./tswitch 9000 &
./tortoise -m 32 -N udp:9001:127.0.0.1:9000 -M 02:00:00:00:00:0a -r rom/rom.bin -D a.img
./tortoise -m 32 -N udp:9002:127.0.0.1:9000 -M 02:00:00:00:00:0b -r rom/rom.bin -D b.img
```

and give each machine an address in its shell:

```
$ ifconfig eth0 10.0.0.1 netmask 255.255.255.0 up
```

on the first and 10.0.0.2 on the second. The control files of /net do the same:

```
$ echo add 10.0.0.1 255.255.255.0 > /net/ipifc/1/ctl
$ echo up > /net/ipifc/1/ctl
```

Then ping, nc, tget, telnet, browse (with httpd running on the other machine), import and listen1 work between them. An address in */etc/hosts* lets the programs use a name instead.

To configure eth0 at every boot, put */etc/net.conf* in the image. */etc/rc* reads it and runs ifconfig, route and hostname; it takes the keys addr, netmask, gateway and hostname, one per line:

```
addr 10.0.0.1
netmask 255.255.255.0
hostname alpha
```

`make run_net2_test` builds two images in this way, with *tests/net2/a.conf* and *tests/net2/b.conf*, and is a complete example. `-W file.pcap` records every frame a machine sends and receives, for tcpdump or Wireshark.

## /net

The devices `#I` and `#l`, bound on /net in the boot namespace, give the network as files, after Plan 9. /net/tcp, /net/udp and /net/icmp each hold a clone file and a directory N for every connection of that protocol. Opening clone makes a connection and gives its ctl file; reading ctl gives N. A connection directory holds

| File | Use |
|---|---|
| ctl | commands, one per write: `connect addr!port[!localport]`, `announce [addr!]port`, `bind addr!port` and `hangup` |
| data | the bytes of the connection, or one datagram per read and write for UDP and ICMP |
| listen | for an announced TCP connection: opening it waits for a call and gives the ctl file of the new connection |
| local, remote | `addr!port` |
| status | the state, beginning with its name: Closed, Listen, Syn_sent, Syn_received, Established, Finwait1, Finwait2, Close_wait, Closing, Last_ack or Time_wait for TCP, Open or Closed otherwise |
| err | the last error of the connection, such as `connection refused` |

A connection lives while one of its files is open. The connections are the sockets of NETWORKING seen another way: a connection made with socket() and connect() appears in /net/tcp/N, and one made through /net is listed by netstat. /net/ipifc/N/ctl, /net/iproute and /net/ether0/ctl are described under CONTROL FILES; /net/arp gives the ARP table as /proc/net/arp does.

/net/cs translates addresses. A write of `net!host!service`, where net is tcp, udp, icmp or net (meaning tcp), host is a dotted address, `*` or a name of /etc/hosts and service a number or a name of /etc/services, followed by a read from the start, gives a line such as `/net/tcp/clone 10.0.0.2!80`. A name it cannot find fails with ENOENT and an errstr such as `cs: can't translate address 'nosuchhost'`.

/net/ether0 also holds a clone file for raw frames, for the super user. Its connection directories hold ctl, where `connect T` chooses the frame type T (decimal, -1 for every type), and data, where a read gives one whole frame of that type as it arrived and a write sends a frame: destination address, any six bytes, the type and the payload.

The library has, in `<net9.h>`, dial(addr, local, dir, cfdp), which translates addr through /net/cs, connects and returns the data descriptor, announce(addr, dir), listen9(dir, newdir), accept9(ctl, dir), reject(ctl, dir, cause), hangup(ctl) and netmkaddr(linear, defnet, defsrv). listen9 and accept9 are the listen and accept of Plan 9, renamed because the socket calls already have those names. `listen1 [-v] addr cmd [args]` announces addr and runs cmd for every call with the connection as its standard input, output and error, and `netstat -n` lists the connections of /net.

## WEB BROWSER

browse is a text web browser in the manner of lynx. It shows HTML and plain text on the terminal, follows `http:` links through /net and opens `file:` paths, and runs nothing from a page: there is no scripting, and script and style elements are dropped unread.

```
browse [url]
browse -dump [-width n] url
```

**Warning.** Do not use browse with servers you do not trust, and do not use it for anything that must stay private. It has been written with care and tested against hostile pages, but it is a small program that has not been audited by anyone else, and it may still have mistakes that a malicious server could use. Everything it sends and receives travels in clear text over plain HTTP, since TORTOISE has no TLS: anyone on the network path can read the pages, see the addresses you visit and change what you receive, and a page that looks like a known site may come from anyone. Never type a password, a key or other secret into a page or an address, never take what a page says as proof of who sent it, and do not run or install what you download with it. Use it on your own machine, on a network you control, and with servers you know, such as httpd on another TORTOISE.

Without -dump it fills the terminal: the title on the first line, the page, the address of the selected link or a message, and a line of keys. Links are numbered as `[n]` in the text and the selected one is shown in reverse video.

| Key | Action |
|---|---|
| Down, Up, j, k | the next or previous link, scrolling as needed |
| Space, +, Page Down; -, b, Page Up | the next or previous page |
| Enter, Right, l | follow the selected link |
| a number, then Enter | follow that link |
| Left, Backspace, u | go back, to the same place on the page |
| g | go to an address; one without `http://` or `file:` is taken as `http://` |
| r | reload |
| = | show the address of the page and its number of lines and links |
| /, n | find text, and find it again |
| ?, h | help |
| q | quit |

The terminal is put in raw mode and given back on q, on an interrupt or on a hangup. Without a url it opens */var/www/index.html*. `-dump` prints the page laid out for the given width (80 by default) and then a list of the links, numbered, as lynx -dump does, and is meant for scripts and tests.

The renderer knows paragraphs, line breaks, headings (`==` and `--` before h1 and h2), lists, nested and numbered, blockquote, pre, hr, tables as cells separated by `|`, definition lists, links, images as their alt text in brackets, frames as links, and form fields as bracketed placeholders; forms cannot be sent. The entities of HTML for ASCII, `&nbsp;`, `&copy;`, `&mdash;` and a few more, and numeric ones, are understood. A request is HTTP/1.0 GET with Host and Connection: close; up to five redirections to `http:` addresses are followed.

What a page sends never reaches the terminal as control characters: the text, the title and every address are reduced to printable ASCII, a UTF-8 character shows as one `?` and other bytes are dropped, so a page cannot move the cursor, change the title of the terminal or ring the bell. browse loads nothing by itself, neither images nor frames nor refresh targets, and keeps no cookies, cache or history on disk. A page from the network cannot open a `file:` address; `javascript:`, `mailto:`, `https:` and other schemes, and addresses with a user name, are shown but not followed. Documents are cut at 1MB, a read waits at most 10 seconds and a transfer a minute, and addresses are limited to 1024 characters; spaces in an address are sent as `%20` and other characters that may not appear in a URL are encoded or left out, so an address cannot add lines to a request. In a listing of a local directory, names holding `<`, `>`, `&` or quotes are left out.

## PROGRAMS AND SHELL

*/bin* holds these programs; each prints its errors on standard error.

| Group | Programs |
|---|---|
| files | ls (-l), cp (-r), mv, ln (-s), rm (-r, -f), mkdir (-p), rmdir, touch, pwd, stat, du (-s), df, find (-name, -type), chmod, chown |
| text | cat (-n), echo (-n), head (-n), tail (-n, -f), grep (-v, -i, -c, -n; . * ^ $ [...]), sort (-r, -n, -u), uniq (-c), tr (-d), cut (-d, -f, -c), tee (-a), wc (-l, -w, -c), od (-x, -c), more, xargs (-n) |
| processes | ps (-e, -f), kill (-N, -NAME, -l; TERM by default), sleep (seconds, or -t ticks), nice (-n), nohup, top, lsof, test and [, true, false, env, printenv, which, basename, dirname |
| system | init, getty, login, su, passwd, id, whoami, hostname, uname (-a, -s, -n, -r, -v, -m), date (+format), uptime, dmesg, mount, umount, bind, unmount, ns, ramfs9, exportfs, import, srv, sync, shutdown, kinstall |
| network | ifconfig, route, arp, netstat, ping, nc, httpd, tget, telnetd, telnet, listen1, browse |
| other | sh, rc, newns, auth/none, hello, and crash, which faults on purpose |

The network programs are ifconfig (`ifconfig [if [addr [netmask mask]] [up|down]]`), route (`route [add|del default|net/bits gw addr]`), arp, netstat (-t, -u, -x, -a), ping (`ping [-c n] host`, printing `64 bytes from A: icmp_seq=N time=T ticks` for each reply and a summary), nc (`nc [-u] host port` and `nc [-u] -l port`, copying standard input to the connection and the connection to standard output), httpd (`httpd [-p port] [dir]`, an HTTP/1.0 server for GET and HEAD with listings of directories, from */var/www* by default), tget (`tget URL [-o file]`, its client), and telnetd (`telnetd [-p port] [-l program]`) with telnet (`telnet host [port]`), which carry a plain TCP stream between a terminal and a program running on a pseudo terminal.

The shell, sh, reads commands from its terminal, from a script named as its first argument (the rest become `$1` onward), or from the string given with `-c`; `-e` and `-x` may come first. It prompts with `$ `, and with `> ` while a command is incomplete.

A simple command is a list of words with optional assignments `name=value` in front and redirections anywhere: `<`, `>`, `>>`, `<>`, `<&n`, `>&n` and `>&-`, each with an optional descriptor number in front, as in `2>err` or `2>&1`. Commands are joined into pipelines with `|`, negated with `!`, combined with `&&` and `||`, and separated by `;`, `&` or newlines; `&` runs the command in the background. The compound commands are `if ... then ... elif ... else ... fi`, `while` and `until ... do ... done`, `for name in words; do ... done`, `case word in pattern|pattern) ... ;; esac`, `{ ...; }` and `( ... )`, which runs in a subshell. `name() command` defines a function; its arguments are its positional parameters, and `return` leaves it. `#` starts a comment.

Single quotes keep everything literal, double quotes allow `$` expansion and the escapes `\$`, `\"`, `\\` and backslash newline, and a backslash outside quotes takes the next character literally. `$name` and `${name}` give a variable; `$?` the status of the last command, `$$` the shell's process number, `$!` that of the last background command, `$#` the number of arguments, `$0` to `$9` the arguments, `$*` and `$@` all of them, and `"$@"` each as a separate word. `$(command)` is replaced by the command's output less its trailing newlines. The results of unquoted expansions are split at the characters of IFS (blank, tab and newline by default), and unquoted words with `*`, `?` or `[...]` are replaced by the sorted names they match, if any. A leading `~` is HOME. Exported variables, and those inherited from the environment, are passed to commands.

Commands without a slash are looked up in the directories of PATH (`/bin` when unset); a file that is not an executable program is run as a shell script. A command that cannot be found is reported as `sh: name: not found` with status 127. Statuses are the exit code, or 128 plus the signal number for a command ended by a signal.

The built-in commands are cd (to HOME without an argument, `cd -` to the previous directory), pwd, exit, export, unset, set (`-e` exits on a failing command, `-x` traces commands, `--` sets the arguments), shift, read (with `-r`), true, false, `:`, test and `[`, exec, eval, `.` and source, wait, jobs, fg, bg, kill (also with `%job`), umask, rfork, break, continue and return. `rfork [nNsfFeE]` changes the shell itself, with the letters of rc: n copies the namespace and N starts a clean one, f copies the descriptors and F closes them, e copies the environment group and E starts an empty one; s is accepted and changes nothing, since notepg addresses the process group. With no letters it is `rfork ens`.

An interactive shell on a terminal does job control. It ignores INT, QUIT, TSTP, TTIN and TTOU. Each pipeline runs in its own process group, and a foreground job gets the terminal until it ends or stops. Starting a background job prints its process number as `[pid]`. Before each prompt the shell reports finished and stopped jobs as `[n]+ Done cmd`, `[n]+ Exit N cmd`, `[n]+ Stopped cmd` or the name of the signal that ended the job. `+` marks the current job and `-` the previous one; a job is named as `%n`, `%%`, `%+`, `%-` or by the start of its command. A foreground command that ends with status 128 or more is reported as `sh: pid N terminated, status S`.

## RC

/bin/rc is a second shell, after the Plan 9 shell of the same name; /bin/sh stays the shell of login and of every script that names it. rc runs a script named as its first argument, with the rest as `$*` and `$1` onward, the commands given with `-c`, or commands from its standard input, prompting with `% ` and with a tab while a command is incomplete when that is a terminal (or with `-i`).

Every value is a list of words. `x=(a b c)` sets a list and `x=()` empties it; `$x` gives the list, `$#x` its length and `$"x` its words joined by blanks into one word. `^` joins two words, pairing the elements of lists of the same length and distributing a single word over a list, and it is implied where a word and a variable touch, as in `$name.c`. Single quotes quote, a doubled quote standing for a quote; `#` starts a comment. Words with `*`, `?` or `[...]` outside quotes are replaced by the names they match. `` `{cmd} `` gives the output of cmd split at the characters of `$ifs` (blank, tab and newline by default), and `<{cmd}` gives the name of a file holding the output of cmd.

Commands are joined by `|`, with `|[n]` and `|[n=m]` choosing the descriptors, by `&&` and `||`, negated with `!`, and ended by a newline, `;` or `&`, which runs the command in the background and leaves its process number in `$apid`. Redirections are `<`, `>`, `>>`, `>[n] file`, `>[n=m]` (n becomes a copy of m) and `>[n=]` (n is closed). The compound commands are `{ list }`, `if(list) cmd`, `if not cmd` (run when the last if's condition failed), `for(name in words) cmd` and `for(name) cmd` over `$*`, `while(list) cmd`, and `switch(word) { case patterns ... }`, which runs the commands after the first case whose patterns match. `fn name { list }` defines a function, whose arguments are its `$*`, and `fn name` removes it. `~ subject patterns` succeeds when the subject matches a pattern. A command succeeds when `$status` is empty; it holds the exit code of a program that failed, `sigN` for one ended by signal N, or a message.

The built-in commands are cd (to `$home` without an argument), exit, wait (for one process or all), whatis (a variable's value in rc syntax, a function, a built-in or a program's path), `.` (run a file in this shell, with arguments), rfork with the letters of sh's rfork, shift and eval. bind, mount, unmount and ns are the programs.

Variables live in /env: rc reads every file of /env when it starts, as a list of words each followed by a zero byte, and writes a variable there whenever it sets one, so a child rc sees them. It also takes the environment strings it was given that /env does not have, and passes every variable to the programs it runs as `name=words joined by blanks`. `$path`, the directories searched for programs, starts as `$PATH` split at colons, or `. /bin`; `$home` and `$user` start as `$HOME` and `$USER`. `$pid` is rc's process number and `$status` the result of the last command.

## NAMESPACE FILES

A namespace file describes a namespace as commands, one per line: `bind [-b|-a|-c] new old`, `mount [-b|-a|-c] /srv/name old [aname]` and `mount -t type source target`, `unmount [new] old`, `cd dir`, `. file` to include another file, and `clear`, which starts from a clean namespace. A line whose first word begins with `#` is a comment; words may be quoted with single quotes, and `$user` and `$home` stand for the user and the user's home directory. The library's addns(user, file), in `<plan9.h>`, applies a file to the caller's namespace.

login and getty, after checking the user and before giving up the super user's identity, make a private copy of the namespace and apply */lib/namespace* when it exists. The file shipped in the images binds `#e`, `#s`, `#I` and `#l` as the boot namespace has them, so a login sees the same tree, in a namespace of its own: a bind made by the user is not seen by other logins.

`newns [-n file] [-u user] [cmd args]` runs cmd, or rc, in a private namespace built by applying file (*/lib/namespace* by default) for user (the caller by default). `auth/none [cmd args]`, for the super user, runs cmd, or rc, as the user none (uid and gid 65534, which cannot log in) with a private namespace built from */lib/namespace* and a copy of the environment group.

## SELF HOSTING

`make selfhost.img` builds an image that also holds the assembler, the linker and tcc0, compiled by tcc0, with the library in */lib*. The resulting image boots through the same ROM and contains the compiler, assembler, linker, headers, and libraries. A program compiled with *tcc0* from the shell runs as another process in the operating system. See RUNNING for the commands.

`make srcimg` builds *src.img*, a 128MB image with the whole system, the library objects of both variants, and the sources of the library, assembler, linker, tcc0, kernel, boot sector and programs under */usr/src*, laid out as in the project. */usr/src/build.sh* rebuilds everything into */usr/obj* in this order: the library, as and tlink with the installed tcc0, tcc0 twice (first with the installed tcc0, then with the one just built), the kernel and boot sector with the new tools, and every program. The kernel's build date is read from */usr/src/os/kernel/build.date*, so the result is the same on any day, and it is byte for byte the same as the host build. `kinstall kernel.tex` writes a TEX1 kernel to sectors 1 to 1023 of */dev/disk0*, where the boot sector finds it at the next boot:

```
$ cd /usr/src
$ sh build.sh
$ kinstall /usr/obj/kernel/kernel.tex
```

The rebuild executes about 55.4 billion instructions and takes some 27 minutes on an ordinary machine.

build.sh also runs in a private namespace with */usr/obj* on a file server, and with the programs, tools and library it has just built bound over */bin* and */lib*:

```
$ cd /usr/src
$ rfork n
$ ramfs9 -m /usr/obj
$ sh build.sh
$ bind -b /usr/obj/tools /bin
$ bind -b /usr/obj/bin /bin
$ bind -b /usr/obj/lib /lib
$ unmount /usr/obj
$ sh build.sh
```

The first build goes to ramfs9 over 9P; the binds keep the files it made after /usr/obj is unmounted, and the second build, made with them, goes to the disk. Both are the same as the host build.

## COMPILER SELF COMPILATION

tcc0 is written in the C it accepts, so it compiles itself. The sources are cc/main.c, cc/pp.c, cc/parse.c and cc/gen.c with cc/cc.h; the host build makes *tcc0*, which runs on the host, and with it *os/tools/tcc0*, which runs on TORTOISE. To try it by hand, build the image, which holds the selfhost image and the compiler sources under */src/cc*, and boot it with enough memory:

```
make selfcompile.img
./tortoise -m 64 -r rom/rom.bin -D selfcompile.img
```

In the shell of the machine, build a second generation with the installed tcc0, a third with the second, and a program with the third. A new compiler looks for as and tlink next to itself, so they are copied there first:

```
$ mkdir -p /src/stage2 /src/stage3
$ cp /bin/as /bin/tlink /src/stage2
$ cp /bin/as /bin/tlink /src/stage3
$ cd /src/cc
$ tcc0 -L /lib main.c pp.c parse.c gen.c -o /src/stage2/tcc0
$ /src/stage2/tcc0 -L /lib main.c pp.c parse.c gen.c -o /src/stage3/tcc0
$ /src/stage3/tcc0 -L /lib /src/hello.c -o /src/hello
$ /src/hello
$ shutdown 0
```

The two compilations take a few minutes. Back on the host, take the compilers out of the image and compare them with the one the host built; cmp prints nothing when the files are the same:

```
./tdisk -x selfcompile.img /src/stage2/tcc0 stage2.tcc0
./tdisk -x selfcompile.img /src/stage3/tcc0 stage3.tcc0
cmp os/tools/tcc0 stage2.tcc0
cmp stage2.tcc0 stage3.tcc0
```

The image keeps what was built; `rm selfcompile.img` before `make selfcompile.img` gives a fresh one.

`make selfcompile` (also `make run_selfcompile_test`, part of `make check`) does all of this by itself on a copy of the image. It builds a second generation with the installed tcc0 and a third with the second, compiles and runs a test program with the third, and checks on the host that the second generation is byte for byte the *os/tools/tcc0* built by the host and that the third is byte for byte the second, a fixed point. It takes about four minutes and some 7.9 billion instructions.

## DEVICE FILES

`/dev` holds the devices. stat reports them as character devices whose device number is the major number times 256 plus the minor number.

| Name | Major, minor | Device |
|---|---|---|
| console | 4, 1 | the system console |
| tty | 4, 0 | the controlling terminal of the calling process |
| null | 1, 3 | reads give end of file, writes vanish |
| zero | 1, 5 | reads give zero bytes |
| random, urandom | 1, 8 and 1, 9 | xorshift128 numbers seeded from the clock and the MAC at boot |
| kmsg | 1, 11 | the kernel log, mode 0600 |
| disk0, disk1 | 8, 0 and 8, 1 | the disks as arrays of bytes, mode 0600 |
| fb0 | 29, 0 | the framebuffer, 640 by 480 pixels of 4 bytes, mode 0660; without SDL it reads zeros |
| kbd | 13, 0 | keyboard characters, mode 0660 |
| ptmx | 5, 2 | opens a new pseudo terminal pair; TIOCGPTN gives its number N |
| pts/N | 136, N | the terminal side of pair N, described under TERMINALS |
| consctl | 4, 2 | control of the console, described under CONTROL FILES |
| pts/Nctl | | control of pts/N, a plain file |

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
| 0xF0008000 | network | MAC_LO, MAC_HI, CTRL, STATUS, TX_ADDR, TX_LEN, RX_ADDR, RX_LEN, STATS_TX, STATS_RX, STATS_DROP |
| 0xF0009000 | second disk | SECTOR, COUNT, ADDR, CMD, STATUS, SIZE |

Registers lie at offsets 0, 4, 8 and so on, in the order shown.

The interrupt controller collects eight lines: 0 timer, 1 UART receive, 2 UART transmit, 3 disk, 4 keyboard, 5 network, 6 second disk. A device sets its PENDING bit; MASK enables lines; writing a mask to ACK clears those pending bits; CLAIM reads the lowest pending enabled line or 0xFFFFFFFF. Before each instruction, in extended mode with I set, the processor takes vector 32 + n for the lowest pending enabled line n. The handler must acknowledge it.

The timer counts ticks of PRESCALE instructions (default 1000) in COUNT and, when CTRL bit 0 is set and PERIOD is not zero, raises line 0 every PERIOD ticks.

The UART writes the low byte stored in DATA to the host's standard output and returns the next input byte when DATA is read, or 0 if there is none. STATUS bit 0 is set when a byte is waiting, bit 1 (transmit ready) always. CTRL bit 0 enables the receive interrupt and bit 1 the transmit interrupt. Input comes from the host's standard input, put in raw mode when it is a terminal and restored on every exit, or from the file named with -i, whose next byte becomes available as soon as the previous one is read.

The disk transfers COUNT sectors of 512 bytes between sector SECTOR and the physical RAM address ADDR. Writing 1 to CMD reads from the disk and 2 writes to it. STATUS shows busy (bit 0) until 1000 instructions per sector have passed; then the transfer happens at once, done (bit 2) or error (bit 1) is set, and line 3 is raised. Writing STATUS clears done and error. SIZE is the number of sectors. With -D the disk is a host file, written through immediately; otherwise it is 256 sectors of memory. A second controller with the same registers lives at 0xF0009000 and raises line 6; it is backed by the file named with -E, and without one its SIZE reads 0.

The real time clock returns host time in seconds, or, with -t, executed instructions divided by one million, so that a second is a hundred timer ticks of 10000 instructions.

The framebuffer (640 by 480 pixels of 32 bits, 0x00RRGGBB, at ADDR, shown while CTRL bit 0 is set, with the text buffer drawn over it when bit 1 is set) and the keyboard exist only in the build made with `make SDL=1`. In the default build they read as zero and ignore writes.

The system control device exits the emulator with the value written to POWEROFF and reports the RAM size and the version, 2.

The network controller has MAC_LO and MAC_HI (the MAC address, bytes 0 to 3 and 4 to 5, read only), CTRL (bit 0 enable, 1 receive interrupt, 2 transmit interrupt, 3 promiscuous), STATUS (bit 0 a received frame is ready, 1 transmitter busy, 2 link up; writing 1 to bit 0 releases the frame), TX_ADDR and TX_LEN (writing a length of 14 to 1514 bytes sends the frame at that physical address), RX_ADDR (a 1536 byte buffer) and RX_LEN, and the counters STATS_TX, STATS_RX and STATS_DROP. Frames are Ethernet II without the checksum. A transmission takes one instruction per 16 bytes, after which busy clears and line 5 is raised if enabled. Frames addressed to the controller's MAC, to the broadcast address, or to anyone in promiscuous mode wait in a queue of 64; whenever STATUS bit 0 is clear the next one is copied to RX_ADDR, RX_LEN is set, bit 0 is set and line 5 is raised. A frame that finds the queue full or the controller disabled is counted as dropped. Where the frames go and come from is chosen with -N; -W records them in a pcap file that tcpdump reads.

The host program `tswitch [-v] PORT` is a learning Ethernet switch for emulators started with `-N udp:LPORT:127.0.0.1:PORT`: it receives frames as UDP datagrams on PORT, remembers which sender each source MAC came from, and forwards a frame to the sender that owns its destination or, for broadcasts and unknown destinations, to every other sender it knows.

WFI waits until a line that is enabled in MASK becomes pending. The emulator moves the instruction count forward to the next device event instead of executing anything, and with the udp backend waits for datagrams as it waits for input; if no enabled line can ever become pending it reports `WFI deadlock` and exits with status 4.

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

A state file holds the registers, flags, control registers, RAM and device registers and the instruction count, but not the disks or the network controller. It is valid only for the build that wrote it.

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

builds and runs the unit, assembler, linker, ROM, operating-system, disk, debugger, C compiler, network, self-hosting and self-rebuild tests. It finishes with `ALL TESTS PASSED` after the test targets have completed; the two full rebuilds inside the machine make it take about two hours, some 27 minutes for run_rebuild_test and 82 for run_rebuild_ns_test. The decoded-instruction cache can be excluded from program execution with

```
make check_nocache
```

which repeats the program-level tests with *-n*, leaving out the network and rebuild tests. The original assembly programs remain byte-for-byte compatible with the original assembler and emulator.

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

## SESSION TESTS

`make run_os_tests` boots a fresh 32MB image for every file tests/os/NAME.in, made of the system, the test programs of os/test installed as /test/NAME and the extra files listed in NAME.files, and feeds the file to the console with -t. The output must equal NAME.expected. An entry of NAME.files replaces a base file with the same path, NAME.drop lists base paths to leave out, NAME.disk2 describes a second disk, and NAME.opts gives extra emulator options such as `-N loop`. No session may take more than 60 seconds.

| Session | What it shows |
|---|---|
| kmalloc | the kernel heap self test |
| cow, lazy, mmap | copy on write, demand zero pages, private and shared mappings |
| files, poll, bigfile, disk1 | every file call and its errors, poll, triple indirect files, mounting a second disk |
| signals, exec, proc | signals, waitpid, groups, faults as signals, execve and `#!`, nice, clocks and times |
| dev, term, init | every device, terminal line discipline and control characters, inittab |
| procfs | every /proc file for a known process |
| perm, login | the permission matrix, sticky directories, set user id programs, login, su and passwd |
| libc, shell | the library functions that need the kernel; the shell scripts of tests/os/sh and job control |
| progfiles, progtext, progsys | every program of the file, text and process groups |
| sock, pty, net | Unix and Internet sockets, pseudo terminals, the network programs over lo and eth0 |
| ns | private namespaces, unions, every kind of bind, `..` across binds, unmount, fd2path, /proc/N/ns and rebuilding a namespace from it, RFCNAMEG, set user id programs in a private namespace |
| ctl, ctlnet | every device name bound and read, the pipe device, every control file command checked against its ioctl, errstr, notes, /proc/N/ctl against ps; ctlnet runs with `-N loop` |
| p9 | ramfs9 on a socket pair and through /srv, two mounts of one post, a server that dies, exportfs over a socket pair and over TCP on lo with import, rebuilding a namespace with a 9P mount from its text, and fstest run unchanged on ramfs9 |
| env | /env read, written, truncated and appended, its limit, sharing by fork, RFENVG and RFCENVG from C and from the shell, and getenv, setenv and unsetenv untouched by it |
| net9 | /net/cs, TCP through clone, ctl, data and listen with dial, announce, listen9 and accept9, a socket() connection seen in /net/tcp, UDP, the err file, raw frames of ether0, listen1 and netstat -n, with `-N loop` |
| note | notify and noted with NCONT and NDFLT, notes from /proc/N/note and notepg, signals as notes, sigaction taking precedence, faults as notes, followed by the unchanged signal test |
| rc | the rc scripts of tests/os/rc (lists, control flow, functions and `.`, redirections and process substitution, /env, namespaces and exit), an interactive rc, newns with /lib/namespace and with a file of its own, auth/none, and the private namespace of the login shell |
| browse | browse -dump on pages with lists, quotations, pre, tables, entities, images and forms, relative and scheme-relative links, plain text, a local directory, a redirection, a binary document, a missing page and unreachable or unsupported addresses, a hostile page whose escape sequences, scripts and dangerous links are shown harmless, and the keys of the interactive browser |

## OTHER TESTS

| Target | Test |
|---|---|
| run_test | unit tests in tests/ for every part of the emulator, assembler, linker and network controller |
| run_tswitch_test | learning, flooding and forwarding of tswitch with a host client |
| run_exit_test | the exit statuses of the emulator |
| run_rom_test | booting a prepared disk and a scripted monitor session |
| run_link_test | two objects linked at 0x8000 and run |
| run_c_test | the C programs in tests/c, which print OK |
| run_cc_errors | diagnostics of the compiler |
| run_tdisk_test | creating, listing and extracting disk images |
| run_tdisk2_test | TFS2 modes, owners, links, triple indirect files and reading TFS1 |
| run_os_test | a scripted shell session compared with tests/os/expected.txt |
| run_os_tests | the sessions under SESSION TESTS |
| run_gdb_test | a scripted exchange with the GDB stub |
| run_selfhost_test | compiling and running a program inside the system |
| run_net2_test | two machines on tswitch: ping, nc both ways, tget from the other's httpd, and tcpdump reading the -W capture when it is installed |
| run_selfcompile_test | tcc0 compiles itself twice inside the machine; both generations must equal the host-built os/tools/tcc0, as under COMPILER SELF COMPILATION |
| run_rebuild_test | src.img rebuilds the whole system inside the machine; the results must equal the host build, and the kernel installed with kinstall must boot |
| run_rebuild_ns_test | src.img rebuilds the system twice in a private namespace, first onto ramfs9 and then with the new programs and library bound over /bin and /lib, as under SELF HOSTING; every compared file of the second build must equal the host build |

## FILES

| Directory | Contents |
|---|---|
| src, include | the emulator and its network controller |
| assembler | the assembler and the assembly test programs |
| tools | the linker, tdisk and tswitch |
| cc | the C compiler |
| lib | the C library |
| rom | the boot ROM |
| os/boot, os/kernel, os/bin | boot sector, kernel and programs; os/bin/none.c is installed as /bin/auth/none |
| os/etc | the files of /etc in the images, and namespace, installed as /lib/namespace |
| os/build.sh | the rebuild script, /usr/src/build.sh on src.img |
| os/test | test programs installed as /test/NAME in the session images |
| tests | tests and their data |

## LICENSE

Copyright (C) 2026 Ivan Gaydardzhiev. *tortoise* is distributed under the GNU General Public License, version 3 only. See *COPYING*.
