/*
 * Linker script for boot shim
 * Loaded by ABL at 0x80080000
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80080000, LENGTH = 0x00080000
}

SECTIONS
{
    . = 0x80080000;

    .text : {
        *(.text.boot)
        *(.text*)
    } > RAM

    .rodata : ALIGN(4096) {
        *(.rodata*)
    } > RAM

    .data : ALIGN(4096) {
        __data_start = .;
        *(.data*)
        __data_end = .;
    } > RAM

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    __shim_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
