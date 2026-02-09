/*
 * GUI Applications for RO-DOS
 * Provides graphical applications using VirtIO-GPU (800x600) or VGA Mode 13h
 * Includes PS/2 Mouse Support
*/

#include <stdint.h>
#include <stdbool.h>

/* GPU Driver Interface - Supports both VirtIO-GPU (800x600) and VGA (320x200) */

/* External GPU driver functions */
#include "drivers/mouse.h"

extern uint32_t *gpu_setup_framebuffer(void);
extern int gpu_flush(void);
extern void gpu_clear(uint32_t color);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern int gpu_disable_scanout(void);
extern int gpu_get_width(void);
extern int gpu_get_height(void);
extern void set_mode_13h(void);
extern void vga_draw_pixel(int x, int y, uint32_t c);
extern void vga_clear(uint32_t c);
extern void vga_fill_rect(int x, int y, int w, int h, uint32_t c);
extern void vga_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg);
extern void vga_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern void vga_set_target(uint8_t *ptr);
extern void sys_reboot(void);

static uint8_t *gui_buffer = NULL;
static bool use_vga_fallback = false;

/* Wrappers */
/* Color mapping helper for VGA fallback */
static uint8_t rgb_to_vga(uint32_t c) {
    /* Map RGB values to standard VGA palette indices */
    switch(c) {
        case 0x000000: return 0;  // Black
        case 0x0000AA: return 1;  // Blue
        case 0x00AA00: return 2;  // Green
        case 0x00AAAA: return 3;  // Cyan
        case 0xAA0000: return 4;  // Red
        case 0xAA00AA: return 5;  // Magenta
        case 0xAA5500: return 6;  // Brown
        case 0xAAAAAA: return 7;  // Gray
        case 0x555555: return 8;  // Dark Gray
        case 0x5555FF: return 9;  // LBlue
        case 0x55FF55: return 10; // LGreen
        case 0x55FFFF: return 11; // LCyan
        case 0xFF5555: return 12; // LRed
        case 0xFF55FF: return 13; // LMagenta
        case 0xFFFF55: return 14; // Yellow
        case 0xFFFFFF: return 15; // White
        default: return 15;       // Default White
    }
}

/* Wrappers */
static void safe_gpu_draw_pixel(int x, int y, uint32_t c) {
    if (use_vga_fallback) vga_draw_pixel(x, y, rgb_to_vga(c));
    else { extern void gpu_draw_pixel(int, int, uint32_t); gpu_draw_pixel(x, y, c); }
}
static void safe_gpu_clear(uint32_t c) {
    if (use_vga_fallback) vga_clear(rgb_to_vga(c));
    else { extern void gpu_clear(uint32_t); gpu_clear(c); }
}
static void safe_gpu_fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (use_vga_fallback) vga_fill_rect(x, y, w, h, rgb_to_vga(c));
    else { extern void gpu_fill_rect(int, int, int, int, uint32_t); gpu_fill_rect(x, y, w, h, c); }
}
static void safe_gpu_draw_char(int x, int y, uint8_t ch, uint32_t fg, uint32_t bg) {
    if (use_vga_fallback) vga_draw_char(x, y, ch, rgb_to_vga(fg), rgb_to_vga(bg));
    else { extern void gpu_draw_char(int, int, uint8_t, uint32_t, uint32_t); gpu_draw_char(x, y, ch, fg, bg); }
}
static void safe_gpu_draw_string(int x, int y, const uint8_t *s, uint32_t fg, uint32_t bg) {
    if (use_vga_fallback) vga_draw_string(x, y, s, rgb_to_vga(fg), rgb_to_vga(bg));
    else { extern void gpu_draw_string(int, int, const uint8_t*, uint32_t, uint32_t); gpu_draw_string(x, y, s, fg, bg); }
}

/* Redefine to use wrappers */
#define gpu_draw_pixel safe_gpu_draw_pixel
#define gpu_clear safe_gpu_clear
#define gpu_fill_rect safe_gpu_fill_rect
#define gpu_draw_char safe_gpu_draw_char
#define gpu_draw_string safe_gpu_draw_string

/* External kernel functions */
extern void c_puts(const char *s);
extern void c_putc(char c);
extern void c_cls(void);
extern uint16_t c_getkey(void);
extern uint16_t c_getkey_nonblock(void);
extern void sys_reboot(void);
extern uint32_t get_ticks(void);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

#define puts c_puts
#define putc c_putc
#define cls c_cls
#define getkey c_getkey

/* Screen dimensions - will be set by GPU driver */
static int SCREEN_WIDTH = 320;
static int SCREEN_HEIGHT = 200;

/* Color definitions (work for both VGA palette and RGB) */
#define COLOR_BLACK     0x000000
#define COLOR_BLUE      0x0000AA
#define COLOR_GREEN     0x00AA00
#define COLOR_CYAN      0x00AAAA
#define COLOR_RED       0xAA0000
#define COLOR_MAGENTA   0xAA00AA
#define COLOR_BROWN     0xAA5500
#define COLOR_GRAY      0xAAAAAA
#define COLOR_DARKGRAY  0x555555
#define COLOR_LBLUE     0x5555FF
#define COLOR_LGREEN    0x55FF55
#define COLOR_LCYAN     0x55FFFF
#define COLOR_LRED      0xFF5555
#define COLOR_LMAGENTA  0xFF55FF
#define COLOR_YELLOW    0xFFFF55
#define COLOR_WHITE     0xFFFFFF
#define COLOR_LGRAY     0xC0C0C0

extern int c_kb_hit(void);

static void gpu_draw_hline(int x, int y, int w, uint32_t c) {
    gpu_fill_rect(x, y, w, 1, c);
}

static void gpu_draw_rect(int x, int y, int w, int h, uint32_t c) {
    gpu_fill_rect(x, y, w, 1, c);
    gpu_fill_rect(x, y+h-1, w, 1, c);
    gpu_fill_rect(x, y, 1, h, c);
    gpu_fill_rect(x+w-1, y, 1, h, c);
}

/* PS/2 Mouse Driver - Moved to drivers/mouse.c */

/* Macros to maintain compatibility with existing code */
#define mouse_x mouse_get_x()
#define mouse_y mouse_get_y()
#define mouse_left mouse_get_left()
#define mouse_right mouse_get_right()


/* GUI Drawing Primitives */

/* Draw horizontal line */
static void gui_draw_hline(int x, int y, int w, uint32_t color) {
    for (int i = 0; i < w; i++) {
        gpu_draw_pixel(x + i, y, color);
    }
}

/* Draw vertical line */
static void gui_draw_vline(int x, int y, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        gpu_draw_pixel(x, y + i, color);
    }
}

