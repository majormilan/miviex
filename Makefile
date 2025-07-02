# Cross-compiling toolchain
CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld
GRUB_MKRESCUE = grub-mkrescue

# Directories
SRC_DIR = .
BOOT_DIR = $(SRC_DIR)/boot
KERNEL_DIR = $(SRC_DIR)/kernel
TARGETS_DIR = $(SRC_DIR)/targets/x86_64/iso

BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso
GRUB_DIR = $(ISO_DIR)/boot/grub

# Create necessary directories
OBJ_DIR = $(BUILD_DIR)/kernel
ASM_OBJ_DIR = $(BUILD_DIR)/boot

# Files
ASM_SOURCES = $(wildcard $(BOOT_DIR)/*.asm)
C_SOURCES = $(wildcard $(KERNEL_DIR)/*.c)
OBJECTS = $(ASM_SOURCES:$(BOOT_DIR)/%.asm=$(ASM_OBJ_DIR)/%.o) \
          $(C_SOURCES:$(KERNEL_DIR)/%.c=$(OBJ_DIR)/%.o)
LINKER_SCRIPT = $(SRC_DIR)/linker.ld
GRUB_CFG = $(TARGETS_DIR)/boot/grub/grub.cfg

KERNEL_BIN = $(BUILD_DIR)/kernel.bin
ISO_FILE = $(BUILD_DIR)/miviex.iso

# Compiler and assembler flags
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib
ASFLAGS = -f elf64

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
$(ASM_OBJ_DIR)/%.o: $(BOOT_DIR)/%.asm | $(ASM_OBJ_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

# Compile C files
$(OBJ_DIR)/%.o: $(KERNEL_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link kernel binary (Multiboot2 compliant)
$(KERNEL_BIN): $(OBJECTS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Create ISO
iso: all $(GRUB_DIR)
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	cp $(GRUB_CFG) $(GRUB_DIR)/grub.cfg
	$(GRUB_MKRESCUE) -o $(ISO_FILE) $(ISO_DIR)

# Run ISO in QEMU
run: iso
	qemu-system-x86_64 -cdrom $(ISO_FILE) -serial file:serial.log

# Debug target
debug: $(BUILD_DIR)/kernel.elf
	@echo "Kernel ELF built: $(BUILD_DIR)/kernel.elf"
	@echo "Use 'nm $(BUILD_DIR)/kernel.elf' or 'objdump -d $(BUILD_DIR)/kernel.elf' for debugging."

$(BUILD_DIR)/kernel.elf: $(OBJECTS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Clean build files
clean:
	rm -rf $(BUILD_DIR)
