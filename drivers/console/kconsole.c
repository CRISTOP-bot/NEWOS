#include "kconsole.h"
#include <core/core.h>
#include <drivers/vga_console.h>
#include <x86_io.h>
#include <iru_string.h>

#define VGA_COLS 80
#define VGA_ROWS 25
#define CHAR_WIDTH  8
#define CHAR_HEIGHT 10

static int cursor_x = 0;
static int cursor_y = 0;
static bool kconsole_active = false;
static uint32_t text_color = 0xFFFFFFFF; // White

void kconsole_init(void) {
    cursor_x = 10;
    cursor_y = 10;
    kconsole_active = false;
    
    // Initialize VGA console if not already done
    extern struct console_device g_vga_console;
    if (g_vga_console.write == NULL) {
        vga_console_init();
    }
    
    // Clear screen using VGA write escape sequence (clear screen)
    // Use individual newline+scroll approach instead of complex escape
    kconsole_write("\n\n\n\n\n\n\n\n\n\n"); // 10 newlines to clear
    kconsole_active = true;
}

void kconsole_set_active(bool active) {
    kconsole_active = active;
}

void kconsole_set_color(uint32_t color) {
    text_color = color;
}

static void kconsole_scroll(void) {
    // Scroll the VGA console by writing a scroll escape
    // Simple approach: shift all rows up
    extern struct console_device g_vga_console;
    int i;
    u16 *vid_mem = (u16 *)0xB8000;
    
    // Shift all rows up by one
    for (i = 1; i < VGA_ROWS; i++) {
        memcpy(&vid_mem[i * VGA_COLS], &vid_mem[(i-1) * VGA_COLS], VGA_COLS * 2);
    }
    // Clear bottom row
    for (i = 0; i < VGA_COLS; i++) {
        vid_mem[(VGA_ROWS - 1) * VGA_COLS + i] = (u16)(0x07 << 8); // light gray on black
    }
    cursor_y -= CHAR_HEIGHT;
}

static void kconsole_putc_nolock(char c) {
    if (!kconsole_active) return;

    if (c == '\n') {
        cursor_x = 10;
        cursor_y += CHAR_HEIGHT;
        if (cursor_y >= VGA_ROWS * CHAR_HEIGHT) {
            kconsole_scroll();
        }
        return;
    }

    if (c == '\r') {
        cursor_x = 10;
        return;
    }

    if (c == '\t') {
        cursor_x += CHAR_WIDTH * 4;
        return;
    }

    // Write character directly to VGA memory
    extern struct console_device g_vga_console;
    u8 attr = (u8)((text_color >> 4) & 0x0F) | (u8)((text_color & 0x0F) << 4);
    
    // VGA memory at 0xB8000, 2 bytes per cell: attribute in high byte, ASCII in low byte
    if (g_vga_console.write) {
        // Use the console device write function if available
        // But it expects char, so we just write the char
        char ch = c;
        g_vga_console.write(&g_vga_console, ch);
    } else {
        // Direct VGA write as fallback
        u16 *screen = (u16 *)0xB8000;
        size_t pos = (cursor_y / CHAR_HEIGHT) * VGA_COLS + (cursor_x / CHAR_WIDTH);
        if (pos < VGA_COLS * VGA_ROWS) {
            screen[pos] = (u16)((attr << 8) | (u8)c);
        }
    }

    cursor_x += CHAR_WIDTH;
    if (cursor_x + CHAR_WIDTH >= VGA_COLS * CHAR_WIDTH) {
        cursor_x = 10;
        cursor_y += CHAR_HEIGHT;
        if (cursor_y >= VGA_ROWS * CHAR_HEIGHT) {
            kconsole_scroll();
        }
    }
}

void kconsole_putc(char c) {
    kconsole_putc_nolock(c);
}

void kconsole_write(const char *s) {
    if (!s) return;
    
    while (*s) {
        kconsole_putc(*s++);
    }
}