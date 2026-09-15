#include "kernel.h"

uint32_t strlen(const char* str) 
{
    uint32_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

void *memset(void *bufptr, int value, uint32_t num) 
{
    unsigned char *buf = (unsigned char *)bufptr;
    for (uint32_t i = 0; i < num; i++) {
        buf[i] = (unsigned char)value;
    }
    return bufptr;
}

void printk(const char *str)
{
    if (str != 0) {
        terminal_writestring(str);
    }
}

extern char stack_bottom[];
extern char stack_top[];

static void printk_hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[11];

    buffer[0] = '0';
    buffer[1] = 'x';
    for (uint32_t i = 0; i < 8; i++) {
        buffer[2 + i] = digits[(value >> (28 - i * 4)) & 0xf];
    }
    buffer[10] = '\0';
    printk(buffer);
}

void print_stack(void)
{
    uint32_t *frame;
    uint32_t frame_address;
    uint32_t stack_start = (uint32_t)stack_bottom;
    uint32_t stack_end = (uint32_t)stack_top;
    uint32_t frame_number = 0;

    __asm__ volatile ("movl %%ebp, %0" : "=r"(frame));
    printk("Kernel stack trace:\n");

    while (frame_number < 32) {
        frame_address = (uint32_t)frame;
        if (frame_address < stack_start || frame_address + 8 > stack_end) {
            printk("  invalid frame at ");
            printk_hex(frame_address);
            printk("\n");
            return;
        }

        printk("  #");
        printk_hex(frame_number);
        printk(" frame=");
        printk_hex(frame_address);
        printk(" return=");
        printk_hex(frame[1]);
        printk("\n");

        if (frame[0] <= frame_address) {
            return;
        }
        frame = (uint32_t *)frame[0];
        frame_number++;
    }

    printk("  ... stack trace truncated\n");
}