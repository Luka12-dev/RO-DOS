# RO-DOS System Architecture

## Table of Contents

1. [Overview](#overview)
2. [Boot Process](#boot-process)
3. [Memory Architecture](#memory-architecture)
4. [Kernel Subsystems](#kernel-subsystems)
5. [File System](#file-system)
6. [I/O Management](#io-management)
7. [Interrupt Handling](#interrupt-handling)
8. [System Call Interface](#system-call-interface)
9. [Shell Architecture](#shell-architecture)
10. [Command System](#command-system)
11. [Data Structures](#data-structures)
12. [API Reference](#api-reference)

---

## Overview

RO-DOS (Retro-Disk Operating System) is a 32-bit protected mode operating system designed with a modular architecture that separates concerns between hardware abstraction, kernel services, and user-space functionality.

### Design Philosophy

- **Simplicity**: Clean, understandable code with minimal dependencies
- **Performance**: Direct hardware access where appropriate
- **Modularity**: Well-defined interfaces between components
- **Retro Aesthetics**: DOS-like user experience with modern capabilities
- **Educational Value**: Comprehensive comments and documentation

### System Components

```
┌─────────────────────────────────────────────────┐
│              User Applications                  │
│         (Shell, Built-in Commands)              │
├─────────────────────────────────────────────────┤
│           System Call Interface                 │
│              (INT 0x80)                         │
├─────────────────────────────────────────────────┤
│              Kernel Services                    │
│  ┌──────────┬──────────┬──────────┬──────────┐  │
│  │  Memory  │   File   │   I/O    │ Process  │  │
│  │  Manager │  System  │  Manager │ Manager  │  │
│  └──────────┴──────────┴──────────┴──────────┘  │
├─────────────────────────────────────────────────┤
│          Hardware Abstraction Layer             │
│  ┌──────────┬──────────┬──────────┬──────────┐  │
│  │   PIC    │   PIT    │    RTC   │   ATA    │  │
│  │  (IRQ)   │ (Timer)  │  (Clock) │  (Disk)  │  │
│  └──────────┴──────────┴──────────┴──────────┘  │
├─────────────────────────────────────────────────┤
│               Hardware Layer                    │
│        (CPU, Memory, Disk, Keyboard)            │
└─────────────────────────────────────────────────┘
```

---

## Boot Process

### Stage 1: BIOS Boot

**Location**: Physical sector 0 (0x7C00 in memory)

**File**: `src/bootload.asm`

**Process**:

1. **BIOS POST** (Power-On Self-Test)
   - Hardware initialization
   - Memory detection
   - Device enumeration

2. **Boot Device Selection**
   - BIOS searches for bootable media
   - Loads first sector (512 bytes) to 0x7C00
   - Verifies boot signature (0xAA55)
   - Transfers control to bootloader

### Stage 2: Bootloader Execution

**Memory State**: Real Mode (16-bit)

**Responsibilities**:

```asm
; 1. Setup initial environment
cli                          ; Disable interrupts
xor ax, ax                   ; Clear segment registers
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7C00              ; Stack below bootloader

; 2. Load kernel from disk
mov dword [kernel_lba], 1   ; Kernel starts at sector 1
mov word [kernel_sectors], KERNEL_SECTORS
mov dword [kernel_dest], 0x10000  ; Load to 64KB
call read_sectors_lba       ; Read using BIOS INT 13h

; 3. Enable A20 gate (access > 1MB memory)
in al, 0x92
or al, 2
out 0x92, al

; 4. Setup Global Descriptor Table (GDT)
lgdt [gdtr]                 ; Load GDT register

; 5. Enter Protected Mode
mov eax, cr0
or eax, 1                   ; Set PE (Protection Enable) bit
mov cr0, eax

; 6. Far jump to flush pipeline and enter 32-bit mode
jmp 0x08:pmode_entry        ; CS=0x08 (code segment selector)
```

**GDT Structure**:

```
Offset  | Segment        | Base    | Limit      | Type
--------|----------------|---------|------------|------
0x00    | Null Descriptor| 0       | 0          | N/A
0x08    | Code Segment   | 0       | 0xFFFFF    | Execute/Read
0x10    | Data Segment   | 0       | 0xFFFFF    | Read/Write
```

### Stage 3: Protected Mode Entry

**Memory State**: 32-bit Protected Mode

**File**: `src/kernel.asm::pmode_entry`

```asm
[BITS 32]
pmode_entry:
    ; Setup 32-bit segment registers
    mov ax, 0x10            ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000        ; Setup stack (top at 640KB)

    ; Jump to kernel entry point
    jmp 0x08:KERNEL_DEST    ; 0x10000
```

### Stage 4: Kernel Initialization

**File**: `src/kernel.asm::kernel_entry`

**Initialization Sequence**:

```
1. Disable interrupts (CLI)
2. Setup segment registers (DS, ES, FS, GS, SS)
3. Initialize stack pointer (ESP = 0x90000, EBP = ESP)
4. Mask all PIC interrupts (prevent spurious IRQs)
5. Initialize Interrupt Descriptor Table (IDT)
   - Load exception handlers (INT 0x00-0x1F)
   - Load IRQ handlers (INT 0x20-0x2F)
   - Load system call handler (INT 0x80)
6. Remap PIC (Programmable Interrupt Controller)
   - Master PIC: IRQ 0-7 → INT 0x20-0x27
   - Slave PIC: IRQ 8-15 → INT 0x28-0x2F
7. Initialize I/O subsystem
   - Setup VGA text mode (80x25)
   - Initialize keyboard handler
8. Initialize memory manager
   - Setup heap (base: 0x200000, size: 2MB)
   - Initialize MCB (Memory Control Block) chain
9. Enable interrupts (STI)
10. Print boot messages
11. Start shell (shell_main)
```

**Boot Messages**:

```
RO-DOS Kernel v1.0
System initialized
Memory manager ready
Enabling interrupts...
Starting shell...

C:>
```

---

## Memory Architecture

### Physical Memory Layout

```
┌──────────────────────────────────────────────────┐
│ 0xFFFFFFFF                                       │
│            Extended Memory (Expansion)           │
│                                                  │
├──────────────────────────────────────────────────┤
│ 0x00400000 (4MB)                                 │
│            Available for Heap Expansion          │
├──────────────────────────────────────────────────┤
│ 0x00200000 (2MB)                                 │
│            Kernel Heap (Dynamic Allocation)      │
│            - Managed by memory.asm               │
│            - MCB-based allocator                 │
├──────────────────────────────────────────────────┤
│ 0x00100000 (1MB)                                 │
│            Free Extended Memory                  │
├──────────────────────────────────────────────────┤
│ 0x000A0000 (640KB)                              │
│            Video Memory Region                   │
│ 0x000B8000 : VGA Text Mode Buffer (4KB)        │
├──────────────────────────────────────────────────┤
│ 0x00090000 (576KB)                              │
│            Kernel Stack (grows downward)         │
│            - Base: ESP = 0x90000                │
│            - Size: ~64KB                        │
├──────────────────────────────────────────────────┤
│ 0x00010000 (64KB)                               │
│            Kernel Code & Data                    │
│            - .text section (code)               │
│            - .data section (initialized)        │
│            - .bss section (uninitialized)       │
├──────────────────────────────────────────────────┤
│ 0x00007E00                                       │
│            Free Conventional Memory              │
├──────────────────────────────────────────────────┤
│ 0x00007C00                                       │
│            Boot Sector (512 bytes)              │
│            - Loaded by BIOS                     │
│            - Jumps to kernel after setup        │
├──────────────────────────────────────────────────┤
│ 0x00000500                                       │
│            Free Conventional Memory              │
├──────────────────────────────────────────────────┤
│ 0x00000400                                       │
│            BIOS Data Area (BDA)                 │
│            - Hardware configuration             │
│            - Detected devices                   │
├──────────────────────────────────────────────────┤
│ 0x00000000                                       │
│            Interrupt Vector Table (IVT)         │
│            - 256 entries × 4 bytes              │
│            - Real mode interrupt vectors        │
└──────────────────────────────────────────────────┘
```

### Memory Manager Architecture

**File**: `src/memory.asm`

**Allocation Strategy**: First-Fit with Coalescing

**Data Structure**: Memory Control Block (MCB)

```c
struct MCB {
    uint32_t prev;          // Pointer to previous block
    uint32_t next;          // Pointer to next block
    uint32_t size;          // Payload size (bytes)
    uint32_t magic;         // 0x4D43424B ("MCKB")
    uint32_t flags;         // 0=used, 1=free
};
// Total header size: 20 bytes
```

**Memory Block States**:

```
┌─────────────────────────────────────────────┐
│  MCB Header (20 bytes)                      │
│  ┌────────┬────────┬────────┬────────────┐  │
│  │  prev  │  next  │  size  │ magic+flags│  │
│  └────────┴────────┴────────┴────────────┘  │
├─────────────────────────────────────────────┤
│  Payload (size bytes)                       │
│  - User data area                           │
│  - Returned by kmalloc()                    │
└─────────────────────────────────────────────┘
```

**Allocation Algorithm** (`kmalloc`):

```
1. Validate requested size (> 0)
2. Align size to 4-byte boundary
3. Search free list (first-fit)
   FOR each block in free_list:
       IF block.size >= requested_size:
           - Found suitable block
           - Break loop
4. IF block found:
   a. IF block is much larger (split threshold):
      - Split block into allocated + free
      - Create new MCB after allocated portion
      - Update free list pointers
   b. ELSE:
      - Use entire block
      - Remove from free list
   c. Mark block as used (flags = 0)
   d. Return payload pointer (MCB + 20 bytes)
5. ELSE:
   - Return NULL (out of memory)
```

**Deallocation Algorithm** (`kfree`):

```
1. Validate pointer (not NULL)
2. Get MCB header (pointer - 20 bytes)
3. Validate magic number
4. IF already free: return (double-free protection)
5. Mark block as free (flags = 1)
6. Coalesce with next block:
   IF next block exists AND next is free:
      - Merge: current.size += MCB_SIZE + next.size
      - Update: current.next = next.next
7. Coalesce with previous block:
   IF prev block exists AND prev is free:
      - Merge: prev.size += MCB_SIZE + current.size
      - Update: prev.next = current.next
8. IF no coalescing with prev:
   - Add to head of free list
```

**Statistics Tracking**:

```c
struct MemStats {
    uint32_t total_free;    // Free memory (bytes)
    uint32_t total_used;    // Used memory (bytes)
    uint32_t num_blocks;    // Total blocks
};
```

---

## Kernel Subsystems

### 1. Interrupt Management

**File**: `src/interrupt.asm`

**Interrupt Descriptor Table (IDT)**:

```
Entry   | Vector | Handler           | Type
--------|--------|-------------------|------
0x00    | #DE    | isr0 (Div by 0)  | Exception
0x0D    | #GP    | isr13 (Gen Fault)| Exception
0x20    | IRQ0   | irq0 (Timer)     | Hardware
0x21    | IRQ1   | irq1 (Keyboard)  | Hardware
0x80    | -      | syscall_stub     | Software
```

**IDT Entry Format** (8 bytes):

```
Bits    | Field
--------|------------------
0-15    | Offset low (handler address bits 0-15)
16-31   | Selector (code segment, 0x08)
32-39   | Reserved (0)
40-47   | Flags (type, DPL, present)
48-63   | Offset high (handler address bits 16-31)
```

**Interrupt Handling Flow**:

```
1. CPU receives interrupt
2. CPU saves EFLAGS, CS, EIP on stack
3. CPU looks up handler in IDT
4. CPU jumps to handler
5. Handler (common stub):
   a. Push registers (PUSHA)
   b. Push segment registers
   c. Setup kernel data segments
   d. Call C handler (if applicable)
   e. Restore segment registers
   f. Restore registers (POPA)
   g. Return from interrupt (IRET)
```

**Keyboard Handler** (`irq1`):

```asm
; Read scancode from keyboard controller
in al, 0x60

; Handle key releases (break codes)
test al, 0x80
jnz .handle_release

; Convert scancode to ASCII
movzx ebx, al
lea esi, [scan_to_ascii]
test byte [kb_shift], 1
jz .no_shift
lea esi, [scan_to_ascii_shift]
.no_shift:
mov al, [esi + ebx]

; Handle Caps Lock and Shift logic
; Apply case conversion if needed

; Store in keyboard buffer (ring buffer)
mov ebx, [key_buffer_head]
mov [key_buffer + ebx], al
inc ebx
and ebx, 255
mov [key_buffer_head], ebx
```

**Timer Handler** (`irq0`):

```asm
; Increment system tick counter
inc dword [timer_ticks]

; Send EOI (End of Interrupt) to PIC
mov al, 0x20
out 0x20, al
```

### 2. Process Management

**Current State**: Single-process (shell only)

**Process Structure** (simulated):

```c
struct Process {
    uint16_t pid;           // Process ID
    char name[32];          // Process name
    uint8_t priority;       // 0 (high) to 31 (low)
    uint32_t mem_kb;        // Memory usage
    bool active;            // Running state
};
```

**Process Table**:

```c
static Process proc_table[16];  // Max 16 processes
static int proc_count = 0;

// Default processes:
// PID 0: IDLE
// PID 1: KERNEL
// PID 2: SHELL.EXE
```

**System Calls**:

- `sys_getpid()` - Returns current process ID (fixed: 2)
- Future: `sys_fork()`, `sys_exec()`, `sys_wait()`

---

## File System

### FAT12 Implementation

**File**: `src/filesys.asm`

**Boot Parameter Block (BPB)**:

```
Offset  | Size | Field              | Value
--------|------|--------------------|---------
0x0B    | 2    | Bytes per sector   | 512
0x0D    | 1    | Sectors per cluster| 1
0x0E    | 2    | Reserved sectors   | 1
0x10    | 1    | Number of FATs     | 2
0x11    | 2    | Root entries       | 224
0x13    | 2    | Total sectors      | 2880
0x15    | 1    | Media descriptor   | 0xF0
0x16    | 2    | Sectors per FAT    | 9
```

**Disk Layout**:

```
Sector   | Content
---------|------------------
0        | Boot sector + BPB
1-9      | FAT #1
10-18    | FAT #2
19-32    | Root directory (14 sectors)
33+      | Data clusters
```

**Directory Entry** (32 bytes):

```
Offset  | Size | Field
--------|------|------------------
0x00    | 11   | Filename (8.3 format)
0x0B    | 1    | Attributes
0x0C    | 10   | Reserved
0x16    | 2    | First cluster
0x18    | 4    | File size
```

**File Operations**:

```asm
; Initialize file system
fs_init:
    ; Read boot sector
    ; Parse BPB
    ; Calculate FAT/root/data locations
    ; Allocate cluster buffer

; List root directory
fs_list_root:
    ; Read root directory sectors
    ; Parse directory entries
    ; Return entry count

; Find file in root
fs_find_file:
    ; Search root directory
    ; Compare filenames (11 bytes)
    ; Return entry index or -1

; Open file
fs_open:
    ; Find file in directory
    ; Allocate file handle
    ; Initialize handle structure
    ; Return handle ID

; Read file
fs_read:
    ; Walk cluster chain via FAT
    ; Read cluster data
    ; Update file position
    ; Return bytes read

; Close file
fs_close:
    ; Free file handle
    ; Clear handle structure
```

**File Handle Structure**:

```c
struct FileHandle {
    uint32_t first_cluster;     // Starting cluster
    uint32_t file_size;         // Total size
    uint32_t current_cluster;   // Current position cluster
    uint32_t position;          // Byte offset
    void* cluster_buffer;       // Buffer for I/O
    uint32_t cluster_offset;    // Offset in cluster
    uint32_t flags;             // Bit 0: in-use
    uint32_t reserved;
};
```

**FAT12 Cluster Chain**:

```
Cluster chains are linked lists in the FAT:

Example: File spans clusters 2, 3, 5, 7

FAT Entry | Value  | Meaning
----------|--------|------------------
[2]       | 3      | Next cluster is 3
[3]       | 5      | Next cluster is 5
[5]       | 7      | Next cluster is 7
[7]       | 0xFFF  | End of file (EOF)

Special values:
0x000       : Free cluster
0xFF0-0xFF6 : Reserved
0xFF7       : Bad cluster
0xFF8-0xFFF : End of file
```

---

## I/O Management

### VGA Text Mode

**File**: `src/io.asm`

**Video Buffer**: 0xB8000 (80×25 characters, 2 bytes each)

**Character Cell Format**:

```
Byte 0: ASCII character code
Byte 1: Attribute byte
   Bits 0-3: Foreground color
   Bits 4-6: Background color
   Bit 7: Blink (if enabled)
```

**Color Palette**:

```
Value | Color
------|-------------
0x0   | Black
0x1   | Blue
0x2   | Green
0x3   | Cyan
0x4   | Red
0x5   | Magenta
0x6   | Brown
0x7   | Light Gray
0x8   | Dark Gray
0x9   | Light Blue
0xA   | Light Green
0xB   | Light Cyan
0xC   | Light Red
0xD   | Light Magenta
0xE   | Yellow
0xF   | White
```

**I/O Functions**:

```asm
; Print character
putc:
    ; Handle special characters (\n, \r, \b)
    ; Calculate screen position
    ; Write to video memory
    ; Update cursor position
    ; Handle scrolling if needed

; Print string
puts:
    ; Loop through string
    ; Call putc for each character

; Clear screen
cls:
    ; Fill video buffer with spaces
    ; Reset cursor to (0, 0)

; Scroll screen up
scroll_up:
    ; Copy lines 1-24 to lines 0-23
    ; Clear bottom line
```

**Hardware Cursor**:

```asm
set_cursor_hardware:
    ; Calculate linear position: row * 80 + col
    ; Write to VGA CRTC registers:
    ; - Port 0x3D4: Register index
    ; - Port 0x3D5: Register data
    ; - Register 0x0E: Cursor location high byte
    ; - Register 0x0F: Cursor location low byte
```

### Keyboard Input

**Keyboard Controller**: Port 0x60 (data), Port 0x64 (status/command)

**Scancode Translation**:

```asm
scan_to_ascii:
    ; Normal scancodes (0x00-0x3A)
    db 0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9
    db 'q','w','e','r','t','y','u','i','o','p','[',']',13,0
    db 'a','s','d','f','g','h','j','k','l',';',39,96,0,92
    db 'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '

scan_to_ascii_shift:
    ; Shifted scancodes
    db 0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9
    db 'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0
    db 'A','S','D','F','G','H','J','K','L',':',34,'~',0,'|'
    db 'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
```

**Key State Tracking**:

```asm
kb_shift db 0    ; Shift key pressed
kb_caps  db 0    ; Caps Lock toggled
```

**Keyboard Buffer** (Ring Buffer):

```asm
key_buffer:       times 256 db 0
key_buffer_head:  dd 0
key_buffer_tail:  dd 0

; Producer (IRQ handler):
mov ebx, [key_buffer_head]
mov [key_buffer + ebx], al
inc ebx
and ebx, 255
mov [key_buffer_head], ebx

; Consumer (getkey_block):
mov eax, [key_buffer_tail]
cmp eax, [key_buffer_head]
je .buffer_empty
movzx eax, byte [key_buffer + eax]
inc dword [key_buffer_tail]
and dword [key_buffer_tail], 255
```

---

## System Call Interface

### INT 0x80 Mechanism

**File**: `src/syscall.c`

**Calling Convention**:

```c
// Registers:
// EAX: System call number
// EBX: Argument 1
// ECX: Argument 2
// EDX: Argument 3
// Return: EAX (result)

int32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int32_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}
```

**System Call Table**:

| Number | Name | Description |
|--------|------|-------------|
| 0x01 | SYS_PRINT_STRING | Print null-terminated string |
| 0x02 | SYS_PRINT_CHAR | Print single character |
| 0x04 | SYS_READ_CHAR | Read character from keyboard |
| 0x05 | SYS_CLEAR_SCREEN | Clear screen |
| 0x08 | SYS_SET_COLOR | Set text attribute |
| 0x1A | SYS_CHDIR | Change directory |
| 0x1B | SYS_GETCWD | Get current directory |
| 0x1D | SYS_OPENDIR | Open directory |
| 0x1E | SYS_READDIR | Read directory entry |
| 0x1F | SYS_CLOSEDIR | Close directory |
| 0x44 | SYS_GETPID | Get process ID |
| 0x50 | SYS_GET_TIME | Read RTC time |
| 0x51 | SYS_GET_DATE | Read RTC date |
| 0x52 | SYS_GET_TICKS | Get system uptime |
| 0x54 | SYS_SYSINFO | Get system information |
| 0x55 | SYS_UNAME | Get system name |
| 0x61 | SYS_READ_SECTOR | Read disk sector (ATA) |
| 0x71 | SYS_SHUTDOWN | Halt system |
| 0x72 | SYS_BEEP | Generate PC speaker beep |
| 0x73 | SYS_DEBUG | Debug output |

**Handler Dispatch**:

```c
int syscall_handler(int num, int arg1, int arg2, int arg3) {
    switch (num) {
        case SYS_PRINT_STRING:
            puts((const char*)arg1);
            return 0;
        
        case SYS_GET_TIME: {
            uint8_t h = get_cmos_reg(0x04);
            uint8_t m = get_cmos_reg(0x02);
            uint8_t s = get_cmos_reg(0x00);
            return (h << 16) | (m << 8) | s;
        }
        
        case SYS_READ_SECTOR:
            return disk_read_lba(arg1, arg2, (void*)arg3);
        
        default:
            return -1;  // ENOSYS
    }
}
```

---

## Shell Architecture

### Shell Main Loop

**File**: `src/shell.c`

**Input Processing**:

```c
void shell_main(void) {
    char line[MAX_INPUT];
    int pos = 0;

    while (1) {
        // Display prompt
        set_attr(0x0E);  // Yellow
        c_puts("C:> ");
        set_attr(0x0F);  // White
        
        // Read input
        pos = 0;
        while (1) {
            uint16_t k = c_getkey();
            uint8_t key = (uint8_t)(k & 0xFF);

            if (key == '\n' || key == '\r') {
                line[pos] = '\0';
                c_putc('\n');
                break;
            }
            else if (key == 8) {  // Backspace
                if (pos > 0) {
                    pos--;
                    c_putc(8);   // Move cursor back
                    c_putc(' '); // Erase character
                    c_putc(8);   // Move cursor back again
                }
            }
            else if (key >= 32 && key <= 126 && pos < MAX_INPUT - 1) {
                line[pos++] = (char)key;
                c_putc((char)key);
            }
        }

        // Trim whitespace
        str_trim(line);

        // Execute command
        if (str_len(line) > 0) {
            set_attr(0x07);  // Gray
            cmd_dispatch(line);
        }
    }
}
```

---

## Command System

### Command Dispatcher

**File**: `src/commands.c`

**Tokenization**:

```c
int tokenize(char *line, char *argv[], int maxargs) {
    int argc = 0;
    char *p = line;
    
    while (*p && argc < maxargs) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        
        // Handle quoted strings
        if (*p == '"') {
            ++p;
            argv[argc++] = p;
            while (*p && *p != '"') ++p;
            if (*p == '"') { *p = '\0'; ++p; }
        }
        // Handle regular tokens
        else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') ++p;
            if (*p) { *p = '\0'; ++p; }
        }
    }
    return argc;
}
```

**Command Dispatch**:

```c
int cmd_dispatch(const char *cmdline) {
    char buf[256];
    strcpy(buf, cmdline);
    
    char *argv[16];
    int argc = tokenize(buf, argv, 16);
    if (argc == 0) return 0;
    
    char *cmd = argv[0];
    toupper_str(cmd);
    
    // System commands
    if (strcmp(cmd, "VER") == 0) return cmd_ver(argc, argv);
    if (strcmp(cmd, "CLS") == 0) return cmd_cls(argc, argv);
    if (strcmp(cmd, "MEM") == 0) return cmd_mem(argc, argv);
    
    // File system commands
    if (strcmp(cmd, "DIR") == 0) return cmd_dir(argc, argv);
    if (strcmp(cmd, "CD") == 0) return cmd_cd(argc, argv);
    if (strcmp(cmd, "TYPE") == 0) return cmd_type(argc, argv);
    
    // ... 100+ more commands ...
    
    // Unknown command
    puts("Bad command or file name: ");
    puts(cmd);
    return -1;
}
```

### Command Categories

**System & Utility** (15 commands):
- VER, MEM, ECHO, REBOOT, SHUTDOWN, CLS, COLOR, BEEP, SLEEP, GETTICK, TIME, DATE, WHOAMI, EXIT, HELP

**Filesystem** (14 commands):
- DIR, CD, MD, RD, TYPE, COPY, DEL, REN, ATTRIB, TOUCH, XCOPY, FIND, PUSHD, POPD

**Disk & Hardware** (8 commands):
- FORMAT, FDISK, CHKDSK, MOUNT, LABEL, DISKCOMP, BACKUP, DRIVE

**Networking** (9 commands):
- PING, IPCONFIG, TRACERT, NETSTAT, FTP, TELNET, WGET, DNS, ROUTE

**Process Management** (6 commands):
- PS, KILL, RUN, FREE, NICE, START

**Device/Driver** (5 commands):
- BIOS, LOAD, DRIVER, USB, IRQ

**Environment/Security** (5 commands):
- SET, UNSET, PATH, USERADD, PASSWD

---

## Data Structures

### Core Kernel Structures

#### 1. Memory Control Block (MCB)

```c
typedef struct MCB {
    struct MCB* prev;       // Previous block in list
    struct MCB* next;       // Next block in list
    uint32_t size;          // Size of payload (bytes)
    uint32_t magic;         // Magic number: 0x4D43424B
    uint32_t flags;         // Bit 0: 1=free, 0=used
} MCB;

#define MCB_MAGIC       0x4D43424B
#define MCB_HEADER_SIZE 20
#define MCB_FLAG_FREE   1
#define MCB_FLAG_USED   0
```

#### 2. File System Entry

```c
typedef struct FSEntry {
    char name[64];          // File/directory name
    uint32_t size;          // File size in bytes
    uint8_t type;           // 0=file, 1=directory
    uint8_t attr;           // DOS attributes
    uint16_t reserved;      // Alignment padding
} FSEntry;

// DOS Attributes
#define ATTR_READONLY   0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME     0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
```

#### 3. Process Control Block

```c
typedef struct Process {
    uint16_t pid;           // Process ID
    char name[32];          // Process name
    uint8_t priority;       // Priority (0=highest, 31=lowest)
    uint8_t state;          // Running, blocked, etc.
    uint32_t mem_kb;        // Memory usage in KB
    uint32_t cpu_time;      // CPU time used (ticks)
    bool active;            // Is process active
    uint8_t reserved[3];    // Alignment padding
} Process;

// Process States
#define PROC_RUNNING    0
#define PROC_READY      1
#define PROC_BLOCKED    2
#define PROC_TERMINATED 3
```

#### 4. Network Interface

```c
typedef struct NetInterface {
    char name[16];          // Interface name (e.g., "ETH0")
    char ip[16];            // IP address (string)
    char mask[16];          // Subnet mask
    char gateway[16];       // Default gateway
    bool active;            // Interface active
    uint8_t reserved[3];    // Alignment padding
} NetInterface;
```

#### 5. Disk Drive

```c
typedef struct DiskDrive {
    char letter;            // Drive letter (A, C, D, etc.)
    char label[32];         // Volume label
    uint32_t total_mb;      // Total capacity (MB)
    uint32_t free_mb;       // Free space (MB)
    uint8_t type;           // 0=HDD, 1=FDD, 2=CD
    uint8_t reserved[3];    // Alignment padding
} DiskDrive;
```

### FAT12 Structures

#### 1. BIOS Parameter Block (BPB)

```c
typedef struct BPB {
    uint8_t  jmp[3];            // Jump instruction
    char     oem[8];            // OEM name
    uint16_t bytes_per_sector;  // Usually 512
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) BPB;
```

#### 2. Directory Entry

```c
typedef struct DirEntry {
    char     filename[11];      // 8.3 format (no dot)
    uint8_t  attributes;        // File attributes
    uint8_t  reserved;
    uint8_t  creation_time_ms;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; // FAT32 only
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) DirEntry;
```

#### 3. File Handle

```c
typedef struct FileHandle {
    uint32_t first_cluster;     // Starting cluster number
    uint32_t file_size;         // Total file size
    uint32_t current_cluster;   // Current cluster being read
    uint32_t position;          // Current byte offset
    void*    cluster_buffer;    // Buffer for cluster data
    uint32_t cluster_offset;    // Offset within current cluster
    uint32_t flags;             // Status flags
    uint32_t reserved;          // Future use
} FileHandle;

#define FH_FLAG_INUSE   0x01
#define FH_FLAG_EOF     0x02
#define FH_FLAG_ERROR   0x04
```

### System Information Structures

#### 1. System Info

```c
typedef struct SysInfo {
    uint32_t total_memory;      // Total RAM (bytes)
    uint32_t free_memory;       // Free RAM (bytes)
    uint32_t used_memory;       // Used RAM (bytes)
    uint32_t kernel_memory;     // Kernel usage (bytes)
    uint32_t uptime_seconds;    // System uptime
    uint32_t num_processes;     // Active process count
} SysInfo;
```

#### 2. Memory Statistics

```c
typedef struct MemStats {
    uint32_t total_free;        // Total free memory
    uint32_t total_used;        // Total used memory
    uint32_t num_blocks;        // Number of blocks
    uint32_t largest_free;      // Largest free block
    uint32_t fragmentation;     // Fragmentation percentage
} MemStats;
```

---

## API Reference

### Memory Management API

#### kmalloc

```c
void* kmalloc(uint32_t size);
```

**Description**: Allocate memory from kernel heap

**Parameters**:
- `size`: Number of bytes to allocate

**Returns**: Pointer to allocated memory, or NULL on failure

**Example**:
```c
char* buffer = (char*)kmalloc(1024);
if (buffer == NULL) {
    // Out of memory
}
```

#### kfree

```c
void kfree(void* ptr);
```

**Description**: Free previously allocated memory

**Parameters**:
- `ptr`: Pointer returned by kmalloc

**Returns**: Nothing

**Example**:
```c
kfree(buffer);
```

#### mem_get_stats

```c
void mem_get_stats(uint32_t* stats);
```

**Description**: Get memory statistics

**Parameters**:
- `stats`: Pointer to 3-element array for results
  - stats[0]: Total free memory
  - stats[1]: Total used memory
  - stats[2]: Number of blocks

**Returns**: Nothing (fills stats array)

**Example**:
```c
uint32_t stats[3];
mem_get_stats(stats);
printf("Free: %u, Used: %u\n", stats[0], stats[1]);
```

### File System API

#### fs_init

```asm
; Input: EAX = boot_sector_lba, EBX = temp_buffer
; Returns: EAX = 0 on success
```

**Description**: Initialize FAT12 file system

#### fs_list_root

```asm
; Input: EAX = output_buffer, EBX = max_entries
; Returns: EAX = number of entries read
```

**Description**: List root directory entries

#### fs_find_file

```asm
; Input: EAX = filename pointer (11 bytes, uppercase)
; Returns: EAX = entry index or -1
```

**Description**: Find file in root directory

#### fs_open

```asm
; Input: EAX = filename, EBX = mode, ECX = handle_out
; Returns: EAX = 0 on success, -1 on error
```

**Description**: Open file for reading

#### fs_read

```asm
; Input: EAX = handle, EBX = buffer, ECX = bytes, EDX = bytes_read
; Returns: EAX = 0 on success
```

**Description**: Read from open file

#### fs_close

```asm
; Input: EAX = handle
; Returns: EAX = 0 on success
```

**Description**: Close file handle

### I/O API

#### putc

```c
void putc(char c);
```

**Description**: Print single character to console

**Parameters**:
- `c`: Character to print

**Special Characters**:
- `\n` (10): New line
- `\r` (13): Carriage return
- `\b` (8): Backspace

**Example**:
```c
putc('H');
putc('i');
putc('\n');
```

#### puts

```c
void puts(const char* str);
```

**Description**: Print null-terminated string

**Parameters**:
- `str`: String to print

**Example**:
```c
puts("Hello, RO-DOS!\n");
```

#### cls

```c
void cls(void);
```

**Description**: Clear screen and reset cursor

**Example**:
```c
cls();
```

#### set_attr

```c
void set_attr(uint8_t attr);
```

**Description**: Set text color attributes

**Parameters**:
- `attr`: Color attribute byte
  - Low nibble (0-3): Foreground color
  - High nibble (4-7): Background color

**Example**:
```c
set_attr(0x0A);  // Light green on black
puts("Green text!");
set_attr(0x07);  // Reset to gray on black
```

#### getkey

```c
uint16_t getkey(void);
```

**Description**: Read character from keyboard (blocking)

**Returns**: ASCII character code

**Example**:
```c
uint16_t key = getkey();
char c = (char)(key & 0xFF);
```

### System Call API

#### sys_get_time

```c
int sys_get_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds);
```

**Description**: Read current time from RTC

**Parameters**:
- `hours`: Pointer to store hours (0-23)
- `minutes`: Pointer to store minutes (0-59)
- `seconds`: Pointer to store seconds (0-59)

**Returns**: 0 on success

**Example**:
```c
uint8_t h, m, s;
sys_get_time(&h, &m, &s);
printf("%02d:%02d:%02d\n", h, m, s);
```

#### sys_get_date

```c
int sys_get_date(uint8_t* day, uint8_t* month, uint16_t* year);
```

**Description**: Read current date from RTC

**Parameters**:
- `day`: Pointer to store day (1-31)
- `month`: Pointer to store month (1-12)
- `year`: Pointer to store year (2000+)

**Returns**: 0 on success

**Example**:
```c
uint8_t d, m;
uint16_t y;
sys_get_date(&d, &m, &y);
printf("%02d/%02d/%04d\n", m, d, y);
```

#### sys_getpid

```c
int sys_getpid(void);
```

**Description**: Get current process ID

**Returns**: Process ID (currently fixed at 2 for shell)

**Example**:
```c
int pid = sys_getpid();
printf("PID: %d\n", pid);
```

#### sys_read_sector

```c
int sys_read_sector(uint32_t lba, uint32_t count, void* buffer);
```

**Description**: Read disk sectors using ATA PIO

**Parameters**:
- `lba`: Logical block address (sector number)
- `count`: Number of sectors to read
- `buffer`: Buffer to store data (must be count * 512 bytes)

**Returns**: 0 on success, -1 on error

**Example**:
```c
uint8_t sector[512];
if (sys_read_sector(0, 1, sector) == 0) {
    // Successfully read boot sector
}
```

#### sys_sysinfo

```c
int sys_sysinfo(void* info);
```

**Description**: Get system information

**Parameters**:
- `info`: Pointer to SysInfo structure

**Returns**: 0 on success

**Example**:
```c
SysInfo sysinfo;
sys_sysinfo(&sysinfo);
printf("Uptime: %u seconds\n", sysinfo.uptime_seconds);
```

#### sys_beep

```c
int sys_beep(uint32_t frequency, uint32_t duration);
```

**Description**: Generate PC speaker beep

**Parameters**:
- `frequency`: Frequency in Hz (20-20000)
- `duration`: Duration in milliseconds

**Returns**: 0 on success

**Example**:
```c
sys_beep(800, 200);  // 800 Hz for 200ms
```

### Utility Functions

#### strlen

```c
size_t strlen(const char* str);
```

**Description**: Calculate string length

**Example**:
```c
size_t len = strlen("Hello");  // Returns 5
```

#### strcmp

```c
int strcmp(const char* s1, const char* s2);
```

**Description**: Compare two strings

**Returns**: 
- 0 if equal
- <0 if s1 < s2
- >0 if s1 > s2

**Example**:
```c
if (strcmp(cmd, "DIR") == 0) {
    // Command is DIR
}
```

#### strcpy

```c
char* strcpy(char* dest, const char* src);
```

**Description**: Copy string

**Example**:
```c
char buffer[64];
strcpy(buffer, "Hello");
```

#### strcat

```c
char* strcat(char* dest, const char* src);
```

**Description**: Concatenate strings

**Example**:
```c
char path[256] = "C:\\";
strcat(path, "DOCS\\");
```

#### memset

```c
void* memset(void* ptr, int value, size_t num);
```

**Description**: Fill memory with byte value

**Example**:
```c
char buffer[100];
memset(buffer, 0, sizeof(buffer));
```

#### memcpy

```c
void* memcpy(void* dest, const void* src, size_t num);
```

**Description**: Copy memory block

**Example**:
```c
uint8_t buffer[512];
memcpy(buffer, disk_data, 512);
```

---

## Hardware Interfaces

### Programmable Interrupt Controller (PIC)

**Ports**:
- Master PIC Command: 0x20
- Master PIC Data: 0x21
- Slave PIC Command: 0xA0
- Slave PIC Data: 0xA1

**Initialization Sequence** (ICW1-ICW4):

```asm
; ICW1 - Start initialization
mov al, 0x11
out 0x20, al    ; Master
out 0xA0, al    ; Slave

; ICW2 - Vector offsets
mov al, 0x20
out 0x21, al    ; Master: INT 0x20-0x27
mov al, 0x28
out 0xA1, al    ; Slave: INT 0x28-0x2F

; ICW3 - Cascade configuration
mov al, 0x04
out 0x21, al    ; Master: IRQ2 has slave
mov al, 0x02
out 0xA1, al    ; Slave: cascade identity

; ICW4 - Mode
mov al, 0x01
out 0x21, al    ; 8086 mode
out 0xA1, al

; Set interrupt masks
mov al, 0xFC    ; Enable IRQ0, IRQ1
out 0x21, al
mov al, 0xFF    ; Mask all slave IRQs
out 0xA1, al
```

### Programmable Interval Timer (PIT)

**Ports**:
- Channel 0 Data: 0x40
- Channel 1 Data: 0x41
- Channel 2 Data: 0x42
- Mode/Command: 0x43

**Frequency**: 1.193182 MHz (base)

**Timer Setup** (18.2 Hz):

```asm
; Channel 0, Mode 3 (square wave)
mov al, 0x36
out 0x43, al

; Divisor = 65535 (maximum)
mov al, 0xFF
out 0x40, al
mov al, 0xFF
out 0x40, al
```

### Real-Time Clock (RTC/CMOS)

**Ports**:
- CMOS Address: 0x70
- CMOS Data: 0x71

**Registers**:

| Address | Register | Description |
|---------|----------|-------------|
| 0x00 | Seconds | Current seconds (BCD) |
| 0x02 | Minutes | Current minutes (BCD) |
| 0x04 | Hours | Current hours (BCD) |
| 0x07 | Day | Day of month (BCD) |
| 0x08 | Month | Month (BCD) |
| 0x09 | Year | Year (BCD, 00-99) |
| 0x32 | Century | Century (BCD) |

**Reading CMOS**:

```asm
mov al, 0x00        ; Seconds register
out 0x70, al
in al, 0x71         ; Read value
```

### ATA (IDE) Disk Controller

**Primary ATA Ports**:
- Data: 0x1F0
- Error/Features: 0x1F1
- Sector Count: 0x1F2
- LBA Low: 0x1F3
- LBA Mid: 0x1F4
- LBA High: 0x1F5
- Drive/Head: 0x1F6
- Status/Command: 0x1F7

**LBA28 Read Sectors**:

```asm
; Select drive and LBA bits 24-27
mov al, 0xE0
or al, [lba_bits_24_27]
out 0x1F6, al

; Sector count
mov al, [sector_count]
out 0x1F2, al

; LBA bytes
mov al, [lba_low]
out 0x1F3, al
mov al, [lba_mid]
out 0x1F4, al
mov al, [lba_high]
out 0x1F5, al

; Send READ command (0x20)
mov al, 0x20
out 0x1F7, al

; Wait for drive ready
.wait:
    in al, 0x1F7
    test al, 0x80       ; BSY bit
    jnz .wait
    test al, 0x08       ; DRQ bit
    jz .wait

; Read 256 words (512 bytes)
mov cx, 256
mov dx, 0x1F0
rep insw
```

### Keyboard Controller

**Ports**:
- Data: 0x60
- Status/Command: 0x64

**Status Register Bits**:
- Bit 0: Output buffer full
- Bit 1: Input buffer full
- Bit 2: System flag
- Bit 5: Timeout error
- Bit 6: Parity error

**Scancode Sets**: Set 1 (XT) used by default

---

## Performance Considerations

### Memory Allocation

**Best Practices**:
1. Allocate once, reuse frequently
2. Free memory in reverse allocation order when possible
3. Use appropriate sizes to minimize fragmentation
4. Consider block splitting threshold (currently 32 bytes)

**Fragmentation Management**:
- Coalescing happens automatically on free
- First-fit strategy balances speed vs fragmentation
- Consider periodic defragmentation for long-running systems

### File System

**Optimization Techniques**:
1. Cache FAT table in memory
2. Buffer directory entries
3. Align cluster reads to sector boundaries
4. Minimize seeks by organizing data sequentially

### Interrupt Handling

**Performance Tips**:
1. Keep ISRs short and fast
2. Defer complex processing to handlers
3. Use ring buffers for I/O (keyboard, serial)
4. Minimize critical sections (CLI/STI)

---

## Future Enhancements

### Planned Features

1. **Multitasking**
   - Task scheduler (round-robin or priority-based)
   - Context switching
   - Process isolation

2. **Memory Management**
   - Paging support (4KB pages)
   - Virtual memory
   - User/kernel space separation

3. **File System**
   - FAT16/FAT32 support
   - Subdirectory navigation
   - File write operations
   - Volume mounting

4. **Device Drivers**
   - Floppy disk controller
   - Serial port (COM1/COM2)
   - Parallel port (LPT1)
   - Sound Blaster

5. **Networking**
   - Real network driver (RTL8139, NE2000)
   - TCP/IP stack implementation
   - Socket API

6. **Graphics**
   - VGA graphics modes (320x200, 640x480)
   - Basic GUI framework
   - Mouse support

---

## Debugging and Development

### Debug Techniques

**Serial Port Logging**:
```c
void debug_log(const char* msg) {
    // Output to COM1 (0x3F8)
    while (*msg) {
        while (!(inb(0x3FD) & 0x20));
        outb(0x3F8, *msg++);
    }
}
```

**Memory Validation**:
```asm
; Check heap integrity
call mem_validate_heap
test eax, eax
jnz heap_corrupted
```

**Breakpoint Macro**:
```c
#define BREAKPOINT() __asm__ volatile("xchg %bx, %bx")
```

### Testing Checklist

- [ ] Boot sequence completes
- [ ] Interrupts working (timer, keyboard)
- [ ] Memory allocation/deallocation
- [ ] File system operations
- [ ] Command execution
- [ ] System calls functional
- [ ] No memory leaks
- [ ] No crashes on invalid input

---

## References and Resources

### Documentation

- Intel 80386 Programmer's Reference Manual
- IBM PC/AT Technical Reference
- OSDev Wiki (osdev.org)
- NASM Documentation
- GCC Inline Assembly Guide

### Specifications

- FAT File System Specification (Microsoft)
- VGA Programming Guide
- ATA/ATAPI Specification
- PS/2 Keyboard Interface

### Development Tools

- QEMU Emulator Documentation
- GDB Debugging Guide
- Bochs Emulator (alternative)
- GRUB Bootloader (for future multiboot)

---

## Glossary

**A20 Gate**: Hardware mechanism to access memory above 1MB

**BPB**: BIOS Parameter Block - describes filesystem layout

**CHS**: Cylinder-Head-Sector addressing (legacy disk addressing)

**GDT**: Global Descriptor Table - defines memory segments in protected mode

**IDT**: Interrupt Descriptor Table - maps interrupts to handlers

**ISR**: Interrupt Service Routine - handler for hardware/software interrupts

**LBA**: Logical Block Addressing - linear sector numbering

**MCB**: Memory Control Block - heap block metadata structure

**PIC**: Programmable Interrupt Controller (8259A)

**PIT**: Programmable Interval Timer (8253/8254)

**RTC**: Real-Time Clock - CMOS clock chip

**VGA**: Video Graphics Array - display adapter standard

---

**End of RO-DOS System Architecture Documentation**

*For updates and contributions, visit the project repository*

*Last Updated: December 2025*