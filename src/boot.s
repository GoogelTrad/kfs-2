# Constants for the Multiboot Standard
.set ALIGN,    1<<0             # Align loaded modules on page boundaries
.set MEMINFO,  1<<1             # Provide memory map information
.set FLAGS,    ALIGN | MEMINFO  # Combine the flags
.set MAGIC,    0x1BADB002       # The magic number GRUB looks for
.set CHECKSUM, -(MAGIC + FLAGS) # Proven checksum

# 1. Multiboot Header Section
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# 2. Kernel Stack Allocation
.section .bss
.align 16
stack_bottom:
.skip 16384 # Allocate 16 Kilobytes of stack space
stack_top:

# 3. Kernel Execution Entry Point
.section .text
.global _start
.type _start, @function
_start:
    # Initialize the Stack Pointer (%esp) register
    mov $stack_top, %esp

    # Securely jump into our C entry point
    call kernel_main

    # Fallback protection: infinite CPU halt if kernel_main ever returns
    cli
1:  hlt
    jmp 1b
