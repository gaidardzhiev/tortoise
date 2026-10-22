# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

export NOCACHE
CC:=$(shell command -v gcc 2>/dev/null || command -v musl-gcc 2>/dev/null || command -v tcc 2>/dev/null || command -v clang 2>/dev/null)
CFLAGS=-Wall -Wextra -std=c11 -g -O2 -static -D_DEFAULT_SOURCE
INCLUDES=-Iinclude
EMU_SRC=src/cpu.c src/dev.c src/net.c src/dis.c src/dbg.c src/opts.c src/emu.c src/gdb.c
SRC=src/main.c $(EMU_SRC)
OBJ=$(SRC:.c=.o)
EMU_OBJ=$(EMU_SRC:.c=.o)
HDR=include/cpu.h include/dev.h include/dis.h include/opts.h
TARGET=tortoise
TEST_SRC=tests/test_cpu.c $(EMU_SRC)
TEST_BIN=test_cpu
UNIT_TESTS=tests/test_p1 tests/test_p2 tests/test_p3 tests/test_p4 tests/test_p5 tests/test_p6 tests/test_p9 tests/test_p19
ASM_SRC=assembler/assembler.c
ASM_BIN=as
NEW_ASM_TESTS=sysctl branch alu2 mem2 call2 ext pic wfi prot paging dma

ifeq ($(strip $(CC)),)
CC=cc
endif

ifeq ($(SDL),1)
CFLAGS=-Wall -Wextra -std=c11 -g -O2 -D_DEFAULT_SOURCE -DTORTOISE_SDL $(shell sdl2-config --cflags)
EMU_SRC+=src/sdl.c
LDLIBS=$(shell sdl2-config --libs)
endif

all: $(TARGET) assembler tlink tswitch tcc0 libc test

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDLIBS)

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test: $(TEST_BIN) $(UNIT_TESTS)

$(TEST_BIN): $(TEST_SRC) $(HDR)
	$(CC) $(CFLAGS) -Iinclude -o $(TEST_BIN) $(TEST_SRC) $(LDLIBS)

tests/test_%: tests/test_%.c $(EMU_OBJ) $(HDR)
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(EMU_OBJ) $(LDLIBS)

run_test: test $(TARGET) assembler tlink
	./$(TEST_BIN) > /dev/null
	for t in $(UNIT_TESTS); do ./$$t || exit 1; done

tlink: tools/tlink.c
	$(CC) $(CFLAGS) -o tlink tools/tlink.c

tswitch: tools/tswitch.c
	$(CC) $(CFLAGS) -o tswitch tools/tswitch.c

tests/tswitch_test: tests/tswitch_test.c
	$(CC) $(CFLAGS) -o tests/tswitch_test tests/tswitch_test.c

run_tswitch_test: tswitch tests/tswitch_test
	./tests/tswitch_test

run_net2_test: tswitch os tests/os/base.files.tmp
	sh tests/net2_test.sh

assembler: $(ASM_BIN)

$(ASM_BIN): $(ASM_SRC)
	$(CC) $(CFLAGS) -o $(ASM_BIN) $(ASM_SRC)

run_shift_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/shift_test.asm assembler/shift_test.bin
	./$(TARGET) assembler/shift_test.bin

run_mul_div_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/mul_div_test.asm assembler/mul_div_test.bin
	./$(TARGET) assembler/mul_div_test.bin

run_labels_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/labels_test.asm assembler/labels_test.bin
	./$(TARGET) assembler/labels_test.bin

run_interrupt_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/interrupt_test.asm assembler/interrupt_test.bin
	./$(TARGET) assembler/interrupt_test.bin

run_io_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/io_test.asm assembler/io_test.bin
	./$(TARGET) assembler/io_test.bin

run_reg32_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/reg32_test.asm assembler/reg32_test.bin
	./$(TARGET) assembler/reg32_test.bin

run_indirect_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/indirect_test.asm assembler/indirect_test.bin
	./$(TARGET) assembler/indirect_test.bin

run_directives_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/directives_test.asm assembler/directives_test.bin
	./$(TARGET) assembler/directives_test.bin

run_privilege_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/privilege_test.asm assembler/privilege_test.bin
	./$(TARGET) assembler/privilege_test.bin

run_peripherals_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/peripherals_test.asm assembler/peripherals_test.bin
	./$(TARGET) assembler/peripherals_test.bin

run_multitask_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/multitask.asm assembler/multitask.bin
	./$(TARGET) assembler/multitask.bin

