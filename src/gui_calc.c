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
    
    char display[32] = "0";
    double current_val = 0;
    double stored_val = 0;
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
    
    while (1) {
        if (need_redraw) {
            /* Clear screen */
            gpu_clear(COLOR_BLUE);
            
            /* Window */
            gui_draw_window(win_x, win_y, win_w, win_h, "CALCULATOR");
            
            /* Display area */
            gpu_fill_rect(win_x + 10, win_y + 30, win_w - 20, 24, COLOR_WHITE);
            // Right align text (approx)
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
            
            gpu_draw_string(win_x + 10, win_y + win_h - 12, (const uint8_t *)"ESC to Reboot", COLOR_GRAY, COLOR_LGRAY);
            
            /* Mouse cursor drawn at end */
        }
        
        need_redraw = false;
        
        /* Draw mouse cursor */
        mouse_poll();
        gui_draw_cursor(mouse_x, mouse_y);
        gpu_flush();
        
        /* Check Input */
        
        /* Mouse Click */
        if (mouse_left) {
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
                        /* Parse current display */
                        /* Simple int parsing for now since no atof/sscanf */
                        int val = 0;
                        for(int k=0; display[k]; k++) val = val*10 + (display[k]-'0');
                        current_val = (double)val;
                        
                        double res = 0;
                        if (op == '+') res = stored_val + current_val;
                        else if (op == '-') res = stored_val - current_val;
                        else if (op == '*') res = stored_val * current_val;
                        else if (op == '/') res = stored_val / current_val;
                        else res = current_val;
                        
                        /* Convert back to string (int only) */
                        int res_int = (int)res;
                        if (res_int == 0) {
                            display[0] = '0'; display[1] = 0;
                        } else {
                            char buf[32];
                            int p = 0;
                            int temp = res_int < 0 ? -res_int : res_int;
                            if (res_int < 0) buf[p++] = '-';
                            
                            char num_rev[32];
                            int nr = 0;
                            while (temp > 0) {
                                num_rev[nr++] = '0' + (temp % 10);
                                temp /= 10;
                            }
                            // Reverse
                            for (int k=nr-1; k>=0; k--) buf[p++] = num_rev[k];
                            buf[p] = 0;
                            
                            // Copy to display
                            for(int k=0; k<=p; k++) display[k] = buf[k];
                        }
                        
                        op = 0;
                        new_number = true;
                        
                    } else {
                        /* Operator */
                        int val = 0;
                        for(int k=0; display[k]; k++) val = val*10 + (display[k]-'0');
                        stored_val = (double)val;
                        op = c;
                        new_number = true;
                    }
                    
                    need_redraw = true;
                }
            }
            
            /* Close button check */
            int close_x = win_x + win_w - 18;
            int close_y = win_y + 4;
            if (mouse_x >= close_x && mouse_x < close_x + 14 &&
                mouse_y >= close_y && mouse_y < close_y + 14) {
                sys_reboot();
            }
            
            while(mouse_left) mouse_poll(); /* Wait release */
        }
        
        /* Keyboard input */
        if (c_getkey_nonblock()) {
            uint16_t key = c_getkey();
            uint8_t scan = (key >> 8) & 0xFF;
            if (scan == 0x01) sys_reboot();
            
            /* Add keyboard support for numbers later */
        }
        
        /* Restore background logic skipped for simplicity - flickering may occur on mouse move over UI
           but using smaller redraw loop or just redrawing the mouse cursor area would be better.
           For now, full redraw on interaction is safer.
        */
        if (need_redraw) {
             /* Redraw entire UI if state changed */
        } else {
             /* Just erase old cursor and draw new one? 
                Actually, simpler to just wait a bit and redraw.
             */
             // Initial implementation - just let it loop, but slow it down
             for(volatile int k=0; k<50000; k++);
             
             /* Erase cursor */
             gui_draw_cursor_erase(mouse_x, mouse_y);
        }
    }
    
    return 0;
}