/* Draw a rectangle border */
static void gui_draw_rect(int x, int y, int w, int h, uint32_t color) {
    gui_draw_hline(x, y, w, color);
    gui_draw_hline(x, y + h - 1, w, color);
    gui_draw_vline(x, y, h, color);
    gui_draw_vline(x + w - 1, y, h, color);
}



/* Draw window with title bar */
static void gui_draw_window(int x, int y, int w, int h, const char *title) {
    /* Window background */
    gpu_fill_rect(x, y, w, h, COLOR_GRAY);
    
    /* Title bar */
    gpu_fill_rect(x + 2, y + 2, w - 4, 18, COLOR_BLUE);
    
    /* Title text */
    int title_len = 0;
    while (title[title_len]) title_len++;
    int title_x = x + (w - title_len * 8) / 2;
    gpu_draw_string(title_x, y + 6, (const uint8_t *)title, COLOR_WHITE, COLOR_BLUE);
    
    /* Window border */
    gui_draw_rect(x, y, w, h, COLOR_DARKGRAY);
    
    /* Close button */
    gpu_fill_rect(x + w - 18, y + 4, 14, 14, COLOR_RED);
    gpu_draw_string(x + w - 15, y + 6, (const uint8_t *)"X", COLOR_WHITE, COLOR_RED);
}

/* Draw mouse cursor */
void gui_draw_cursor(int x, int y) {
    /* Simple arrow cursor */
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j <= i && j < 8; j++) {
            if (x + j < SCREEN_WIDTH && y + i < SCREEN_HEIGHT) {
                if (j == 0 || j == i || i == 11) {
                    gpu_draw_pixel(x + j, y + i, COLOR_BLACK);
                } else {
                    gpu_draw_pixel(x + j, y + i, COLOR_WHITE);
                }
            }
        }
    }
}

/* String length helper */
static int gui_strlen(const char *s) {
    int l = 0;
    while (s[l]) l++;
    return l;
}

/* NOTEPAD - Simple Text Editor (High Resolution) */

#define NOTEPAD_MAX_LINES 35
#define NOTEPAD_MAX_COLS 90
#define NOTEPAD_BUFFER_SIZE (NOTEPAD_MAX_LINES * NOTEPAD_MAX_COLS)

static char notepad_buffer[NOTEPAD_MAX_LINES][NOTEPAD_MAX_COLS + 1];
static int notepad_cursor_row = 0;
static int notepad_cursor_col = 0;
static int notepad_total_lines = 1;

static void notepad_clear(void) {
    for (int i = 0; i < NOTEPAD_MAX_LINES; i++) {
        for (int j = 0; j <= NOTEPAD_MAX_COLS; j++) {
            notepad_buffer[i][j] = 0;
        }
    }
    notepad_cursor_row = 0;
    notepad_cursor_col = 0;
    notepad_total_lines = 1;
}

static void notepad_draw(void) {
    /* Clear screen */
    gpu_clear(COLOR_BLUE);
    
    /* Window */
    gui_draw_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, "RO-DOS NOTEPAD");
    
    /* Toolbar */
    int toolbar_h = 24;
    gpu_fill_rect(0, 16, SCREEN_WIDTH, toolbar_h, COLOR_GRAY);
    gpu_draw_hline(0, 16+toolbar_h, SCREEN_WIDTH, COLOR_WHITE);
    
    /* Save Button */
    int btn_x = 4;
    int btn_y = 19;
    int btn_w = 40;
    int btn_h = 18;
    
    gpu_fill_rect(btn_x, btn_y, btn_w, btn_h, COLOR_LGRAY);
    gpu_draw_rect(btn_x, btn_y, btn_w, btn_h, COLOR_BLACK);
    gpu_draw_string(btn_x+4, btn_y+4, (const uint8_t *)"Save", COLOR_BLACK, COLOR_LGRAY);
    
    /* Text area background */
    int text_area_x = 4;
    int text_area_y = 16 + toolbar_h + 4; /* Below toolbar */
    int text_area_w = SCREEN_WIDTH - 8;
    int text_area_h = SCREEN_HEIGHT - text_area_y - 20; /* Minus status bar */
    gpu_fill_rect(text_area_x, text_area_y, text_area_w, text_area_h, COLOR_WHITE);
    gpu_draw_rect(text_area_x-1, text_area_y-1, text_area_w+2, text_area_h+2, COLOR_BLACK);
    
    /* Calculate visible lines based on screen height */
    int visible_lines = (text_area_h - 4) / 10;
    if (visible_lines > NOTEPAD_MAX_LINES) visible_lines = NOTEPAD_MAX_LINES;
    
    /* Draw text content */
    for (int line = 0; line < visible_lines && line < notepad_total_lines; line++) {
        int y = text_area_y + 2 + line * 10;
        
        for (int col = 0; col < NOTEPAD_MAX_COLS && notepad_buffer[line][col]; col++) {
            int x = text_area_x + 2 + col * 8;
            if (x + 8 < text_area_x + text_area_w) {
                gpu_draw_char(x, y, notepad_buffer[line][col], COLOR_BLACK, COLOR_WHITE);
            }
        }
    }
    
    /* Draw cursor */
    int cursor_x = text_area_x + 2 + notepad_cursor_col * 8;
    int cursor_y = text_area_y + 2 + notepad_cursor_row * 10;
    /* Only if within bounds */
    if (notepad_cursor_row < visible_lines) {
        gpu_fill_rect(cursor_x, cursor_y, 7, 10, COLOR_BLACK);
    }
    
    /* Status bar */
    int status_y = SCREEN_HEIGHT - 16;
    gpu_fill_rect(0, status_y, SCREEN_WIDTH, 16, COLOR_LGRAY);
    
    char status[80];
    int n = 0;
    status[n++] = 'L'; status[n++] = ':';
    status[n++] = '0' + ((notepad_cursor_row + 1) / 10) % 10;
    status[n++] = '0' + (notepad_cursor_row + 1) % 10;
    status[n++] = ' '; status[n++] = 'C'; status[n++] = ':';
    status[n++] = '0' + ((notepad_cursor_col + 1) / 10) % 10;
    status[n++] = '0' + (notepad_cursor_col + 1) % 10;
    status[n++] = ' '; status[n++] = '|'; status[n++] = ' ';
    status[n++] = 'E'; status[n++] = 'S'; status[n++] = 'C'; status[n++] = '='; status[n++] = 'Q';
    status[n] = 0;
    gpu_draw_string(4, status_y + 4, (const uint8_t *)status, COLOR_BLACK, COLOR_LGRAY);
    
    gpu_flush();
}

