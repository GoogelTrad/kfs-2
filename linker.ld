ENTRY(_start)

SECTIONS
{
    /* GRUB loads our kernel at the 1 Megabyte mark */
    . = 1M;

    /* Combine Multiboot header and code instructions */
    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.text)
    }

    /* Read-only variables (like literal strings) */
    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    /* Initialized data */
    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    /* Uninitialized data (like our stack) */
    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }
}