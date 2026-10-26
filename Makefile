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
ISO_FILE = $(BUILD_DIR)/miviex.iso

# Compiler and assembler flags
CFLAGS = -I$(INC_DIR) -std=gnu99 -ffreestanding -O2 -Wall -Wextra -MMD -MP \
	-mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib
ASFLAGS = -f elf64 -i $(BOOT_SRC_DIR)/

# Targets
.PHONY: all clean iso run debug

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

# initramfs: initramfs_root/ plus /init, paths relative to the archive root
$(INITRAMFS): $(shell find initramfs_root) $(BUILD_DIR)/user/init
	rm -rf $(INITRAMFS_DIR)
	mkdir -p $(INITRAMFS_DIR)
	cp -R initramfs_root/. $(INITRAMFS_DIR)/
	cp $(BUILD_DIR)/user/init $(INITRAMFS_DIR)/init
	cd $(INITRAMFS_DIR) && find . -mindepth 1 | sed 's|^\./||' | cpio -o -H newc --quiet > $(abspath $@)

# Create ISO
iso: all $(INITRAMFS)
	mkdir -p $(GRUB_DIR)
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	cp $(GRUB_CFG) $(GRUB_DIR)/grub.cfg
	cp $(INITRAMFS) $(ISO_DIR)/boot/initramfs.cpio
	$(GRUB_MKRESCUE) -o $(ISO_FILE) $(ISO_DIR)

# Run ISO in QEMU
run: iso
	qemu-system-x86_64 -cdrom $(ISO_FILE) -serial file:serial.log -m 256M

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