static void notepad_insert_char(char c) {
    if (notepad_cursor_col < NOTEPAD_MAX_COLS) {
        notepad_buffer[notepad_cursor_row][notepad_cursor_col] = c;
        notepad_cursor_col++;
    }
}

static void notepad_new_line(void) {
    if (notepad_cursor_row < NOTEPAD_MAX_LINES - 1) {
        notepad_cursor_row++;
        notepad_cursor_col = 0;
        if (notepad_cursor_row >= notepad_total_lines) {
            notepad_total_lines = notepad_cursor_row + 1;
        }
    }
}

static void notepad_backspace(void) {
    if (notepad_cursor_col > 0) {
        notepad_cursor_col--;
        /* Shift remaining chars left */
        for (int i = notepad_cursor_col; i < NOTEPAD_MAX_COLS - 1; i++) {
            notepad_buffer[notepad_cursor_row][i] = notepad_buffer[notepad_cursor_row][i + 1];
        }
        notepad_buffer[notepad_cursor_row][NOTEPAD_MAX_COLS - 1] = 0;
    } else if (notepad_cursor_row > 0) {
        /* Move to end of previous line */
        notepad_cursor_row--;
        notepad_cursor_col = gui_strlen(notepad_buffer[notepad_cursor_row]);
    }
}

int gui_notepad(const char *args) {
    
    /* Setup graphics - use VGA 320x200 like GUITEST */
    gpu_setup_framebuffer();
    SCREEN_WIDTH = 320;
    SCREEN_HEIGHT = 200;
    use_vga_fallback = true;
    
    mouse_init();
    mouse_set_bounds(SCREEN_WIDTH, SCREEN_HEIGHT);
    
    /* Parse arguments for filename */
    char filename[32] = "Untitled";
    if (args && args[0]) {
        int i = 0;
        /* skip spaces */
        while(args[i] == ' ') i++;
        int j = 0;
        while(args[i] && args[i] != ' ' && j < 31) {
            filename[j++] = args[i++];
        }
        filename[j] = 0;
    }

    notepad_clear();
    
    /* If we had a real filesystem, we would load file content here */
    // if (strcmp(filename, "Untitled") != 0) load_file(filename);
    
    /* Construct window title */
    char title[64];
    int t = 0;
    const char *prefix = "RO-DOS NOTEPAD - ";
    while(prefix[t]) { title[t] = prefix[t]; t++; }
    int n = 0;
    while(filename[n] && t < 63) { title[t++] = filename[n++]; }
    title[t] = 0;
    
    notepad_draw();  /* Initial draw will use default title, but we can override in loop */
    
    /* Initial draw with correct title */
    gpu_clear(COLOR_BLUE);
    gui_draw_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, title);
    // ... rest of initial draw components ...
    /* Since we are duplicating draw logic, just set a global title variable or redraw */
    /* For simplicity, let's just redraw fully in the loop or make draw take a title */
    
    /* Use a modified draw function or just redraw the window header locally */
    
    /* Cursor backing store */
    uint8_t save_bg[16*16]; 
    int saved_mx = -1, saved_my = -1;
    
    while (1) {
        /* Poll Mouse */
        mouse_poll();
        
        /* Restore background from under cursor */
        if (saved_mx != -1 && use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      gui_buffer[py*SCREEN_WIDTH + px] = save_bg[dy*12+dx];
                  }
               }
             }
        }
        
        bool key_pressed = false;
        if (c_getkey_nonblock()) {
            uint16_t key = c_getkey();
            key_pressed = true;
            uint8_t ascii = key & 0xFF;
            uint8_t scan = (key >> 8) & 0xFF;
            
            /* ESC - Reboot */
            if (ascii == 27 || scan == 0x01) {
                sys_reboot();
            }
            
            /* Handle input ... */
            if (scan == 0x48) { /* Up */
                if (notepad_cursor_row > 0) notepad_cursor_row--;
                if (notepad_cursor_col > gui_strlen(notepad_buffer[notepad_cursor_row])) {
                    notepad_cursor_col = gui_strlen(notepad_buffer[notepad_cursor_row]);
                }
            } else if (scan == 0x50) { /* Down */
                if (notepad_cursor_row < notepad_total_lines - 1) notepad_cursor_row++;
                if (notepad_cursor_col > gui_strlen(notepad_buffer[notepad_cursor_row])) {
                    notepad_cursor_col = gui_strlen(notepad_buffer[notepad_cursor_row]);
                }
            } else if (scan == 0x4B) { /* Left */
                if (notepad_cursor_col > 0) notepad_cursor_col--;
            } else if (scan == 0x4D) { /* Right */
                if (notepad_cursor_col < gui_strlen(notepad_buffer[notepad_cursor_row])) {
                    notepad_cursor_col++;
                }
            }
            /* Backspace */
            else if (ascii == 8 || scan == 0x0E) {
                notepad_backspace();
            }
            /* Enter */
            else if (ascii == 13 || ascii == 10) {
                notepad_new_line();
            }
            /* F2 - Save (Mockup) */
            else if (scan == 0x3C) {
                 /* Show save status with filename */
                 char msg[64] = "SAVED: ";
                 int m = 7;
                 int f = 0;
                 while(filename[f] && m < 63) msg[m++] = filename[f++];
                 msg[m] = 0;
                 
                 gui_draw_window(10, 10, SCREEN_WIDTH-20, SCREEN_HEIGHT-20, msg);
                 for(volatile int d=0; d<20000000; d++); /* Delay to show message */
            }
            /* Printable characters */
            else if (ascii >= 32 && ascii < 127) {
                notepad_insert_char(ascii);
            }
        }
        
        /* Check Mouse Clicks */
        if (mouse_left) {
             /* Close button (top right) */
             if (mouse_x >= SCREEN_WIDTH - 20 && mouse_y < 20) sys_reboot();
             
             /* Save Button (4, 19, 40, 18) */
             if (mouse_x >= 4 && mouse_x < 44 && mouse_y >= 19 && mouse_y < 37) {
                 /* Show confirmation with filename */
                 char msg[64] = "FILE SAVED: ";
                 int m = 12;
                 int f = 0;
                 while(filename[f] && m < 63) msg[m++] = filename[f++];
                 msg[m] = 0;

                 gui_draw_window(10, 10, SCREEN_WIDTH-20, SCREEN_HEIGHT-20, msg);
                 for(volatile int d=0; d<20000000; d++);
                 key_pressed = true; /* Trigger redraw */
                 
                 /* Wait release */
                 while(mouse_left) mouse_poll();
             }
        }

        /* Redraw text only if key pressed */
        if (key_pressed) {
            notepad_draw();
            /* Redraw title over default */
            gui_draw_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, title);
             /* Toolbar */
            int toolbar_h = 24;
            gpu_fill_rect(0, 16, SCREEN_WIDTH, toolbar_h, COLOR_GRAY);
            gpu_draw_hline(0, 16+toolbar_h, SCREEN_WIDTH, COLOR_WHITE);
    
            /* Save Button */
            int btn_x = 4;
            int btn_y = 19;
            int btn_w = 40;
            int btn_h = 18;
            gpu_fill_rect(btn_x, btn_y, btn_w, btn_h, COLOR_LGRAY);
            gpu_draw_rect(btn_x, btn_y, btn_w, btn_h, COLOR_BLACK);
            gpu_draw_string(btn_x+4, btn_y+4, (const uint8_t *)"Save", COLOR_BLACK, COLOR_LGRAY);

            /* After text redraw, we must reset saved_mx/my because screen changed */
            saved_mx = -1; 
        } else {
            /* Save background under new cursor position */
            saved_mx = mouse_x; saved_my = mouse_y;
            if (use_vga_fallback && gui_buffer) {
                 for (int dy=0; dy<16; dy++) {
                   for (int dx=0; dx<12; dx++) {
                      int px = saved_mx + dx;
                      int py = saved_my + dy;
                      if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                          save_bg[dy*12+dx] = gui_buffer[py*SCREEN_WIDTH + px];
                      } else {
                          save_bg[dy*12+dx] = 0;
                      }
                   }
                 }
            }
            
            /* Draw Mouse Cursor */
            gui_draw_cursor(mouse_x, mouse_y);
            gpu_flush();
            
            /* Delay */
            for(volatile int d=0; d<10000; d++);
        }
    }
    
    return 0;
}

