#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void c_puts(const char *s);
extern void c_putc(char c);
extern uint16_t c_getkey(void);
extern void set_attr(uint8_t a);
extern int cmd_dispatch(const char *cmd);

#define MAX_INPUT 256

static size_t str_len(const char *s) {
    size_t l = 0;
    while(s[l]) l++;
    return l;
}

static void str_trim(char *s) {
    size_t len = str_len(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

void shell_main(void) {
    char line[MAX_INPUT];
    int pos = 0;

    set_attr(0x07);
    c_puts("\nRO-DOS v1.0\n");

    while (1) {
        set_attr(0x0E); // Yellow prompt
        c_puts("C:> ");
        set_attr(0x0F); // White input
        
        pos = 0;
        for (int i = 0; i < MAX_INPUT; i++) line[i] = 0;

        while (1) {
            uint16_t k = c_getkey();
            uint8_t key = (uint8_t)(k & 0xFF);

            if (key == 13 || key == 10) { 
                line[pos] = '\0';
                c_putc('\n');
                break;
            } 
            else if (key == 8) {
                if (pos > 0) {
                    pos--;
                    c_putc(8); c_putc(' '); c_putc(8);
                }
            } 
            else if (pos < MAX_INPUT - 1 && key >= 32 && key <= 126) {
                line[pos++] = (char)key;
                c_putc((char)key);
            }
        }

        str_trim(line);

        if (str_len(line) > 0) {
            set_attr(0x07); // Default text
            int result = cmd_dispatch(line);
            
            /* CRITICAL: Handle bad commands so we don't reboot */
            if (result < 0) {
                set_attr(0x0C); // Red error
                c_puts("Bad command or file name\n");
            }
        }
    }
}