run_debugger_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/shift_test.asm assembler/shift_test.bin
	printf "r\ns\nq\n" | ./$(TARGET) -d assembler/shift_test.bin

run_asm_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/logic.asm assembler/logic.bin
	./$(TARGET) assembler/logic.bin
	./$(ASM_BIN) assembler/truth.asm assembler/truth.bin
	./$(TARGET) assembler/truth.bin
	./$(ASM_BIN) assembler/bitwise_masking.asm assembler/bitwise_masking.bin
	./$(TARGET) assembler/bitwise_masking.bin
	./$(ASM_BIN) assembler/or_chain.asm assembler/or_chain.bin
	./$(TARGET) assembler/or_chain.bin
	./$(ASM_BIN) assembler/toggle_bits.asm assembler/toggle_bits.bin
	./$(TARGET) assembler/toggle_bits.bin
	./$(ASM_BIN) assembler/xor_flip_flop.asm assembler/xor_flip_flop.bin
	./$(TARGET) assembler/xor_flip_flop.bin
	./$(ASM_BIN) assembler/flags_test.asm assembler/flags_test.bin
	./$(TARGET) assembler/flags_test.bin
	./$(ASM_BIN) assembler/peano.asm assembler/peano.bin
	./$(TARGET) assembler/peano.bin
	./$(ASM_BIN) assembler/shift_test.asm assembler/shift_test.bin
	./$(TARGET) assembler/shift_test.bin
	./$(ASM_BIN) assembler/mul_div_test.asm assembler/mul_div_test.bin
	./$(TARGET) assembler/mul_div_test.bin
	./$(ASM_BIN) assembler/labels_test.asm assembler/labels_test.bin
	./$(TARGET) assembler/labels_test.bin
	./$(ASM_BIN) assembler/interrupt_test.asm assembler/interrupt_test.bin
	./$(TARGET) assembler/interrupt_test.bin
	./$(ASM_BIN) assembler/io_test.asm assembler/io_test.bin
	./$(TARGET) assembler/io_test.bin
	./$(ASM_BIN) assembler/reg32_test.asm assembler/reg32_test.bin
	./$(TARGET) assembler/reg32_test.bin
	./$(ASM_BIN) assembler/indirect_test.asm assembler/indirect_test.bin
	./$(TARGET) assembler/indirect_test.bin
	./$(ASM_BIN) assembler/directives_test.asm assembler/directives_test.bin
	./$(TARGET) assembler/directives_test.bin
	./$(ASM_BIN) assembler/privilege_test.asm assembler/privilege_test.bin
	./$(TARGET) assembler/privilege_test.bin
	./$(ASM_BIN) assembler/peripherals_test.asm assembler/peripherals_test.bin
	./$(TARGET) assembler/peripherals_test.bin
	./$(ASM_BIN) assembler/multitask.asm assembler/multitask.bin
	./$(TARGET) assembler/multitask.bin

run_sysctl_test: $(TARGET) assembler
	sh tests/runasm.sh sysctl Y

run_branch_test: $(TARGET) assembler
	sh tests/runasm.sh branch B

run_alu2_test: $(TARGET) assembler
	sh tests/runasm.sh alu2 A

run_mem2_test: $(TARGET) assembler
	sh tests/runasm.sh mem2 W

run_call2_test: $(TARGET) assembler
	sh tests/runasm.sh call2 C

run_ext_test: $(TARGET) assembler
	sh tests/runasm.sh ext E

run_pic_test: $(TARGET) assembler
	sh tests/runasm.sh pic Q

run_wfi_test: $(TARGET) assembler
	sh tests/runasm.sh wfi Z

run_prot_test: $(TARGET) assembler
	sh tests/runasm.sh prot G

run_paging_test: $(TARGET) assembler
	sh tests/runasm.sh paging H

rom: rom/rom.bin

rom/rom.bin: rom/rom.asm $(ASM_BIN)
	./$(ASM_BIN) rom/rom.asm rom/rom.bin

run_rom_test: $(TARGET) rom
	sh tests/rom_test.sh

run_link_test: $(TARGET) assembler tlink
	sh tests/link_test.sh

tests/gdb_client: tests/gdb_client.c
	$(CC) $(CFLAGS) -o tests/gdb_client tests/gdb_client.c

run_gdb_test: $(TARGET) assembler tests/gdb_client
	sh tests/gdb_test.sh