/* ===========================================================================
 * PAINT - Drawing Application with Mouse Support
 * =========================================================================== */

static int paint_brush_size = 3;
static uint32_t paint_color = COLOR_WHITE;

/* Color palette for paint */
static uint32_t paint_palette[] = {
    COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN,
    COLOR_RED, COLOR_MAGENTA, COLOR_BROWN, COLOR_GRAY,
    COLOR_DARKGRAY, COLOR_LBLUE, COLOR_LGREEN, COLOR_LCYAN,
    COLOR_LRED, COLOR_LMAGENTA, COLOR_YELLOW, COLOR_WHITE
};

static void paint_draw_palette(void) {
    int palette_y = SCREEN_HEIGHT - 24;
    int palette_x = 20;
    
    for (int i = 0; i < 16; i++) {
        int x = palette_x + i * 24;
        gpu_fill_rect(x, palette_y, 20, 16, paint_palette[i]);
        if (paint_palette[i] == paint_color) {
            gui_draw_rect(x - 1, palette_y - 1, 22, 18, COLOR_WHITE);
        }
    }
    
    /* Brush size indicator */
    gpu_draw_string(SCREEN_WIDTH - 80, palette_y, (const uint8_t *)"Size:", COLOR_WHITE, COLOR_BLACK);
    gpu_draw_char(SCREEN_WIDTH - 32, palette_y, '0' + paint_brush_size, COLOR_YELLOW, COLOR_BLACK);
}

static void paint_draw_brush(int x, int y) {
    for (int dy = -paint_brush_size; dy <= paint_brush_size; dy++) {
        for (int dx = -paint_brush_size; dx <= paint_brush_size; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 24 && py < SCREEN_HEIGHT - 28) {
                gpu_draw_pixel(px, py, paint_color);
            }
        }
    }
}

int gui_paint(const char *args) {
    (void)args;
    
    /* Setup graphics - use VGA 320x200 like GUITEST */
    gpu_setup_framebuffer();
    SCREEN_WIDTH = 320;
    SCREEN_HEIGHT = 200;
    use_vga_fallback = true;
    
    mouse_init();
    mouse_set_bounds(SCREEN_WIDTH, SCREEN_HEIGHT);

    
    /* Clear canvas */
    gpu_clear(COLOR_BLACK);
    
    /* Title bar */
    gpu_fill_rect(0, 0, SCREEN_WIDTH, 22, COLOR_BLUE);
    gpu_draw_string(8, 6, (const uint8_t *)"PAINT - Arrow keys to move, Space to draw", COLOR_WHITE, COLOR_BLUE);
    
    /* Draw palette */
    paint_draw_palette();
    
    /* Main loop with polling */
    int last_mx = -1, last_my = -1;
    uint8_t save_bg[16*16]; 
    int saved_mx = -1, saved_my = -1;
    
    while (1) {
        bool changed = false;
        
        /* 0. Restore background from under cursor */
        if (saved_mx != -1 && use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      gui_buffer[py*SCREEN_WIDTH + px] = save_bg[dy*12+dx];
                  }
               }
             }
        }
        
        /* 1. Poll Mouse */
        mouse_poll();
        if (mouse_x != last_mx || mouse_y != last_my || mouse_left || mouse_right) {
            changed = true;
            last_mx = mouse_x;
            last_my = mouse_y;
        }
        
        /* 2. Poll Keyboard */
        if (c_getkey_nonblock()) {
            uint16_t key = c_getkey();
            uint8_t ascii = key & 0xFF;
            uint8_t scan = (key >> 8) & 0xFF;
            
            /* ESC - Reboot */
            if (ascii == 27 || scan == 0x01) {
                sys_reboot();
            }
            
            /* Arrow keys move cursor (optional, mouse is primary) */
            /* ... omitted for brevity to focus on mouse ... */
            
            /* Space - draw */
            if (ascii == ' ') {
                paint_draw_brush(mouse_x, mouse_y);
                changed = true;
            }
            
            /* +/- change brush size */
            if (ascii == '+' || scan == 0x4E) {
                if (paint_brush_size < 10) paint_brush_size++;
                paint_draw_palette();
                changed = true;
            }
            if (ascii == '-' || scan == 0x4A) {
                if (paint_brush_size > 1) paint_brush_size--;
                paint_draw_palette();
                changed = true;
            }
            
            /* Number keys 1-8 select colors */
            if (ascii >= '1' && ascii <= '8') {
                paint_color = paint_palette[ascii - '1'];
                paint_draw_palette();
                changed = true;
            }
        }
        
        /* 3. Handle Mouse Drawing */
        if (mouse_left) {
            /* Check palette */
            if (mouse_y >= SCREEN_HEIGHT - 24) {
                int palette_x = 20;
                for (int i = 0; i < 16; i++) {
                    int x = palette_x + i * 24;
                    if (mouse_x >= x && mouse_x < x + 20) {
                        paint_color = paint_palette[i];
                        paint_draw_palette();
                        changed = true; /* Redraw palette */
                        break;
                    }
                }
            } else if (mouse_y >= 24) {
                /* Draw on canvas */
                paint_draw_brush(mouse_x, mouse_y);
                changed = true;
            }
        }
        
        /* 4. Update Screen if needed */
        if (changed) {
             /* Logic handled above */
        }
        
        /* 5. Save background under new cursor position */
        saved_mx = mouse_x; saved_my = mouse_y;
        if (use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      save_bg[dy*12+dx] = gui_buffer[py*SCREEN_WIDTH + px];
                  } else {
                      save_bg[dy*12+dx] = 0;
                  }
               }
             }
        }
        
        /* 6. Draw Cursor */
        gui_draw_cursor(mouse_x, mouse_y);
        
        /* 7. Flush */
        gpu_flush();
        
        /* Small delay */
        for(volatile int d=0; d<10000; d++);
    }
    
    return 0;
}

