BITS 32

extern timer_handler
extern syscall_handler
extern isr_handler
extern pic_remap

%define PIC1_CMD    0x20
%define PIC1_DATA   0x21
%define EOI         0x20

section .data
align 4
key_buffer_size equ 256
key_buffer:     times 256 db 0
key_buffer_head dd 0
key_buffer_tail dd 0

kb_shift db 0
kb_caps  db 0

scan_to_ascii:
    db 0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9
    db 'q','w','e','r','t','y','u','i','o','p','[',']',13,0
    db 'a','s','d','f','g','h','j','k','l',';',39,96,0,92
    db 'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    times (128-104) db 0

scan_to_ascii_shift:
    db 0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9
    db 'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0
    db 'A','S','D','F','G','H','J','K','L',':',34,'~',0,'|'
    db 'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    times (128-104) db 0

section .text
global getkey_block
global c_getkey
global init_interrupts
global syscall_stub

; Common ISR Stub
isr_common_stub:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    push esp
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8
    iretd

; Common IRQ Stub
irq_common_stub:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov eax, [esp + 48] ; Get Int No from stack

    cmp eax, 33         ; Check if Keyboard (IRQ 1)
    jne .timer_check

    in al, 0x60         ; Read scancode
    
    ; Handle Release (Break) Codes
    test al, 0x80
    jnz .handle_release

    ; Handle Press (Make) Codes
    cmp al, 0x2A ; L-Shift
    je .shift_on
    cmp al, 0x36 ; R-Shift
    je .shift_on
    cmp al, 0x3A ; Caps Lock
    je .caps_toggle

    movzx ebx, al
    lea esi, [scan_to_ascii]
    test byte [kb_shift], 1
    jz .no_shift
    lea esi, [scan_to_ascii_shift]
.no_shift:
    mov al, [esi + ebx]
    test al, al
    jz .done_irq
    
    ; Shift XOR Caps logic for letter casing
    cmp al, 'a'
    jl .check_upper
    cmp al, 'z'
    jg .check_upper
    mov cl, [kb_shift]
    xor cl, [kb_caps]
    test cl, 1
    jz .store
    sub al, 32 ; Make Big
    jmp .store

.check_upper:
    cmp al, 'A'
    jl .store
    cmp al, 'Z'
    jg .store
    mov cl, [kb_shift]
    xor cl, [kb_caps]
    test cl, 1
    jz .store
    add al, 32 ; Make Small

.store:
    mov ebx, [key_buffer_head]
    mov [key_buffer + ebx], al
    inc ebx
    and ebx, 255
    mov [key_buffer_head], ebx
    jmp .done_irq

.shift_on:
    mov byte [kb_shift], 1
    jmp .done_irq
.caps_toggle:
    xor byte [kb_caps], 1
    jmp .done_irq
.handle_release:
    and al, 0x7F
    cmp al, 0x2A
    je .shift_off
    cmp al, 0x36
    je .shift_off
    jmp .done_irq
.shift_off:
    mov byte [kb_shift], 0
    jmp .done_irq

.timer_check:
    push esp
    call timer_handler
    add esp, 4

.done_irq:
    mov al, 0x20
    out 0x20, al
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8
    iretd

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro IRQ_STUB 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp irq_common_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 13
IRQ_STUB 0, 32
IRQ_STUB 1, 33

getkey_block:
.wait:
    cli
    mov eax, [key_buffer_tail]
    cmp eax, [key_buffer_head]
    jne .ready
    sti
    hlt
    jmp .wait
.ready:
    movzx eax, byte [key_buffer + eax]
    mov ebx, [key_buffer_tail]
    inc ebx
    and ebx, 255
    mov [key_buffer_tail], ebx
    sti
    ret

c_getkey: jmp getkey_block

syscall_stub:
    push dword 0
    push dword 0x80
    jmp isr_common_stub

install_isr:
    ; EAX = Vector, EBX = Addr, CL = Flags
    push edi
    mov edi, idt_table
    shl eax, 3
    add edi, eax
    mov [edi], bx       ; Low 16 bits
    mov word [edi+2], 0x08 ; Selector
    mov byte [edi+4], 0
    mov [edi+5], cl     ; Flags
    shr ebx, 16         ; Get High 16 bits
    mov [edi+6], bx
    pop edi
    ret

init_interrupts:
    call pic_remap
    
    mov eax, 0
    mov ebx, isr0
    mov cl, 0x8E
    call install_isr

    mov eax, 13
    mov ebx, isr13
    mov cl, 0x8E
    call install_isr

    mov eax, 32
    mov ebx, irq0
    mov cl, 0x8E
    call install_isr

    mov eax, 33
    mov ebx, irq1
    mov cl, 0x8E
    call install_isr

    mov eax, 0x80
    mov ebx, syscall_stub
    mov cl, 0xEE
    call install_isr

    lidt [idt_desc]
    ret

align 16
idt_table: times 256 dq 0
idt_desc:
    dw 256*8-1
    dd idt_table