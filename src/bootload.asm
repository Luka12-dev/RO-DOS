BITS 16
org 0x7C00

%define SECTORS_PER_TRACK 18
%define HEADS_PER_CYL     2

%ifndef KERNEL_LBA
%define KERNEL_LBA 1
%endif
%ifndef KERNEL_DEST
%define KERNEL_DEST 0x10000
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif

start:
    jmp short code_start
    nop

; BPB 
oem_id        db 'RO-DOS  '
bytes_per_sec dw 512
sec_per_cl    db 1
reserved_sec  dw 1
num_fats      db 2
root_entries  dw 224
total_sec     dw 2880
media_desc    db 0xF0
sec_per_fat   dw 9
sec_per_trk   dw SECTORS_PER_TRACK
num_heads     dw HEADS_PER_CYL
hidden_sec    dd 0
total_sec_big dd 0
drive_num     db 0
reserved      db 0
boot_sig      db 0x29
vol_id        dd 0
vol_label     db 'RO-DOS BOOT'
fs_type       db 'FAT12   '

code_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [drive_num], dl

    mov dword [kernel_lba], KERNEL_LBA
    mov word  [kernel_sectors], KERNEL_SECTORS
    mov dword [kernel_dest], KERNEL_DEST
    call read_sectors_lba

    ; Enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Load GDT
    lgdt [gdtr]

    ; Enter protected mode
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump to 32-bit code segment
    jmp 0x08:pmode_entry

BITS 32
pmode_entry:
    ; Setup 32-bit flat segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Jump to kernel entry point
    jmp 0x08:KERNEL_DEST

; Read kernel from disk (LBA)

BITS 16
read_sectors_lba:
    pusha
    push ds
    push es
    xor ax, ax
    mov ds, ax

.load_loop:
    cmp word [kernel_sectors], 0
    je .done

    ; Compute ES:BX from linear kernel_dest
    mov ax, word [kernel_dest]
    mov dx, word [kernel_dest+2]
    mov cx, ax
    shr cx, 4
    mov bx, dx
    shl bx, 12
    or  bx, cx
    mov es, bx
    mov bx, ax
    and bx, 0x0F

    ; Compute CHS from kernel_lba
    mov ax, [kernel_lba]
    xor dx, dx
    mov cx, SECTORS_PER_TRACK*HEADS_PER_CYL
    div cx
    mov bp, ax
    mov ax, dx
    mov cx, SECTORS_PER_TRACK
    xor dx, dx
    div cx
    mov dh, al
    mov cl, dl
    inc cl
    mov ax, bp
    mov ch, al
    shr ax, 8
    and al, 0x03
    shl al, 6
    or cl, al

    mov al, 1
    mov ah, 0x02
    mov dl, [drive_num]

    mov di, 3
.read_retry:
    push ax
    push bx
    push cx
    push dx
    int 0x13
    pop dx
    pop cx
    pop bx
    pop ax
    jnc .read_ok

    mov ah, 0
    int 0x13
    dec di
    jnz .read_retry
    jmp .disk_error

.read_ok:
    inc dword [kernel_lba]
    dec word  [kernel_sectors]
    add word  [kernel_dest], 512
    adc word  [kernel_dest+2], 0
    jmp .load_loop

.disk_error:
    cli
.hang:
    hlt
    jmp .hang

.done:
    pop es
    pop ds
    popa
    ret

; GDT - Flat 32-bit

ALIGN 8
gdt_start:
    dq 0x0000000000000000        ; Null
    dq 0x00CF9A000000FFFF        ; Code segment, 0x08
    dq 0x00CF92000000FFFF        ; Data segment, 0x10
gdt_end:

gdtr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Variables
kernel_lba      dd 0
kernel_sectors  dw 0
kernel_dest     dd 0

times 510-($-$$) db 0
dw 0xAA55