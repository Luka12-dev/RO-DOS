# RO-DOS Makefile
# Author: RO-DOS Development Team
# Purpose: Build RO-DOS from assembly, C, and Rust sources into bootable floppy image
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
CARGO       := cargo

# Build Flags

# NASM flags
NASMFLAGS_ELF := -f elf32 -g -F dwarf
NASMFLAGS_BIN := -f bin

# GCC flags for 32-bit freestanding
CFLAGS := -m32 -ffreestanding -nostdlib -Iinclude -fno-builtin -fno-stack-protector
CFLAGS += -O0 -g -Wall -Wextra -std=c11
CFLAGS += -fno-pie -fno-pic
CFLAGS += -mpreferred-stack-boundary=2 -mno-mmx -mno-sse -mno-sse2
CFLAGS += -c

# Linker flags
LDFLAGS := -m elf_i386 -nostdlib -T link.ld --oformat binary



# Directories and Files
SRC_DIR     := src
DRIVER_DIR  := Drivers
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj

# Output files
BOOT_BIN    := $(BUILD_DIR)/bootload.bin
KERNEL_BIN  := $(BUILD_DIR)/kernel.bin
FLOPPY_IMG  := $(BUILD_DIR)/rodos.img
ISO_IMG     := $(BUILD_DIR)/rodos.iso
ISO_DIR     := $(BUILD_DIR)/iso

HDD_IMG     := $(BUILD_DIR)/rodos_hdd.img
HDD_SIZE_MB := 100

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
              $(SRC_DIR)/interrupt.asm \
              $(SRC_DIR)/vesa.asm

# C sources - kernel files (includes real hardware drivers)
C_SOURCES  := $(SRC_DIR)/shell.c \
              $(SRC_DIR)/commands.c \
              $(SRC_DIR)/cmd_netmode.c \
              $(SRC_DIR)/syscall.c \
              $(SRC_DIR)/utils.c \
              $(SRC_DIR)/handlers.c \
              $(SRC_DIR)/pci.c \
              $(SRC_DIR)/wifi_autostart.c \
              $(SRC_DIR)/network_interface.c \
              $(SRC_DIR)/tcp_ip_stack.c \
              $(SRC_DIR)/dhcp_client.c \
              $(SRC_DIR)/firmware_loader.c \
              $(SRC_DIR)/scrollback.c \
              $(SRC_DIR)/rust_driver_stubs.c \
              $(SRC_DIR)/gui_apps.c \
              $(SRC_DIR)/drivers/ata.c \
              $(SRC_DIR)/drivers/vbe_graphics.c \
              $(SRC_DIR)/drivers/mouse.c \
              $(SRC_DIR)/drivers/ne2000.c

# Object files
ASM_OBJS      := $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.o,$(ASM_KERNEL))
C_OBJS        := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
ALL_OBJS      := $(ASM_OBJS) $(C_OBJS)

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
	@$(MKDIR) $(OBJ_DIR)/drivers



$(ISO_DIR):
	@$(MKDIR) $(ISO_DIR)

# Rust Drivers Build


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

# Kernel Linking (with Rust library if available)
$(KERNEL_BIN): $(ALL_OBJS) link.ld | $(BUILD_DIR)
	@echo "Linking kernel..."
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
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

# Create persistent hard disk image (only if it doesn't exist)
$(HDD_IMG):
	@echo "Creating persistent hard disk image ($(HDD_SIZE_MB)MB)..."
	@$(DD) if=/dev/zero of=$(HDD_IMG) bs=1M count=$(HDD_SIZE_MB) status=none
	@echo "✓ Hard disk image created: $(HDD_IMG)"

.PHONY: run
run: $(FLOPPY_IMG) $(HDD_IMG)
	@echo "Starting QEMU with VirtIO GPU + VirtIO Network + Persistent HDD..."
	@$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
		-drive file=$(HDD_IMG),if=ide,format=raw \
		-boot a -m 32M \
		-device virtio-net-pci,disable-modern=on,netdev=net0 -netdev user,id=net0 \
		-device virtio-gpu-pci \
		-vga std \
		-display gtk,zoom-to-fit=off

