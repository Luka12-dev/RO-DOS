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
    call read_sectors
    
    ; Clear VESA info - kernel will detect no VESA and use VGA fallback
    ; GUI apps will set VESA mode when launched
    mov dword [0x9000], 0
    mov dword [0x9004], 0
    mov dword [0x9008], 0
    
    ; Enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdtr]

    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    jmp 0x08:pmode_entry

BITS 32
pmode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    jmp 0x08:KERNEL_DEST

; Read kernel using CHS (works on all BIOS)
BITS 16
read_sectors:
    pusha
    push es

.loop:
    cmp word [kernel_sectors], 0
    je .done

    ; ES:BX = destination
    mov ax, word [kernel_dest+2]
    mov bx, word [kernel_dest]
    shr bx, 4
    shl ax, 12
    or ax, bx
    mov es, ax
    mov bx, word [kernel_dest]
    and bx, 0x0F

    ; LBA to CHS
    mov ax, [kernel_lba]
    xor dx, dx
    div word [sec_per_trk]
    mov cl, dl
    inc cl
    xor dx, dx
    div word [num_heads]
    mov ch, al
    mov dh, dl
    mov dl, [drive_num]

    mov ax, 0x0201
    int 0x13
    jc .retry

    inc word [kernel_lba]
    dec word [kernel_sectors]
    add word [kernel_dest], 512
    adc word [kernel_dest+2], 0
    jmp .loop

.retry:
    xor ax, ax
    int 0x13
    jmp .loop

.done:
    pop es
    popa
    ret

; GDT
ALIGN 4
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdtr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

kernel_lba      dw 0
kernel_sectors  dw 0
kernel_dest     dd 0

times 510-($-$$) db 0
dw 0xAA55