/* ===========================================================================
 * SYSINFO - System Information Viewer
 * =========================================================================== */

int gui_sysinfo(const char *args) {
    (void)args;
    
    /* Setup graphics - use VGA 320x200 like GUITEST */
    gpu_setup_framebuffer();
    SCREEN_WIDTH = 320;
    SCREEN_HEIGHT = 200;
    use_vga_fallback = true;
    
    mouse_init();
    mouse_set_bounds(SCREEN_WIDTH, SCREEN_HEIGHT);

    
    /* Cursor backing store */
    uint8_t save_bg[16*16]; 
    int saved_mx = -1, saved_my = -1;
    
    /* Calculate window dimensions */
    int win_w = SCREEN_WIDTH > 400 ? 500 : SCREEN_WIDTH - 40;
    int win_h = SCREEN_HEIGHT > 300 ? 350 : SCREEN_HEIGHT - 40;
    int win_x = (SCREEN_WIDTH - win_w) / 2;
    int win_y = (SCREEN_HEIGHT - win_h) / 2;
    
    /* Initial Draw */
    /* Clear screen */
    gpu_clear(COLOR_BLUE);
    
    /* Draw window */
    gui_draw_window(win_x, win_y, win_w, win_h, "SYSTEM INFORMATION");
    
    int y = win_y + 32;
    int x = win_x + 20;
    int line_h = 14;
    
    /* System information */
    gpu_draw_string(x, y, (const uint8_t *)"RO-DOS Version 1.1", COLOR_YELLOW, COLOR_GRAY);
    y += line_h;
    
    gpu_draw_string(x, y, (const uint8_t *)"32-bit Protected Mode Operating System", COLOR_WHITE, COLOR_GRAY);
    y += line_h + 8;
    
    gpu_draw_string(x, y, (const uint8_t *)"HARDWARE:", COLOR_LCYAN, COLOR_GRAY);
    y += line_h;
    
    gpu_draw_string(x + 10, y, (const uint8_t *)"CPU: x86 (i386 compatible)", COLOR_WHITE, COLOR_GRAY);
    y += line_h;
    
    gpu_draw_string(x + 10, y, (const uint8_t *)"Graphics: VirtIO-GPU / VGA", COLOR_WHITE, COLOR_GRAY);
    y += line_h;

    gpu_draw_string(x + 10, y + line_h*5, (const uint8_t *)"Input: PS/2 Keyboard + Mouse", COLOR_WHITE, COLOR_GRAY);
    
    gpu_draw_string(x, y + line_h * 8, (const uint8_t *)"Press ESC or click X to reboot", COLOR_YELLOW, COLOR_GRAY);
    
    /* Polling Loop */
    while (1) {
        /* Poll inputs */
        mouse_poll();
        
        /* Restore background from under cursor */
        if (saved_mx != -1 && use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      gui_buffer[py*SCREEN_WIDTH + px] = save_bg[dy*12+dx];
                  }
               }
             }
        }
        
        /* Check Keyboard */
        if (c_getkey_nonblock()) {
            uint16_t key = c_getkey();
            uint8_t scan = (key >> 8) & 0xFF;
            if (scan == 0x01) sys_reboot();
        }
        
        /* Check Mouse Click */
        if (mouse_left) {
            int close_x = win_x + win_w - 18;
            int close_y = win_y + 4;
            if (mouse_x >= close_x && mouse_x < close_x + 14 &&
                mouse_y >= close_y && mouse_y < close_y + 14) {
                sys_reboot();
            }
        }
        
        /* Save background under new cursor position */
        saved_mx = mouse_x; saved_my = mouse_y;
        if (use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      save_bg[dy*12+dx] = gui_buffer[py*SCREEN_WIDTH + px];
                  } else {
                      save_bg[dy*12+dx] = 0;
                  }
               }
             }
        }
        
        /* Draw Cursor */
        gui_draw_cursor(mouse_x, mouse_y);
        gpu_flush();
        
        /* Delay */
        for(volatile int d=0; d<10000; d++);
    }
    
    return 0;
}

/* ===========================================================================
 * FILE BROWSER - Simple File Browser GUI
 * =========================================================================== */

/* Filesystem structures from commands.c */
typedef struct {
    char name[56];
    uint32_t size;
    uint8_t type;
    uint8_t attr;
    uint16_t parent_idx;
    uint16_t reserved;
} FSEntry;

extern FSEntry fs_table[];
extern int fs_count;

static int filebrowser_selected = 0;
static int filebrowser_scroll = 0;

