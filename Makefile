# RO-DOS Makefile
# Author: RO-DOS Development Team
# Purpose: Build RO-DOS from assembly and C sources into bootable floppy image
# Target: Bootable 1.44MB floppy disk image

# Tool Configuration
NASM        := nasm
GCC         := gcc
LD          := ld
DD          := dd
QEMU        := qemu-system-i386
OBJCOPY     := objcopy
HEXDUMP     := hexdump
MKDIR       := mkdir -p
RM          := rm -f
GENISOIMAGE := genisoimage
MKISOFS     := mkisofs

# Build Flags

# NASM flags
NASMFLAGS_ELF := -f elf32 -g -F dwarf
NASMFLAGS_BIN := -f bin

# GCC flags for 32-bit freestanding
CFLAGS := -m32 -ffreestanding -nostdlib -Iinclude -fno-builtin -fno-stack-protector
CFLAGS += -O2 -Wall -Wextra -Werror -std=c11
CFLAGS += -fno-pie -fno-pic
CFLAGS += -mno-red-zone -mno-mmx -mno-sse -mno-sse2
CFLAGS += -c

# Linker flags
LDFLAGS := -m elf_i386 -nostdlib -T link.ld --oformat binary

# Directories and Files
SRC_DIR     := src
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj

# Output files
BOOT_BIN    := $(BUILD_DIR)/bootload.bin
KERNEL_BIN  := $(BUILD_DIR)/kernel.bin
FLOPPY_IMG  := $(BUILD_DIR)/rodos.img
ISO_IMG     := $(BUILD_DIR)/rodos.iso
ISO_DIR     := $(BUILD_DIR)/iso

# Image parameters
IMG_SIZE_SECTORS := 2880           # 1.44MB floppy (2880 * 512 bytes)
BOOT_SIZE        := 512            # Boot sector size
KERNEL_START     := 1              # Kernel starts at sector 1

# Source Files
# Assembly sources
ASM_BOOT   := $(SRC_DIR)/bootload.asm
ASM_KERNEL := $(SRC_DIR)/kernel.asm \
              $(SRC_DIR)/memory.asm \
              $(SRC_DIR)/filesys.asm \
              $(SRC_DIR)/io.asm \
              $(SRC_DIR)/interrupt.asm

# C sources
C_SOURCES  := $(SRC_DIR)/shell.c \
              $(SRC_DIR)/commands.c \
              $(SRC_DIR)/syscall.c \
              $(SRC_DIR)/utils.c \
              $(SRC_DIR)/handlers.c

# Object files
ASM_OBJS   := $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.o,$(ASM_KERNEL))
C_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
ALL_OBJS   := $(ASM_OBJS) $(C_OBJS)

# Build Rules

.PHONY: all
all: $(FLOPPY_IMG) $(ISO_IMG)
	@echo "======================================"
	@echo "RO-DOS build completed successfully!"
	@echo "Floppy: $(FLOPPY_IMG)"
	@echo "ISO:    $(ISO_IMG)"
	@echo "======================================"

# Directory Creation
$(BUILD_DIR):
	@$(MKDIR) $(BUILD_DIR)

$(OBJ_DIR):
	@$(MKDIR) $(OBJ_DIR)

$(ISO_DIR):
	@$(MKDIR) $(ISO_DIR)

# Bootloader Build
$(BOOT_BIN): $(ASM_BOOT) $(KERNEL_BIN) | $(BUILD_DIR)
	@echo "Assembling bootloader: $<"
	@KBYTES=$$(stat -c%s $(KERNEL_BIN)); \
	KSECTORS=$$(( (KBYTES + 511) / 512 )); \
	echo "Kernel size: $$KBYTES bytes ($$KSECTORS sectors)"; \
	$(NASM) $(NASMFLAGS_BIN) -D KERNEL_SECTORS=$$KSECTORS -D KERNEL_LBA=$(KERNEL_START) -D KERNEL_DEST=0x10000 $< -o $@
	@if [ $$(stat -c%s $@) -ne 512 ]; then \
		echo "ERROR: Bootloader must be exactly 512 bytes!"; \
		exit 1; \
	fi
	@echo "✓ Bootloader built successfully (512 bytes)"

# Kernel Assembly Files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm | $(OBJ_DIR)
	@echo "Assembling: $<"
	@$(NASM) $(NASMFLAGS_ELF) $< -o $@

# Kernel C Files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "Compiling: $<"
	@$(GCC) $(CFLAGS) $< -o $@

# Kernel Linking
$(KERNEL_BIN): $(ALL_OBJS) link.ld | $(BUILD_DIR)
	@echo "Linking kernel..."
	@$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "✓ Kernel linked successfully"
	@ls -lh $@

# Floppy Image Assembly (for QEMU)
$(FLOPPY_IMG): $(KERNEL_BIN) $(BOOT_BIN) | $(BUILD_DIR)
	@echo "Creating floppy image..."
	@$(DD) if=/dev/zero of=$@ bs=512 count=$(IMG_SIZE_SECTORS) status=none
	@$(DD) if=$(BOOT_BIN) of=$@ conv=notrunc status=none
	@$(DD) if=$(KERNEL_BIN) of=$@ bs=512 seek=$(KERNEL_START) conv=notrunc status=none
	@echo "✓ Floppy image created: $@"

