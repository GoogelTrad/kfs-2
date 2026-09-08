#ifndef KERNEL_H
#define KERNEL_H

/* Fixed width basic kernel types (Mandatory Requirement) */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef int                int32_t;

/* VGA Text Mode Hardware Coordinates */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

/* VGA basic color definitions */
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15
};

void kernel_main(void);

void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_write(const char* data, uint32_t size);
void terminal_writestring(const char* data);


//helpers
uint32_t strlen(const char* str);
void *memset(void *bufptr, int value, uint32_t num);

#endif