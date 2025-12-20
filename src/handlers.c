#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* PIC ports and constants */
#define PIC1_CMD     0x20
#define PIC1_DATA    0x21
#define PIC2_CMD     0xA0
#define PIC2_DATA    0xA1
#define EOI          0x20

/* ATA PIO Ports */
#define ATA_DATA         0x1F0
#define ATA_ERROR        0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW      0x1F3
#define ATA_LBA_MID      0x1F4
#define ATA_LBA_HIGH     0x1F5
#define ATA_DRIVE_HEAD   0x1F6
#define ATA_STATUS       0x1F7
#define ATA_COMMAND      0x1F7

#define ATA_SR_BSY       0x80
#define ATA_SR_DRQ       0x08
#define ATA_SR_ERR       0x01

/* External kernel/IO functions */
extern void c_puts(const char* s);
extern void set_attr(uint8_t a);

/* Register structure */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} registers_t;

/* Global system state */
static volatile uint32_t timer_ticks = 0;

/* I/O primitives */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %1, %0" : "=a"(ret) : "dN"(port) );
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    /* Using %b0 forces the compiler to use AL (the byte register) */
    __asm__ volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) );
}

static inline void insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile (
        "cld; rep insw"
        : "+D"(addr), "+c"(count)
        : "d"(port)
        : "memory"
    );
}

/*
 PIC remap
*/
void pic_remap(void) {
    /* ICW1 - start initialization */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    /* ICW2 - set vector offsets */
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    /* ICW3 - setup cascading */
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    /* ICW4 - 8086/88 mode */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Masks: IRQ0 (timer) and IRQ1 (keyboard) enabled on Master */
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

void timer_handler(registers_t *regs) {
    (void)regs;
    timer_ticks++;
}

void isr_handler(registers_t *regs) {
    (void)regs;
    c_puts("\nCPU EXCEPTION - SYSTEM HALTED\n");
    __asm__ volatile("cli");
    for (;;) { __asm__ volatile("hlt"); }
}

/* Utility */
uint32_t get_ticks(void) {
    return timer_ticks;
}


int disk_read_lba(uint32_t lba, uint32_t count, void* buffer) {
    uint8_t status;
    uint16_t *buf_ptr = (uint16_t*)buffer;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, (uint8_t)count);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, 0x20);

    for (int k = 0; k < 4; ++k) { (void)inb(ATA_STATUS); }

    for (uint32_t i = 0; i < count; ++i) {
        while (1) {
            status = inb(ATA_STATUS);
            if (status & ATA_SR_ERR) return -1;
            if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
        }
        insw(ATA_DATA, buf_ptr, 256);
        buf_ptr += 256;
    }
    return 0;
}