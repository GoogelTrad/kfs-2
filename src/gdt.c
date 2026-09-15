#include "gdt.h"
#include "kernel.h"

// Forcer l'emplacement mémoire exactement à 0x00000800
static struct gdt_entry *gdt = (struct gdt_entry *)0x00000800;
static struct gdt_ptr gp;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    // Configuration de la base
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    // Configuration de la limite (sur 20 bits)
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;

    // Ajout des flags de granularité et d'accès
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access      = access;
}

void init_gdt(void) {
    // Pointeur GDT : 7 entrées * 8 octets - 1
    gp.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gp.base  = (uint32_t)gdt;

    // 0x00: Segment Nul (Obligatoire)
    gdt_set_gate(0, 0, 0, 0, 0);

    // 0x08: Kernel Code (Ring 0, Exec/Read)
    // Access: 0x9A (10011010b), Granularity: 0xCF (Flat model 4GB, page granularity)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // 0x10: Kernel Data (Ring 0, Read/Write)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 0x18: Kernel Stack (Ring 0, Read/Write)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 0x20: User Code (Ring 3, Exec/Read)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // 0x28: User Data (Ring 3, Read/Write)
    gdt_set_gate(5, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // 0x30: User Stack (Ring 3, Read/Write)
    gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // Charger la GDT au niveau matériel
    gdt_flush((uint32_t)&gp);
}