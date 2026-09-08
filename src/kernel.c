#include "kernel.h"

uint32_t terminal_row;
uint32_t terminal_column;
uint8_t  terminal_color;
uint16_t* terminal_buffer;

void terminal_initialize(void) 
{
    terminal_row = 0;
    terminal_column = 0;

    terminal_color = VGA_COLOR_LIGHT_GREEN | (VGA_COLOR_BLACK << 4);
    terminal_buffer = (uint16_t*) VGA_ADDRESS;

    memset(terminal_buffer, 0, VGA_WIDTH * VGA_HEIGHT * sizeof(uint16_t));
}

void terminal_scroll(void)
{
    uint16_t clear_char = ' ' | (terminal_color << 8);

    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[y * VGA_WIDTH + x] = terminal_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    for (int x = 0; x < VGA_WIDTH; x++) {
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = clear_char;
    }
}

void terminal_putchar(char c) 
{
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
        return;
    }

    const int index = terminal_row * VGA_WIDTH + terminal_column;
    terminal_buffer[index] = c | (terminal_color << 8);

    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
    }
}

void terminal_write(const char* data, uint32_t size) 
{
    for (uint32_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

void terminal_writestring(const char* data) 
{
    uint32_t len = strlen(data);
    terminal_write(data, len);
}

/* --- KERNEL ENTRY POINT --- */
void kernel_main(void) 
{
    terminal_initialize();

    terminal_writestring("Welcome to KFS-1 from scratch!\n");
    terminal_writestring("System initialized successfully...\n");
    terminal_writestring("-----------------------------------\n");
    terminal_writestring("Booting core context...\n");
    terminal_writestring("Done.\n\n");
    
    terminal_writestring("42\n");

    while (1) {
        // Halt state safely
    }
}