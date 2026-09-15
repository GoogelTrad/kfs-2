#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Descripteur d'un segment GDT (8 octets)
struct gdt_entry {
    uint16_t limit_low;    // Bits 0-15 de la limite
    uint16_t base_low;     // Bits 0-15 de la base
    uint8_t  base_middle;  // Bits 16-23 de la base
    uint8_t  access;       // Octet d'accès (privilège, type de segment)
    uint8_t  granularity;  // Flags + bits 16-19 de la limite
    uint8_t  base_high;    // Bits 24-31 de la base
} __attribute__((packed));

// Pointeur GDT pour l'instruction lgdt (6 octets)
struct gdt_ptr {
    uint16_t limit;        // Taille totale de la table - 1
    uint32_t base;         // Adresse physique mémoire (0x00000800)
} __attribute__((packed));

// Prototype de la routine assembleur
extern void gdt_flush(uint32_t gdt_ptr_addr);

// Initialisation
void init_gdt(void);

#endif