int gui_filebrowser(const char *args) {
    (void)args;
    
    /* Setup graphics - use VGA 320x200 like GUITEST */
    gpu_setup_framebuffer();
    SCREEN_WIDTH = 320;
    SCREEN_HEIGHT = 200;
    use_vga_fallback = true;
    
    mouse_init();
    mouse_set_bounds(SCREEN_WIDTH, SCREEN_HEIGHT);

    
    filebrowser_selected = 0;
    filebrowser_scroll = 0;
    
    int visible_items = 8;
    bool need_redraw = true;
    
    /* Cursor backing store */
    uint8_t save_bg[16*16]; 
    int saved_mx = -1, saved_my = -1;
    
    while (1) {
        if (need_redraw) {
            /* Clear screen */
            gpu_clear(COLOR_BLUE);
            
            /* Window */
            gui_draw_window(10, 10, 300, 180, "FILE BROWSER");
            
            /* ... (drawing code same as before, simplified for diff matches) ... */
            /* File list area */
            int list_x = 16;
            int list_y = 36;
            int list_w = 288;
            int list_h = 120;
            gpu_fill_rect(list_x, list_y, list_w, list_h, COLOR_WHITE);
            
            /* Draw files */
            int y = list_y + 2;
            int visible_count = 0;
            
            for (int i = filebrowser_scroll; i < fs_count && visible_count < visible_items; i++) {
                bool is_selected = (i == filebrowser_selected);
                uint32_t bg = is_selected ? COLOR_BLUE : COLOR_WHITE;
                uint32_t fg = is_selected ? COLOR_WHITE : COLOR_BLACK;
                
                if (is_selected) {
                    gpu_fill_rect(list_x + 1, y, list_w - 2, 14, COLOR_BLUE);
                }
                
                /* Icon and Name drawing ... */
                if (fs_table[i].type == 1) 
                    gpu_draw_string(list_x + 4, y + 2, (const uint8_t *)"[DIR]", COLOR_YELLOW, bg);
                else 
                    gpu_draw_string(list_x + 4, y + 2, (const uint8_t *)"[FIL]", COLOR_LCYAN, bg);
                
                /* Filename */
                char name[24];
                int j = 0, start = 0;
                for (int k = 0; fs_table[i].name[k] && k < 55; k++) {
                    if (fs_table[i].name[k] == '\\') start = k + 1;
                }
                for (int k = start; fs_table[i].name[k] && j < 23; k++) {
                    name[j++] = fs_table[i].name[k];
                }
                name[j] = 0;
                gpu_draw_string(list_x + 48, y + 2, (const uint8_t *)name, fg, bg);
                
                y += 14;
                visible_count++;
            }
            
            /* Status bar */
            gpu_fill_rect(16, 160, 288, 16, COLOR_GRAY);
            gpu_draw_string(20, 163, (const uint8_t *)"Arrows=select, ESC=reboot", COLOR_BLACK, COLOR_GRAY);
            
            need_redraw = false;
            saved_mx = -1; /* Reset background save after full redraw */
        }
        
        /* Poll Mouse */
        mouse_poll();
        
        /* Restore background from under cursor */
        if (saved_mx != -1 && use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      gui_buffer[py*SCREEN_WIDTH + px] = save_bg[dy*12+dx];
                  }
               }
             }
        }
        
        /* Check Keyboard */
        if (c_getkey_nonblock()) {
            uint16_t key = c_getkey();
            uint8_t scancode = (key >> 8) & 0xFF;
            
            if (scancode == 0x01) { /* ESC */
                sys_reboot();
            }
            else if (scancode == 0x48) { /* Up */
                if (filebrowser_selected > 0) {
                    filebrowser_selected--;
                    if (filebrowser_selected < filebrowser_scroll) {
                        filebrowser_scroll = filebrowser_selected;
                    }
                    need_redraw = true;
                }
            } else if (scancode == 0x50) { /* Down */
                if (filebrowser_selected < fs_count - 1) {
                    filebrowser_selected++;
                    if (filebrowser_selected >= filebrowser_scroll + visible_items) {
                        filebrowser_scroll = filebrowser_selected - visible_items + 1;
                    }
                    need_redraw = true;
                }
            }
        }
        
        /* Check Mouse Click */
        if (mouse_left) {
             /* Check if clicking on file list */
            int list_x = 16;
            int list_y = 36;
            int list_w = 288;
            int list_h = 120;
             
            if (mouse_x >= list_x && mouse_x < list_x + list_w &&
                mouse_y >= list_y && mouse_y < list_y + list_h) {
                int clicked_item = filebrowser_scroll + (mouse_y - list_y) / 14; /* 14px item height */
                if (clicked_item < fs_count) {
                    filebrowser_selected = clicked_item;
                    need_redraw = true;
                }
            }
            
            /* Close button check (approx) */
             if (mouse_x >= 300 && mouse_x < 315 && mouse_y >= 10 && mouse_y < 25) {
                sys_reboot();
             }
            
            while(mouse_left) mouse_poll();
        }
        
        if (!need_redraw) {
            /* Save background under new cursor position */
            saved_mx = mouse_x; saved_my = mouse_y;
            if (use_vga_fallback && gui_buffer) {
                 for (int dy=0; dy<16; dy++) {
                   for (int dx=0; dx<12; dx++) {
                      int px = saved_mx + dx;
                      int py = saved_my + dy;
                      if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                          save_bg[dy*12+dx] = gui_buffer[py*SCREEN_WIDTH + px];
                      } else {
                          save_bg[dy*12+dx] = 0;
                      }
                   }
                 }
            }
            
            /* Draw Cursor */
            gui_draw_cursor(mouse_x, mouse_y);
            gpu_flush();
            
            /* Delay */
            for(volatile int d=0; d<10000; d++);
        }
    }
    
    return 0;
}

/* ===========================================================================
 * CLOCK - Analog Clock Display
 * =========================================================================== */

extern int sys_get_time(uint8_t *hours, uint8_t *minutes, uint8_t *seconds);

/* Simple fixed-point sin/cos tables (scaled by 100) */
static int sin_table[60] = {
    0, 10, 21, 31, 41, 50, 59, 67, 74, 81,
    87, 91, 95, 98, 100, 100, 100, 98, 95, 91,
    87, 81, 74, 67, 59, 50, 41, 31, 21, 10,
    0, -10, -21, -31, -41, -50, -59, -67, -74, -81,
    -87, -91, -95, -98, -100, -100, -100, -98, -95, -91,
    -87, -81, -74, -67, -59, -50, -41, -31, -21, -10
};

static int cos_table[60] = {
    100, 100, 98, 95, 91, 87, 81, 74, 67, 59,
    50, 41, 31, 21, 10, 0, -10, -21, -31, -41,
    -50, -59, -67, -74, -81, -87, -91, -95, -98, -100,
    -100, -100, -98, -95, -91, -87, -81, -74, -67, -59,
    -50, -41, -31, -21, -10, 0, 10, 21, 31, 41,
    50, 59, 67, 74, 81, 87, 91, 95, 98, 100
};

static void draw_clock_hand(int cx, int cy, int length, int minute, uint32_t color, int thickness) {
    int idx = minute % 60;
    int dx = (sin_table[idx] * length) / 100;
    int dy = -(cos_table[idx] * length) / 100;
    
    /* Draw thick line from center to endpoint */
    int steps = length;
    for (int i = 0; i <= steps; i++) {
        int x = cx + (dx * i) / steps;
        int y = cy + (dy * i) / steps;
        for (int t = -thickness/2; t <= thickness/2; t++) {
            gpu_draw_pixel(x + t, y, color);
            gpu_draw_pixel(x, y + t, color);
        }
    }
}

