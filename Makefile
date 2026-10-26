# Cross-compiling toolchain
CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld
GRUB_MKRESCUE = grub-mkrescue

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
TARGETS_DIR = targets/x86_64/iso

BOOT_SRC_DIR = $(SRC_DIR)/boot
KERNEL_SRC_DIR = $(SRC_DIR)/kernel

ISO_DIR = $(BUILD_DIR)/iso
GRUB_DIR = $(ISO_DIR)/boot/grub

OBJ_DIR = $(BUILD_DIR)/obj
ASM_OBJ_DIR = $(BUILD_DIR)/boot

# Files
# Note: gdt.asm is %include'd directly by main.asm (not a standalone
# translation unit), so it's excluded here to avoid it being separately
# compiled and linked, which would export duplicate/conflicting symbols
# (e.g. gdt_tss) against the copy already embedded in main.o.
ASM_SOURCES = $(filter-out $(BOOT_SRC_DIR)/gdt.asm,$(wildcard $(BOOT_SRC_DIR)/*.asm))
C_SOURCES = $(shell find $(KERNEL_SRC_DIR) -name "*.c")
OBJECTS = $(patsubst $(BOOT_SRC_DIR)/%.asm,$(ASM_OBJ_DIR)/%.o,$(ASM_SOURCES)) \
	$(patsubst $(KERNEL_SRC_DIR)/%.c,$(OBJ_DIR)/kernel/%.o,$(C_SOURCES))
LINKER_SCRIPT = linker.ld
GRUB_CFG = $(TARGETS_DIR)/boot/grub/grub.cfg

KERNEL_BIN = $(BUILD_DIR)/kernel.bin

# Userland test programs (static Linux binaries) and the initramfs
USER_CC = musl-gcc
INITRAMFS = $(BUILD_DIR)/initramfs.cpio
INITRAMFS_DIR = $(BUILD_DIR)/initramfs

# External userland, pinned as git submodules under ext/ and fetched on
# first use. Each is built in a copy under build/ so the checkouts stay
# clean. SHUSH_DIR may point at a working copy instead (make SHUSH_DIR=...).
EXT_DIR = ext
SHUSH_DIR ?= $(EXT_DIR)/shush
SBASE_DIR = $(EXT_DIR)/sbase
UBASE_DIR = $(EXT_DIR)/ubase

# sbase/ubase as static Linux binaries; musl has crypt() in libc itself
PORT_FLAGS = CC=$(USER_CC) LDFLAGS="-static -s" LDLIBS=
PORT_INSTALL = install DESTDIR=$(abspath $(INITRAMFS_DIR)) PREFIX= \
	MANPREFIX=/share/man $(PORT_FLAGS)
ISO_FILE = $(BUILD_DIR)/miviex.iso

# Compiler and assembler flags
CFLAGS = -I$(INC_DIR) -std=gnu99 -ffreestanding -O2 -Wall -Wextra -MMD -MP \
	-mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib
ASFLAGS = -f elf64 -i $(BOOT_SRC_DIR)/

# Targets
.PHONY: all clean iso run run-serial debug

all: $(KERNEL_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ISO_DIR):
	mkdir -p $(ISO_DIR)

$(GRUB_DIR):
	mkdir -p $(GRUB_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(ASM_OBJ_DIR):
	mkdir -p $(ASM_OBJ_DIR)

# Compile assembly files
$(ASM_OBJ_DIR)/%.o: $(BOOT_SRC_DIR)/%.asm | $(ASM_OBJ_DIR)
	$(AS) $(ASFLAGS) -MD $(@:.o=.d) -MP -o $@ $<

# Compile C files
$(OBJ_DIR)/kernel/%.o: $(KERNEL_SRC_DIR)/%.c | $(OBJ_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Link kernel binary (Multiboot2 compliant)
$(KERNEL_BIN): $(OBJECTS) $(LINKER_SCRIPT)
	$(info OBJECTS: $(OBJECTS))
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Static Linux userland, built against musl
$(BUILD_DIR)/user/%: user/%.c
	mkdir -p $(@D)
	$(USER_CC) -static -Os -s -o $@ $<

# Fetch a submodule the first time it is needed
$(EXT_DIR)/%/Makefile:
	git submodule update --init $(EXT_DIR)/$*

# Tracked files of a checkout (working-tree contents, so local edits in a
# SHUSH_DIR working copy count, but its stale build output does not).
sources = $(addprefix $(1)/,$(shell git -C $(1) ls-files 2>/dev/null))

# Local changes to a project, patches/<project>/*.patch, applied in order
patches = $(sort $(wildcard patches/$(notdir $(1))/*.patch))

# $(call port,source dir): fresh copy of the sources into $(@D), patched
define port
	rm -rf $(@D)
	mkdir -p $(@D)
	git -C $(1) ls-files -z | tar -C $(1) --null -T - -cf - | tar -C $(@D) -xf -
	for p in $(abspath $(call patches,$(1))); do patch -s -d $(@D) -p1 < $$p || exit 1; done
endef

$(BUILD_DIR)/shush/.built: $(SHUSH_DIR)/Makefile $(call sources,$(SHUSH_DIR)) $(call patches,$(SHUSH_DIR))
	$(call port,$(SHUSH_DIR))
	$(MAKE) -C $(@D)
	touch $@

$(BUILD_DIR)/sbase/.built: $(SBASE_DIR)/Makefile $(call sources,$(SBASE_DIR)) $(call patches,$(SBASE_DIR))
	$(call port,$(SBASE_DIR))
	$(MAKE) -C $(@D) $(PORT_FLAGS)
	touch $@

$(BUILD_DIR)/ubase/.built: $(UBASE_DIR)/Makefile $(call sources,$(UBASE_DIR)) $(call patches,$(UBASE_DIR))
	$(call port,$(UBASE_DIR))
	$(MAKE) -C $(@D) $(PORT_FLAGS)
	touch $@

PORTS = $(BUILD_DIR)/shush/.built $(BUILD_DIR)/sbase/.built $(BUILD_DIR)/ubase/.built

# Directories git cannot track (empty) and modes it cannot store
ROOT_DIRS = bin sbin etc root home tmp var/log run proc sys

# Accounts that get a home directory owned by them (ids from etc/passwd)
HOMES = miviex

# initramfs: rootfs/, ubase then sbase (sbase wins dd and mknod), shush as
# /bin/sh and /init; everything owned by root, paths relative to the root
$(INITRAMFS): $(shell find rootfs) $(BUILD_DIR)/user/init $(PORTS)
	rm -rf $(INITRAMFS_DIR)
	mkdir -p $(addprefix $(INITRAMFS_DIR)/,$(ROOT_DIRS))
	cp -R rootfs/. $(INITRAMFS_DIR)/
	chmod 0600 $(INITRAMFS_DIR)/etc/shadow
	chmod 0700 $(INITRAMFS_DIR)/root
	chmod 1777 $(INITRAMFS_DIR)/tmp
	$(MAKE) -C $(BUILD_DIR)/ubase $(PORT_INSTALL)
	$(MAKE) -C $(BUILD_DIR)/sbase $(PORT_INSTALL)
	rm -rf $(INITRAMFS_DIR)/share
	cp $(BUILD_DIR)/shush/build/shush $(INITRAMFS_DIR)/bin/sh
	cp $(BUILD_DIR)/user/init $(INITRAMFS_DIR)/init
	chmod 4755 $(INITRAMFS_DIR)/bin/su $(INITRAMFS_DIR)/bin/passwd
	ln -s ../run $(INITRAMFS_DIR)/var/run
	cd $(INITRAMFS_DIR) && find . -mindepth 1 | sed 's|^\./||' | \
		cpio -o -H newc -R 0:0 --quiet > $(abspath $@)
	# Each home in its own archive, appended, owned by its user
	for u in $(HOMES); do \
		ids=$$(awk -F: -v u=$$u '$$1 == u { print $$3 ":" $$4 }' rootfs/etc/passwd); \
		rm -rf $(BUILD_DIR)/home-$$u && mkdir -p $(BUILD_DIR)/home-$$u/home/$$u && \
		chmod 0700 $(BUILD_DIR)/home-$$u/home/$$u && \
		(cd $(BUILD_DIR)/home-$$u && echo home/$$u | \
			cpio -o -H newc -R $$ids --quiet) >> $(abspath $@) || exit 1; \
	done

# Create ISO
iso: all $(INITRAMFS)
	mkdir -p $(GRUB_DIR)
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	cp $(GRUB_CFG) $(GRUB_DIR)/grub.cfg
	cp $(INITRAMFS) $(ISO_DIR)/boot/initramfs.cpio
	$(GRUB_MKRESCUE) -o $(ISO_FILE) $(ISO_DIR)

# Run ISO in QEMU: VGA window, serial output to serial.log
run: iso
	qemu-system-x86_64 -cdrom $(ISO_FILE) -serial file:serial.log -m 256M

# Run ISO in this terminal over the serial port; pick the "serial console"
# entry in the GRUB menu. Quit with Ctrl-A X.
run-serial: iso
	qemu-system-x86_64 -cdrom $(ISO_FILE) -nographic -m 256M

# Debug target
debug: $(BUILD_DIR)/kernel.elf
	@echo "Kernel ELF built: $(BUILD_DIR)/kernel.elf"
	@echo "Use 'nm $(BUILD_DIR)/kernel.elf' or 'objdump -d $(BUILD_DIR)/kernel.elf' for debugging."

$(BUILD_DIR)/kernel.elf: $(OBJECTS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

-include $(OBJECTS:.o=.d)

# Clean build files
clean:
	rm -rf $(BUILD_DIR)