run_c_test: $(TARGET) tcc0 libc
	sh tests/c_test.sh

run_cc_errors: tcc0 libc
	sh tests/cc_errors.sh

run_dma_test: $(TARGET) assembler
	sh tests/runasm.sh dma F

run_uart_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/uart_test.asm assembler/uart_test.bin > /dev/null
	./$(TARGET) $(NOCACHE) -i tests/uart_input.txt assembler/uart_test.bin > tests/uart_out.tmp
	printf "Hello, TORTOISE!\nU" | cmp - tests/uart_out.tmp
	rm -f tests/uart_out.tmp
	@echo "uart: U ok"

run_new_asm_test: $(TARGET) assembler
	for t in $(NEW_ASM_TESTS); do $(MAKE) -s run_$${t}_test || exit 1; done

run_legacy_check: $(TARGET) assembler
	sh tests/check_legacy.sh

run_exit_test: $(TARGET) assembler
	sh tests/exit_codes.sh

check: run_test run_tswitch_test run_net2_test run_legacy_check run_exit_test run_new_asm_test run_uart_test run_rom_test run_link_test run_c_test run_cc_errors run_tdisk_test run_tdisk2_test run_os_test run_os_tests run_gdb_test run_selfhost_test run_selfcompile_test run_rebuild_test run_rebuild_ns_test
	@echo ALL TESTS PASSED