int gui_clock(const char *args) {
    (void)args;
    
    /* Setup graphics - use VGA 320x200 like GUITEST */
    gpu_setup_framebuffer();
    SCREEN_WIDTH = 320;
    SCREEN_HEIGHT = 200;
    use_vga_fallback = true;
    
    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;
    int radius = 60;
    
    while (1) {
        /* Non-blocking input handling - check first */
        if (c_getkey_nonblock()) {
            uint16_t key = c_getkey();
            uint8_t ascii = key & 0xFF;
            uint8_t scan = (key >> 8) & 0xFF;
            
            if (ascii == 27 || scan == 0x01) {
                sys_reboot();
            }
        }
        
        /* Get current time (BCD fixed in syscall) */
        uint8_t h, m, s;
        sys_get_time(&h, &m, &s);
        h = (h + 1) % 24; /* Timezone fix */
        
        /* Clear screen */
        gpu_clear(COLOR_BLACK);
        
        /* Draw title */
        gpu_draw_string(cx - 40, 10, (const uint8_t *)"RO-DOS CLOCK", COLOR_WHITE, COLOR_BLACK);
        
        /* Draw clock face background */
        for (int dy = -radius - 5; dy <= radius + 5; dy++) {
            for (int dx = -radius - 5; dx <= radius + 5; dx++) {
                int dist_sq = dx * dx + dy * dy;
                int r_inner = (radius - 2) * (radius - 2);
                int r_outer = (radius + 5) * (radius + 5);
                if (dist_sq <= r_outer && dist_sq >= r_inner) {
                    gpu_draw_pixel(cx + dx, cy + dy, COLOR_GRAY);
                } else if (dist_sq < r_inner) {
                    gpu_draw_pixel(cx + dx, cy + dy, COLOR_WHITE);
                }
            }
        }
        
        /* Draw hour markers */
        for (int i = 0; i < 60; i++) {
            int mark_len = (i % 5 == 0) ? 10 : 4;
            int x1 = cx + (sin_table[i] * (radius - mark_len)) / 100;
            int y1 = cy - (cos_table[i] * (radius - mark_len)) / 100;
            int x2 = cx + (sin_table[i] * (radius - 2)) / 100;
            int y2 = cy - (cos_table[i] * (radius - 2)) / 100;
            
            uint32_t mark_color = (i % 5 == 0) ? COLOR_BLACK : COLOR_GRAY;
            
            /* Draw small line for markers */
            for (int t = 0; t <= 10; t++) {
                int x = x1 + ((x2 - x1) * t) / 10;
                int y = y1 + ((y2 - y1) * t) / 10;
                gpu_draw_pixel(x, y, mark_color);
                if (i % 5 == 0) {
                    gpu_draw_pixel(x + 1, y, mark_color);
                    gpu_draw_pixel(x, y + 1, mark_color);
                }
            }
        }
        
        /* Draw hands */
        /* Hour hand */
        int hour_min = ((h % 12) * 5) + (m / 12);
        draw_clock_hand(cx, cy, radius * 50 / 100, hour_min, COLOR_BLACK, 3);
        
        /* Minute hand */
        draw_clock_hand(cx, cy, radius * 75 / 100, m, COLOR_BLUE, 2);
        
        /* Second hand */
        draw_clock_hand(cx, cy, radius * 85 / 100, s, COLOR_RED, 1);
        
        /* Center dot */
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                if (dx * dx + dy * dy <= 16) {
                    gpu_draw_pixel(cx + dx, cy + dy, COLOR_YELLOW);
                }
            }
        }
        
        /* Digital time display */
        char time_str[12];
        time_str[0] = '0' + (h / 10);
        time_str[1] = '0' + (h % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (m / 10);
        time_str[4] = '0' + (m % 10);
        time_str[5] = ':';
        time_str[6] = '0' + (s / 10);
        time_str[7] = '0' + (s % 10);
        time_str[8] = 0;
        
        gpu_draw_string(cx - 32, SCREEN_HEIGHT - 40, (const uint8_t *)time_str, COLOR_WHITE, COLOR_BLACK);
        gpu_draw_string(cx - 56, SCREEN_HEIGHT - 20, (const uint8_t *)"ESC to Reboot", COLOR_GRAY, COLOR_BLACK);
        
        /* Wait for second to change or key press - eliminates flicker */
        uint8_t current_s = s;
        while (current_s == s) {
            sys_get_time(&h, &m, &s);
            h = (h + 1) % 24; /* Timezone fix */
            
            if (c_kb_hit()) {
                uint16_t key = c_getkey();
                uint8_t scan = (key >> 8) & 0xFF;
                if ((key & 0xFF) == 27 || scan == 0x01) {
                    sys_reboot();
                }
            }
            
            /* Small delay */
            for(volatile int i=0; i<10000; i++);
        }
    }
    
    return 0;
}

/* ===========================================================================
 * CALCULATOR - Simple GUI Calculator
 * =========================================================================== */

