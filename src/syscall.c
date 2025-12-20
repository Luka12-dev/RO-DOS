#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Error Codes */
#define E_OK                0   /* Success */
#define E_INVAL             1   /* Invalid parameter */
#define E_NOENT             2   /* No such file or directory */
#define E_ACCESS            3   /* Access denied */
#define E_NOMEM             4   /* Out of memory */
#define E_NOSPC             5   /* No space left on device */
#define E_EXIST             6   /* File exists */
#define E_NOTDIR            7   /* Not a directory */
#define E_ISDIR             8   /* Is a directory */
#define E_BADF              9   /* Bad file descriptor */
#define E_IO                10  /* I/O error */
#define E_BUSY              11  /* Device busy */
#define E_AGAIN             12  /* Try again */
#define E_NOTSUPP           13  /* Operation not supported */
#define E_PERM              14  /* Operation not permitted */

/* System Call Numbers */
#define SYS_PRINT_STRING    0x01
#define SYS_PRINT_CHAR      0x02
#define SYS_READ_CHAR       0x04
#define SYS_CLEAR_SCREEN    0x05
#define SYS_SET_COLOR       0x08
#define SYS_CHDIR           0x1A
#define SYS_GETCWD          0x1B
#define SYS_OPENDIR         0x1D
#define SYS_READDIR         0x1E
#define SYS_CLOSEDIR        0x1F
#define SYS_GETPID          0x44
#define SYS_GET_TIME        0x50
#define SYS_GET_DATE        0x51
#define SYS_GET_TICKS       0x52
#define SYS_SYSINFO         0x54
#define SYS_UNAME           0x55
#define SYS_READ_SECTOR     0x61
#define SYS_SHUTDOWN        0x71
#define SYS_BEEP            0x72
#define SYS_DEBUG           0x73

/* External Kernel Functions */
extern void puts(const char* s);
extern void putc(char c);
extern void cls(void);
extern void io_set_attr(uint8_t a);
extern int getkey_block(void);
extern uint32_t get_ticks(void);
extern int disk_read_lba(uint32_t lba, uint32_t count, void* buffer);

/* CMOS / RTC Helpers */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %w1, %b0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static uint8_t get_cmos_reg(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

/* System call dispatcher invoked from interrupt.asm */
int syscall_handler(int num, int arg1, int arg2, int arg3) {
    (void)arg2; 
    (void)arg3;

    switch (num) {
        case SYS_PRINT_STRING:
            if ((const char*)arg1) puts((const char*)arg1);
            return E_OK;

        case SYS_PRINT_CHAR:
            putc((char)(arg1 & 0xFF));
            return E_OK;

        case SYS_READ_CHAR:
            return getkey_block();

        case SYS_CLEAR_SCREEN:
            cls();
            return E_OK;

        case SYS_SET_COLOR:
            io_set_attr((uint8_t)(arg1 & 0xFF));
            return E_OK;

        case SYS_CHDIR:
        case SYS_GETCWD:
            return E_OK; 

        case SYS_OPENDIR:
            return -1; /* Simulated FS not mounted yet */

        case SYS_READDIR:
            return -1;

        case SYS_CLOSEDIR:
            return E_OK;

        case SYS_GETPID:
            return 1;

        case SYS_GET_TIME: {
            uint8_t h = get_cmos_reg(0x04);
            uint8_t m = get_cmos_reg(0x02);
            uint8_t s = get_cmos_reg(0x00);
            return (h << 16) | (m << 8) | s;
        }

        case SYS_GET_DATE: {
            uint8_t d = get_cmos_reg(0x07);
            uint8_t m = get_cmos_reg(0x08);
            uint8_t y = get_cmos_reg(0x09);
            return (d << 16) | (m << 8) | y;
        }

        case SYS_GET_TICKS:
            return (int)get_ticks();

        case SYS_READ_SECTOR:
            return disk_read_lba(arg1, arg2, (void*)arg3);

        case SYS_DEBUG:
            puts("[DEBUG] ");
            if ((const char*)arg1) puts((const char*)arg1);
            puts("\n");
            return E_OK;

        case SYS_SHUTDOWN:
            while (1) __asm__("hlt");
            return E_OK;
        
        default:
            return -1;
    }
}

/* Internal Syscall Wrappers */

static inline int32_t syscall0(uint32_t num) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline int32_t syscall1(uint32_t num, uint32_t arg1) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline int32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2) : "memory");
    return ret;
}