clean:
	rm -rf lib/bare lib/os os/tools
	rm -f $(OS_TESTBINS) tests/os/*.tmp* disk.img selfhost.img selfcompile.img tdisk os/boot/boot.bin os/kernel/*.o os/kernel/kernel.tex $(OS_BINS)
	rm -f src.img tests/rebuild_run.log
	rm -rf tests/rebuild
	rm -f $(OBJ) $(TARGET) tlink tswitch tests/tswitch_test tcc0 tests/gdb_client $(TEST_BIN) $(UNIT_TESTS) $(ASM_BIN) assembler/*.bin tests/*.tmp

.PHONY: all clean test assembler rom run_test run_asm_test run_shift_test run_mul_div_test run_labels_test run_interrupt_test run_io_test run_reg32_test run_indirect_test run_directives_test run_privilege_test run_peripherals_test run_multitask_test run_debugger_test run_sysctl_test run_branch_test run_alu2_test run_mem2_test run_call2_test run_ext_test run_pic_test run_wfi_test run_prot_test run_paging_test rom run_rom_test run_link_test run_c_test run_cc_errors run_dma_test run_uart_test run_new_asm_test run_legacy_check run_exit_test check

CC0_SRC=cc/main.c cc/pp.c cc/parse.c cc/gen.c
LIB_SHARED=string ctype stdlib stdio scanf strerror time fnmatch
LIB_BARE=$(addprefix lib/bare/,crt0_bare.o sys_bare.o memops.o $(addsuffix .o,$(LIB_SHARED)))
LIB_OS=$(addprefix lib/os/,crt0.o sys.o memops.o pwd.o posix.o socket.o plan9.o addns.o 9pconv.o 9p.o net9.o $(addsuffix .o,$(LIB_SHARED)))
LIB_HDR=$(wildcard lib/include/*.h)

tcc0: $(CC0_SRC) cc/cc.h
	$(CC) $(CFLAGS) -o tcc0 $(CC0_SRC)

lib/bare/%.o: lib/%.c tcc0 $(ASM_BIN) $(LIB_HDR)
	@mkdir -p lib/bare
	./tcc0 -bare -c $< -o $@

lib/bare/%.o: lib/%.s $(ASM_BIN)
	@mkdir -p lib/bare
	./$(ASM_BIN) -c $< -o $@ > /dev/null

lib/os/%.o: lib/%.c tcc0 $(ASM_BIN) $(LIB_HDR)
	@mkdir -p lib/os
	./tcc0 -c $< -o $@

lib/os/%.o: lib/%.s $(ASM_BIN)
	@mkdir -p lib/os
	./$(ASM_BIN) -c $< -o $@ > /dev/null

libc: $(LIB_BARE) $(LIB_OS)

.PHONY: libc

KERNEL_C=klib kmem mm tfs vfs ramfs devfs procfs tty pty ns dev9 netfs p9 mnt srv env note file stubs sock inet tcp signal cred proc trap main
KERNEL_OBJ=os/kernel/entry.o os/kernel/memops.o $(addprefix os/kernel/,$(addsuffix .o,$(KERNEL_C)))
OS_PROGS=init sh echo cat ls mkdir rm wc hello ps kill sleep shutdown crash mount umount getty login su passwd id whoami chmod chown cp mv ln rmdir touch pwd head tail grep sort uniq tr cut tee env printenv test true false basename dirname date uptime hostname uname stat df du find xargs more od which top sync nice nohup dmesg lsof ifconfig route arp netstat ping nc httpd tget telnetd telnet kinstall bind unmount ns ramfs9 exportfs import srv listen1 rc newns none browse
ETC_FILES=os/etc/hosts:/etc/hosts os/etc/namespace:/lib/namespace os/etc/services:/etc/services os/etc/inittab:/etc/inittab os/etc/rc:/etc/rc os/etc/autologin:/etc/autologin os/etc/passwd:/etc/passwd os/etc/group:/etc/group -d /root:0700 os/bin/test:/bin/[
bindest=$(if $(filter none,$(1)),/bin/auth/none,/bin/$(1))
OS_BINS=$(addprefix os/bin/,$(OS_PROGS))

tdisk: tools/tdisk.c
	$(CC) $(CFLAGS) -o tdisk tools/tdisk.c

os/boot/boot.bin: os/boot/boot.asm $(ASM_BIN)
	./$(ASM_BIN) os/boot/boot.asm os/boot/boot.bin > /dev/null

os/kernel/build.date:
	date +%Y-%m-%d > $@

os/kernel/proc.o: os/kernel/proc.c os/kernel/kernel.h os/kernel/build.date tcc0 $(ASM_BIN)
	./tcc0 -c -DBUILD_DATE='"'$$(cat os/kernel/build.date)'"' $< -o $@

os/kernel/%.o: os/kernel/%.c os/kernel/kernel.h os/kernel/net.h tcc0 $(ASM_BIN)
	./tcc0 -c $< -o $@

os/kernel/entry.o: os/kernel/entry.s $(ASM_BIN)
	./$(ASM_BIN) -c $< -o $@ > /dev/null

os/kernel/memops.o: os/kernel/memops.s $(ASM_BIN)
	./$(ASM_BIN) -c $< -o $@ > /dev/null

os/kernel/kernel.tex: $(KERNEL_OBJ) tlink
	./tlink -T 0x20000 -e _start -o $@ $(KERNEL_OBJ)

os/bin/%: os/bin/%.c tcc0 libc tlink
	./tcc0 $< -o $@

disk.img: tdisk os/boot/boot.bin os/kernel/kernel.tex $(OS_BINS) $(wildcard os/etc/*)
	./tdisk -o disk.img -b os/boot/boot.bin -k os/kernel/kernel.tex $(foreach p,$(OS_PROGS),os/bin/$(p):$(call bindest,$(p))$(if $(filter passwd su ping,$(p)),:04755)) $(ETC_FILES) tests/os/motd.txt:/etc/motd tests/os/big.txt:/etc/big.txt

os: rom disk.img

run_os: os $(TARGET)
	./$(TARGET) -m 16 -r rom/rom.bin -D disk.img

run_os_test: os $(TARGET)
	sh tests/os_test.sh

run_tdisk_test: tdisk os
	sh tests/tdisk_test.sh

run_tdisk2_test: tdisk
	sh tests/tdisk2_test.sh

clean_os:
	rm -f $(OS_TESTBINS) tests/os/*.tmp* disk.img selfhost.img selfcompile.img tdisk os/boot/boot.bin os/kernel/*.o os/kernel/kernel.tex $(OS_BINS) tests/os/*.tmp*

.PHONY: os run_os run_os_test run_tdisk_test clean_os

check_nocache:
	$(MAKE) NOCACHE=-n run_legacy_check run_exit_test run_new_asm_test run_uart_test run_rom_test run_link_test run_c_test run_os_test run_os_tests run_gdb_test run_selfhost_test
	@echo ALL TESTS PASSED WITHOUT DECODE CACHE

.PHONY: check_nocache

SELF_TOOLS=os/tools/as os/tools/tlink os/tools/tcc0
LIB_INC_FILES=$(notdir $(wildcard lib/include/*.h))

os/tools/as: assembler/assembler.c tcc0 libc
	@mkdir -p os/tools
	./tcc0 assembler/assembler.c -o $@

os/tools/tlink: tools/tlink.c tcc0 libc
	@mkdir -p os/tools
	./tcc0 tools/tlink.c -o $@

os/tools/tcc0: $(CC0_SRC) cc/cc.h tcc0 libc
	@mkdir -p os/tools
	./tcc0 $(CC0_SRC) -o $@

SELFHOST_FILES=$(foreach p,$(OS_PROGS),os/bin/$(p):$(call bindest,$(p))$(if $(filter passwd su ping,$(p)),:04755)) $(ETC_FILES) $(foreach t,$(SELF_TOOLS),$(t):/bin/$(notdir $(t))) $(foreach h,$(LIB_INC_FILES),lib/include/$(h):/lib/include/$(h)) $(foreach h,$(notdir $(wildcard lib/include/sys/*.h)),lib/include/sys/$(h):/lib/include/sys/$(h)) $(foreach o,$(LIB_OS),$(o):/$(o))

selfhost.img: tdisk os/boot/boot.bin os/kernel/kernel.tex $(OS_BINS) $(SELF_TOOLS) tests/selfhost/hello.c $(wildcard os/etc/*)
	./tdisk -o selfhost.img -b os/boot/boot.bin -k os/kernel/kernel.tex $(SELFHOST_FILES) tests/selfhost/hello.c:/src/hello.c

selfcompile.img: tdisk os/boot/boot.bin os/kernel/kernel.tex $(OS_BINS) $(SELF_TOOLS) tests/selfhost/hello.c $(wildcard cc/*.c cc/*.h) $(wildcard os/etc/*)
	./tdisk -o selfcompile.img -b os/boot/boot.bin -k os/kernel/kernel.tex $(SELFHOST_FILES) tests/selfhost/hello.c:/src/hello.c $(foreach f,$(wildcard cc/*.c cc/*.h),$(f):/src/$(f)) > /dev/null

selfcompile run_selfcompile_test: selfcompile.img $(TARGET) rom
	sh tests/selfcompile_test.sh

.PHONY: selfcompile run_selfcompile_test

run_selfhost_test: selfhost.img $(TARGET) rom
	sh tests/selfhost_test.sh

.PHONY: run_selfhost_test

OS_TESTPROGS=$(basename $(notdir $(wildcard os/test/*.c)))
OS_TESTBINS=$(addprefix os/test/,$(OS_TESTPROGS))

os/test/%: os/test/%.c tcc0 libc tlink
	./tcc0 $< -o $@

tests/os/base.files.tmp: Makefile $(OS_BINS) $(OS_TESTBINS) $(wildcard os/etc/*)
	echo $(foreach p,$(OS_PROGS),os/bin/$(p):$(call bindest,$(p))$(if $(filter passwd su ping,$(p)),:04755)) $(foreach p,$(OS_TESTPROGS),os/test/$(p):/test/$(p)) $(ETC_FILES) -d /mnt tests/os/motd.txt:/etc/motd > $@

run_os_tests: os tests/os/base.files.tmp $(TARGET)
	sh tests/os_tests.sh

.PHONY: run_os_tests

SRCIMG_SECTORS=262144

src.img: tdisk os/boot/boot.bin os/kernel/kernel.tex os/kernel/build.date $(OS_BINS) $(SELF_TOOLS) $(LIB_OS) $(LIB_BARE) os/build.sh tests/srcimg_files.sh $(wildcard os/etc/*)
	./tdisk -o src.img -s $(SRCIMG_SECTORS) -b os/boot/boot.bin -k os/kernel/kernel.tex $(foreach p,$(OS_PROGS),os/bin/$(p):$(call bindest,$(p))$(if $(filter passwd su ping,$(p)),:04755)) $(ETC_FILES) $(foreach t,$(SELF_TOOLS),$(t):/bin/$(notdir $(t))) $(foreach o,$(LIB_OS),$(o):/$(o)) $(foreach o,$(LIB_BARE),$(o):/$(o)) $$(SHARED="$(LIB_SHARED)" OSONLY="pwd posix socket plan9 addns 9pconv 9p net9 sys" KERNEL_C="$(KERNEL_C)" PROGS="$(OS_PROGS)" sh tests/srcimg_files.sh) > /dev/null

srcimg: src.img

run_rebuild_test: src.img $(TARGET) rom
	sh tests/rebuild_test.sh

run_rebuild_ns_test: src.img $(TARGET) rom
	PROGS="$(OS_PROGS)" sh tests/rebuild_ns_test.sh

.PHONY: srcimg run_rebuild_test run_rebuild_ns_test
