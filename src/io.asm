BITS 32

section .data
align 4

screen_cols     dd 80
screen_rows     dd 25
video_base      dd 0xB8000
default_attr    db 0x07

cursor_row      dd 0
cursor_col      dd 0

VGA_INDEX       equ 0x3D4
VGA_DATA        equ 0x3D5
CURSOR_HIGH     equ 0x0E
CURSOR_LOW      equ 0x0F

section .text
  global c_putc
  global c_puts
  global c_cls
  global set_attr
  global io_wait
global io_init
global putc
global puts
global cls
global io_set_attr
global set_cursor_pos
global set_cursor_hardware
global cursor_row
global cursor_col

; Hardware Cursor Update
set_cursor_hardware:
    push eax
    push ebx
    push edx

    mov eax, [cursor_row]
    imul eax, dword [screen_cols]
    add eax, [cursor_col]
    mov ebx, eax

    mov dx, VGA_INDEX
    mov al, CURSOR_HIGH
    out dx, al
    mov dx, VGA_DATA
    mov al, bh
    out dx, al

    mov dx, VGA_INDEX
    mov al, CURSOR_LOW
    out dx, al
    mov dx, VGA_DATA
    mov al, bl
    out dx, al

    pop edx
    pop ebx
    pop eax
    ret

; Init
io_init:
    pusha
    mov dword [cursor_row], 0
    mov dword [cursor_col], 0
    call cls
    call set_cursor_hardware
    popa
    ret

; Clear Screen
cls:
    pusha
    mov edi, [video_base]
    mov ecx, 2000  ; 80x25 = 2000 characters
    mov ah, [default_attr]
    mov al, ' '
    rep stosw
    mov dword [cursor_row], 0
    mov dword [cursor_col], 0
    call set_cursor_hardware
    popa
    ret

; Scroll Up
scroll_up:
    pusha
    
    ; Capture top line to scrollback before scrolling
    extern scrollback_capture_line
    call scrollback_capture_line
    
    mov edi, [video_base]
    mov esi, [video_base]
    add esi, 160  ; One line = 80 chars * 2 bytes = 160
    mov ecx, 1920  ; 24 lines * 80 = 1920
    rep movsw
    
    mov edi, [video_base]
    add edi, 3840  ; 24 lines * 160 bytes = 3840
    mov ecx, 80
    mov ah, [default_attr]
    mov al, ' '
    rep stosw
    popa
    ret

; Put Char
putc:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edi

    mov al, byte [ebp + 8]

    cmp al, 0x0A
    je .nl
    cmp al, 0x0D
    je .cr
    cmp al, 0x08
    je .bs

    mov ecx, [cursor_row]
    imul ecx, 80
    add ecx, [cursor_col]
    shl ecx, 1
    mov edi, [video_base]
    add edi, ecx
    
    mov ah, [default_attr]
    mov [edi], ax
    
    inc dword [cursor_col]
    jmp .check_wrap

.bs:
    cmp dword [cursor_col], 0
    je .done
    dec dword [cursor_col]
    mov ecx, [cursor_row]
    imul ecx, 80
    add ecx, [cursor_col]
    shl ecx, 1
    mov edi, [video_base]
    add edi, ecx
    mov al, ' '
    mov ah, [default_attr]
    mov [edi], ax
    jmp .done

.cr:
    mov dword [cursor_col], 0
    jmp .done

.nl:
    mov dword [cursor_col], 0
    inc dword [cursor_row]
    jmp .check_scroll

.check_wrap:
    cmp dword [cursor_col], 80
    jl .done
    mov dword [cursor_col], 0
    inc dword [cursor_row]

.check_scroll:
    cmp dword [cursor_row], 25
    jl .done
    call scroll_up
    mov dword [cursor_row], 24

.done:
    call set_cursor_hardware
    pop edi
    pop ecx
    pop ebx
    pop eax
    leave
    ret

; Puts
puts:
    push ebp
    mov ebp, esp
    push esi
    push eax
    
    mov esi, [ebp + 8]
.loop:
    mov al, [esi]
    test al, al
    jz .end
    
    push eax
    call putc
    add esp, 4
    
    inc esi
    jmp .loop
.end:
    pop eax
    pop esi
    leave
    ret

; Set Attribute
io_set_attr:
    push ebp
    mov ebp, esp
    mov al, [ebp+8]
    mov [default_attr], al
    leave
    ret

set_cursor_pos:
    ret

; Provide names expected by C code (commands.c, handlers.c)
; They simply jump to the actual implementations above.

c_putc:
    jmp putc

c_puts:
    jmp puts

c_cls:
    jmp cls

set_attr:
    jmp io_set_attr

; Small I/O wait (used by PIC remap). Traditional 0x80 port delay.
io_wait:
    push eax
    mov al, 0
    out 0x80, al
    pop eax
    ret