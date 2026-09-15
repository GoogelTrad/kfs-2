.global gdt_flush
.type gdt_flush, @function

gdt_flush:
    /* 1. Récupérer l'adresse de gdt_ptr passée en paramètre (sur la pile) */
    movl 4(%esp), %eax
    lgdt (%eax)             /* Charger la GDT dans GDTR */

    /* 2. Mettre à jour les registres de segment de données avec 0x10 (Kernel Data) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 3. Far jump pour recharger CS avec 0x08 (Kernel Code) */
    ljmp $0x08, $.reload_cs

.reload_cs:
    ret