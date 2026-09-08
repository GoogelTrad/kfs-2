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