static inline int32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory" );
    return ret;
}

/* Public API Functions (Linked by commands.c) */

int sys_get_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds) {
    int32_t res = syscall0(SYS_GET_TIME);
    *hours = (res >> 16) & 0xFF;
    *minutes = (res >> 8) & 0xFF;
    *seconds = res & 0xFF;
    return E_OK;
}

int sys_get_date(uint8_t* day, uint8_t* month, uint16_t* year) {
    int32_t res = syscall0(SYS_GET_DATE);
    *day = (res >> 16) & 0xFF;
    *month = (res >> 8) & 0xFF;
    *year = 2000 + (res & 0xFF);
    return E_OK;
}

int sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

int sys_read_sector(uint32_t lba, uint32_t count, void* buffer) {
    return syscall3(SYS_READ_SECTOR, lba, count, (uint32_t)buffer);
}

int sys_print(const char* str) {
    return syscall1(SYS_PRINT_STRING, (uint32_t)str);
}

int sys_putc(char c) {
    return syscall1(SYS_PRINT_CHAR, (uint32_t)c);
}

int sys_clear_screen(void) {
    return syscall0(SYS_CLEAR_SCREEN);
}

int sys_set_color(uint8_t color) {
    return syscall1(SYS_SET_COLOR, color);
}

int sys_chdir(const char* path) {
    return syscall1(SYS_CHDIR, (uint32_t)path);
}

int sys_getcwd(char* buffer, uint32_t size) {
    return syscall2(SYS_GETCWD, (uint32_t)buffer, size);
}

int sys_opendir(const char* path) {
    return syscall1(SYS_OPENDIR, (uint32_t)path);
}

int sys_readdir(int fd, void* entry) {
    return syscall2(SYS_READDIR, (uint32_t)fd, (uint32_t)entry);
}

int sys_closedir(int fd) {
    return syscall1(SYS_CLOSEDIR, (uint32_t)fd);
}

int sys_sysinfo(void* info) {
    return syscall1(SYS_SYSINFO, (uint32_t)info);
}

int sys_uname(char* buffer, uint32_t size) {
    return syscall2(SYS_UNAME, (uint32_t)buffer, size);
}

int sys_debug(const char* message) {
    if (!message) return -E_INVAL;
    return syscall1(SYS_DEBUG, (uint32_t)message);
}

const char* sys_strerror(int error) {
    switch (error) {
        case E_OK:      return "Success";
        case E_INVAL:   return "Invalid parameter";
        case E_NOENT:   return "No such file or directory";
        case E_ACCESS:  return "Access denied";
        case E_NOMEM:   return "Out of memory";
        case E_NOSPC:   return "No space left on device";
        case E_EXIST:   return "File exists";
        case E_NOTDIR:  return "Not a directory";
        case E_ISDIR:   return "Is a directory";
        case E_BADF:    return "Bad file descriptor";
        case E_IO:      return "I/O error";
        case E_BUSY:    return "Device busy";
        case E_AGAIN:   return "Try again";
        case E_NOTSUPP: return "Operation not supported";
        case E_PERM:    return "Operation not permitted";
        default:        return "Unknown error";
    }
}

int sys_errno(int result) {
    return (result < 0) ? -result : E_OK;
}

void sys_shutdown(void) {
    syscall0(SYS_SHUTDOWN);
}

int sys_beep(uint32_t frequency, uint32_t duration) {
    return syscall2(SYS_BEEP, frequency, duration);
}

void sys_perror(const char* prefix) {
    if (prefix) {
        puts(prefix);
        puts(": ");
    }
}