.PHONY: run-debug
run-debug: $(FLOPPY_IMG) $(HDD_IMG)
	@echo "Starting QEMU with debug options..."
	@$(QEMU) -drive file=$(FLOPPY_IMG),if=floppy,format=raw \
		-drive file=$(HDD_IMG),if=ide,format=raw \
		-boot a -m 32M \
		-device virtio-net-pci,disable-modern=on,netdev=net0 -netdev user,id=net0 \
		-device virtio-gpu-pci \
		-d int,cpu_reset -no-reboot

.PHONY: run-iso
run-iso: $(ISO_IMG) $(HDD_IMG)
	@echo "Starting QEMU with ISO (CD-ROM Mode)..."
	@if [ ! -f $(ISO_IMG) ]; then \
		echo "ERROR: ISO file not found at $(ISO_IMG)"; \
		exit 1; \
	fi
	@echo "Booting ISO as CD-ROM (Drive D)..."
	@$(QEMU) -cdrom $(ISO_IMG) \
		-drive file=$(HDD_IMG),if=ide,format=raw \
		-boot d -m 32M \
		-device virtio-net-pci,disable-modern=on,netdev=net0 -netdev user,id=net0 \
		-device virtio-gpu-pci

# Reset the hard disk (clear all saved data)
.PHONY: reset-hdd
reset-hdd:
	@echo "Resetting hard disk image..."
	@$(RM) $(HDD_IMG)
	@$(DD) if=/dev/zero of=$(HDD_IMG) bs=1M count=$(HDD_SIZE_MB) status=none
	@echo "✓ Hard disk reset complete"

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
	@echo "Cleaning build artifacts (preserving HDD for persistence)..."
	@$(RM) $(BOOT_BIN) $(KERNEL_BIN) $(FLOPPY_IMG) $(ISO_IMG)
	@echo "✓ Clean complete (HDD preserved at $(HDD_IMG))"

.PHONY: clean-all
clean-all:
	@echo "Cleaning ALL build artifacts including HDD..."
	@echo "Cleaning ALL build artifacts including HDD..."
	@$(RM) -r $(BUILD_DIR)
	@echo "✓ Full clean complete"

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
rebuild: clean-all all
	@echo "Note: HDD was reset. All saved files cleared."

# Help
.PHONY: help
help:
	@echo "======================================"
	@echo "RO-DOS Makefile - Available Targets"
	@echo "======================================"
	@echo "Building:"
	@echo "  make all          - Build complete system (default)"

	@echo "  make rebuild      - Clean and rebuild everything"
	@echo "  make clean        - Remove build artifacts (preserves HDD)"
	@echo "  make clean-all    - Remove ALL artifacts including HDD"

	@echo ""
	@echo "Running:"
	@echo "  make run          - Build and run in QEMU (floppy + HDD)"
	@echo "  make run-iso      - Build and run in QEMU (ISO + HDD)"
	@echo "  make run-debug    - Run with CPU debug output"
	@echo ""
	@echo "Storage:"
	@echo "  make reset-hdd    - Reset HDD (clear all saved files)"
	@echo "  HDD location: $(HDD_IMG)"
	@echo ""
	@echo "Info:"
	@echo "  make info         - Display build information"
	@echo "  make help         - Show this help message"
	@echo "======================================"
	@echo ""
	@echo "Filesystem Commands in RO-DOS:"
	@echo "  TOUCH file  - Create empty file (persists after reboot)"
	@echo "  MKDIR dir   - Create directory (persists after reboot)"
	@echo "  NANO file   - Edit file (persists after reboot)"
	@echo "  CD dir      - Change directory (persists after reboot)"
	@echo "  DIR/LS      - List files in current directory"
	@echo "======================================"

# Dependencies
$(KERNEL_BIN): $(ALL_OBJS)
$(ASM_OBJS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
$(C_OBJS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
$(BOOT_BIN): $(ASM_BOOT)
$(FLOPPY_IMG): $(BOOT_BIN) $(KERNEL_BIN)
$(ISO_IMG): $(FLOPPY_IMG)

.DEFAULT_GOAL := all