# ISO Image Assembly (El Torito Floppy Emulation)
$(ISO_IMG): $(FLOPPY_IMG) | $(BUILD_DIR) $(ISO_DIR)
	@echo "Creating ISO image from bootable floppy..."
	@cp $(FLOPPY_IMG) $(ISO_DIR)/rodos.img
	@echo "✓ Copied working floppy image to ISO directory"
	@if command -v $(GENISOIMAGE) >/dev/null 2>&1; then \
		$(GENISOIMAGE) -o $@ \
			-b rodos.img \
			-c boot.cat \
			-iso-level 2 \
			-J -r \
			-V "RO-DOS" \
			-input-charset iso8859-1 \
			$(ISO_DIR)/ 2>&1 | grep -v "genisoimage: Warning" || true; \
	elif command -v $(MKISOFS) >/dev/null 2>&1; then \
		$(MKISOFS) -o $@ \
			-b rodos.img \
			-c boot.cat \
			-iso-level 2 \
			-J -r \
			-V "RO-DOS" \
			-input-charset iso8859-1 \
			$(ISO_DIR)/ 2>&1 | grep -v "mkisofs: Warning" || true; \
	else \
		echo "ERROR: genisoimage or mkisofs not found. Install with: sudo apt-get install genisoimage"; \
		exit 1; \
	fi
	@echo "✓ ISO image created: $@"
	@echo "  NOTE: This ISO uses 'El Torito' Floppy Emulation."

# Running and Testing

.PHONY: run
run: $(FLOPPY_IMG)
	@echo "Starting QEMU (Floppy Mode)..."
	@$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw -boot a -m 16M

.PHONY: run-iso
run-iso: $(ISO_IMG)
	@echo "Starting QEMU with ISO (CD-ROM Mode)..."
	@if [ ! -f $(ISO_IMG) ]; then \
		echo "ERROR: ISO file not found at $(ISO_IMG)"; \
		exit 1; \
	fi
	@echo "Booting ISO as CD-ROM (Drive D)..."
	@$(QEMU) -cdrom $(ISO_IMG) -boot d -m 16M

.PHONY: run-debug
run-debug: $(FLOPPY_IMG)
	@echo "Starting QEMU with debug options..."
	@$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw -boot a -m 16M -d int,cpu_reset -no-reboot

# Debugging and Analysis

.PHONY: dump-boot
dump-boot: $(BOOT_BIN)
	@echo "Bootloader hexdump:"
	@$(HEXDUMP) -C $@ | head -n 32

.PHONY: dump-kernel
dump-kernel: $(KERNEL_BIN)
	@echo "Kernel hexdump (first 512 bytes):"
	@$(HEXDUMP) -C $@ | head -n 32

.PHONY: info
info:
	@echo "======================================"
	@echo "RO-DOS Build Information"
	@echo "======================================"
	@if [ -f $(BOOT_BIN) ]; then \
		echo "Bootloader:  $(BOOT_BIN)"; \
		echo "  Size:      $$(stat -c%s $(BOOT_BIN)) bytes"; \
		echo ""; \
	else \
		echo "Bootloader:  $(BOOT_BIN) [NOT BUILT]"; \
		echo ""; \
	fi
	@if [ -f $(KERNEL_BIN) ]; then \
		echo "Kernel:      $(KERNEL_BIN)"; \
		echo "  Size:      $$(stat -c%s $(KERNEL_BIN)) bytes"; \
		echo ""; \
	else \
		echo "Kernel:      $(KERNEL_BIN) [NOT BUILT]"; \
		echo ""; \
	fi
	@if [ -f $(FLOPPY_IMG) ]; then \
		echo "Floppy Image: $(FLOPPY_IMG)"; \
		echo "  Size:       $$(stat -c%s $(FLOPPY_IMG)) bytes"; \
		echo ""; \
	else \
		echo "Floppy Image: $(FLOPPY_IMG) [NOT BUILT]"; \
		echo ""; \
	fi
	@if [ -f $(ISO_IMG) ]; then \
		echo "ISO Image:    $(ISO_IMG)"; \
		echo "  Size:       $$(stat -c%s $(ISO_IMG)) bytes"; \
	else \
		echo "ISO Image:    $(ISO_IMG) [NOT BUILT]"; \
	fi
	@echo "======================================"

# Cleaning
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	@$(RM) -r $(BUILD_DIR)
	@echo "✓ Clean complete"

.PHONY: clean-objs
clean-objs:
	@echo "Cleaning object files..."
	@$(RM) -r $(OBJ_DIR)
	@echo "✓ Object files cleaned"

.PHONY: clean-img
clean-img:
	@echo "Cleaning disk images..."
	@$(RM) $(FLOPPY_IMG) $(ISO_IMG)
	@$(RM) -r $(ISO_DIR)
	@echo "✓ Disk images cleaned"

.PHONY: rebuild
rebuild: clean all

# Help
.PHONY: help
help:
	@echo "======================================"
	@echo "RO-DOS Makefile - Available Targets"
	@echo "======================================"
	@echo "Building:"
	@echo "  make all        - Build complete system (default)"
	@echo "  make rebuild    - Clean and rebuild everything"
	@echo "  make clean      - Remove all build artifacts"
	@echo ""
	@echo "Running:"
	@echo "  make run        - Build and run in QEMU (floppy)"
	@echo "  make run-iso    - Build and run in QEMU (ISO)"
	@echo "  make run-debug  - Run with CPU debug output"
	@echo ""
	@echo "Info:"
	@echo "  make info       - Display build information"
	@echo "  make help       - Show this help message"
	@echo "======================================"

# Dependencies
$(KERNEL_BIN): $(ALL_OBJS)
$(ASM_OBJS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
$(C_OBJS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
$(BOOT_BIN): $(ASM_BOOT)
$(FLOPPY_IMG): $(BOOT_BIN) $(KERNEL_BIN)
$(ISO_IMG): $(FLOPPY_IMG)

.DEFAULT_GOAL := all