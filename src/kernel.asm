[BITS 32]
[EXTERN init_interrupts]
[EXTERN io_init]
[EXTERN mem_init]
[EXTERN shell_main]
[EXTERN get_ticks]
[EXTERN puts]
[EXTERN putc]
[EXTERN cls]
[EXTERN getkey_block]
[EXTERN io_set_attr]
[EXTERN c_puts]
[EXTERN set_attr]

[GLOBAL kernel_entry]
[GLOBAL sys_reboot]

section .text.start
kernel_entry:
    cli

    ; Setup segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Setup stack in kernel data area
    mov esp, 0x00090000
    mov ebp, esp

    cld

    ; mask PICs fully initially
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al

    ; initialize idt / pic
    call init_interrupts

    ; init io
    call io_init

    push dword kernel_ok_msg
    call puts
    add esp, 4

    push dword system_ready_msg
    call puts
    add esp, 4

    ; init mem - Increased heap size to 16MB for better memory availability
    mov eax, 0x00200000
    mov ebx, 0x01000000     ; 16MB heap instead of 2MB
    call mem_init

    push dword mem_init_msg
    call puts
    add esp, 4

    push dword enabling_int_msg
    call puts
    add esp, 4

    ; Unmask timer, keyboard, and cascade to slave (enable IRQ0, IRQ1, IRQ2)
    mov al, 0xF8        ; 11111000 - IRQ0, IRQ1, IRQ2 enabled
    out 0x21, al
    ; Unmask IRQ12 on slave PIC for PS/2 mouse
    mov al, 0xEF        ; 11101111 - IRQ12 enabled (bit 4 = IRQ12)
    out 0xA1, al

    sti

    ; small delay
    mov ecx, 5000000
.delay:
    dec ecx
    jnz .delay

    push dword calling_shell_msg
    call puts
    add esp, 4

    ; jump to shell
    call shell_main

    cli
    push dword shell_exit_msg
    call puts
    add esp, 4

.hang:
    hlt
    jmp .hang

sys_reboot:
    cli
    mov al, 0xFE
    out 0x64, al
    hlt
    jmp $

section .rodata
kernel_ok_msg       db "RO-DOS Kernel v1.2 Beta", 13, 10, 0
system_ready_msg    db "System initialized", 13, 10, 0
mem_init_msg        db "Memory manager ready", 13, 10, 0
enabling_int_msg    db "Enabling interrupts...", 13, 10, 0
calling_shell_msg   db "Starting shell...", 13, 10, 13, 10, 0
shell_exit_msg      db 13, 10, "Shell exited.", 13, 10, 0