int gui_calc(const char *args) {
    (void)args;
    
    /* Setup graphics - use VGA 320x200 like GUITEST */
    gpu_setup_framebuffer();
    SCREEN_WIDTH = 320;
    SCREEN_HEIGHT = 200;
    use_vga_fallback = true;
    
    /* Initialize mouse */
    mouse_init();
    mouse_set_bounds(SCREEN_WIDTH, SCREEN_HEIGHT);

    
    char display[32] = "0";
    long current_val = 0;
    long stored_val = 0;
    char op = 0;
    bool new_number = true;
    
    /* Button layout */
    const char *buttons[] = {
        "C", "/", "*", "-",
        "7", "8", "9", "+",
        "4", "5", "6", "=",
        "1", "2", "3", "0"
    };
    
    /* Draw everything once */
    int win_w = 200;
    int win_h = 180;
    int win_x = (SCREEN_WIDTH - win_w) / 2;
    int win_y = (SCREEN_HEIGHT - win_h) / 2;
    
    bool need_redraw = true;
    
    /* Cursor/Backbuffer logic */
    uint8_t save_bg[16*16]; 
    int saved_mx = -1, saved_my = -1;
    
    while (1) {
        if (need_redraw) {
            /* Clear screen */
            gpu_clear(COLOR_BLUE);
            
            /* Window */
            gui_draw_window(win_x, win_y, win_w, win_h, "CALCULATOR");
            
            /* Display area */
            gpu_fill_rect(win_x + 10, win_y + 30, win_w - 20, 24, COLOR_WHITE);
            
            /* Right align numeric text */
            int text_len = 0;
            while(display[text_len]) text_len++;
            int text_x = win_x + win_w - 20 - (text_len * 8);
            if (text_x < win_x + 14) text_x = win_x + 14;
            gpu_draw_string(text_x, win_y + 36, (const uint8_t *)display, COLOR_BLACK, COLOR_WHITE);
            
            /* Buttons */
            int start_y = win_y + 64;
            for (int i = 0; i < 16; i++) {
                int col = i % 4;
                int row = i / 4;
                int bx = win_x + 10 + col * 45;
                int by = start_y + row * 28;
                int bw = 40;
                int bh = 24;
                
                /* Draw stored button style (simple rect for now) */
                gpu_fill_rect(bx, by, bw, bh, COLOR_GRAY);
                /* Bevel */
                gpu_fill_rect(bx, by, bw, 2, COLOR_WHITE);
                gpu_fill_rect(bx, by, 2, bh, COLOR_WHITE);
                gpu_fill_rect(bx + bw - 2, by, 2, bh, 0); /* Dark */
                gpu_fill_rect(bx, by + bh - 2, bw, 2, 0);
                
                int label_x = bx + (bw - 8) / 2;
                int label_y = by + (bh - 8) / 2;
                gpu_draw_string(label_x, label_y, (const uint8_t *)buttons[i], COLOR_BLACK, COLOR_GRAY);
            }
            
            gpu_draw_string(win_x + 10, win_y + win_h - 12, (const uint8_t *)"ESC to Reboot", COLOR_GRAY, COLOR_WHITE);
            
            need_redraw = false;
            
            /* Reset saved bg so polling loop redraws properly */
            saved_mx = -1; 
        }
        
        /* 1. Restore background from under cursor */
        if (saved_mx != -1 && use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      gui_buffer[py*SCREEN_WIDTH + px] = save_bg[dy*12+dx];
                  }
               }
             }
        }
        
        /* 2. Poll Mouse */
        mouse_poll();
        
        /* 3. Check Mouse Clicks */
        if (mouse_left) {
            /* Close button check */
            int close_x = win_x + win_w - 18;
            int close_y = win_y + 4;
            if (mouse_x >= close_x && mouse_x < close_x + 14 &&
                mouse_y >= close_y && mouse_y < close_y + 14) {
                sys_reboot();
            }
            
            /* Check buttons */
            int start_y = win_y + 64;
            for (int i = 0; i < 16; i++) {
                int col = i % 4;
                int row = i / 4;
                int bx = win_x + 10 + col * 45;
                int by = start_y + row * 28;
                
                if (mouse_x >= bx && mouse_x < bx + 40 &&
                    mouse_y >= by && mouse_y < by + 24) {
                    
                    /* Button Clicked */
                    const char *b = buttons[i];
                    char c = b[0];
                    
                    if (c >= '0' && c <= '9') {
                        if (new_number) {
                            display[0] = c;
                            display[1] = 0;
                            new_number = false;
                        } else {
                            int len = 0;
                            while(display[len]) len++;
                            if (len < 10) {
                                display[len] = c;
                                display[len+1] = 0;
                            }
                        }
                    } else if (c == 'C') {
                        display[0] = '0';
                        display[1] = 0;
                        current_val = 0;
                        stored_val = 0;
                        op = 0;
                        new_number = true;
                    } else if (c == '=') {
                        /* Calculate */
                        long val = 0;
                        bool neg = false;
                        int k=0;
                        if(display[0]=='-') { neg=true; k=1; }
                        for(; display[k]; k++) val = val*10 + (display[k]-'0');
                        if(neg) val = -val;
                        
                        current_val = val;
                        
                        long res = 0;
                        if (op == '+') res = stored_val + current_val;
                        else if (op == '-') res = stored_val - current_val;
                        else if (op == '*') res = stored_val * current_val;
                        else if (op == '/') {
                            if (current_val != 0) res = stored_val / current_val;
                            else res = 0; /* Div by zero protection */
                        }
                        else res = current_val;
                        
                        /* Convert back to string */
                        if (res == 0) {
                            display[0] = '0'; display[1] = 0;
                        } else {
                            char buf[32];
                            int p = 0;
                            long temp = res < 0 ? -res : res;
                            
                            char num_rev[32];
                            int nr = 0;
                            while (temp > 0) {
                                num_rev[nr++] = '0' + (temp % 10);
                                temp /= 10;
                            }
                            if (res < 0) buf[p++] = '-';
                            for (int k=nr-1; k>=0; k--) buf[p++] = num_rev[k];
                            buf[p] = 0;
                            
                            for(int k=0; k<=p; k++) display[k] = buf[k];
                        }
                        
                        op = 0;
                        new_number = true;
                        
                    } else {
                        /* Operator */
                        long val = 0;
                        bool neg = false;
                        int k=0;
                        if(display[0]=='-') { neg=true; k=1; }
                        for(; display[k]; k++) val = val*10 + (display[k]-'0');
                        if(neg) val = -val;
                        
                        stored_val = val;
                        op = c;
                        new_number = true;
                    }
                    
                    need_redraw = true;
                    /* Break loop to redraw immediately */
                    i = 16; 
                }
            }
            
            /* Wait release */
            while(mouse_left) mouse_poll();
            /* If redraw needed, restart loop */
            if (need_redraw) continue;
        }
        
        /* 4. Check Keyboard (Non-blocking) */
        if (c_getkey_nonblock()) {
            uint16_t key = c_getkey();
            uint8_t scan = (key >> 8) & 0xFF;
            if ((key & 0xFF) == 27 || scan == 0x01) {
                sys_reboot();
            }
        }
        
        /* 5. Save background under new cursor position */
        saved_mx = mouse_x; saved_my = mouse_y;
        if (use_vga_fallback && gui_buffer) {
             for (int dy=0; dy<16; dy++) {
               for (int dx=0; dx<12; dx++) {
                  int px = saved_mx + dx;
                  int py = saved_my + dy;
                  if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                      save_bg[dy*12+dx] = gui_buffer[py*SCREEN_WIDTH + px];
                  } else {
                      save_bg[dy*12+dx] = 0;
                  }
               }
             }
        }
        
        /* 6. Draw Cursor */
        gui_draw_cursor(mouse_x, mouse_y);
        
        /* 7. Flush */
        gpu_flush();
    }
    
    return 0;
}

/* ===========================================================================
 * Helper functions for GPU driver (provide defaults if not available)
 * =========================================================================== */

/* Weak symbols to provide defaults if GPU driver doesn't have these */
__attribute__((weak)) int gpu_get_width(void) {
    return 320;  /* Default to VGA resolution */
}

__attribute__((weak)) int gpu_get_height(void) {
    return 200;  /* Default to VGA resolution */
}

extern int c_kb_hit(void);

__attribute__((weak)) uint16_t c_getkey_nonblock(void) {
    return c_kb_hit();
}
