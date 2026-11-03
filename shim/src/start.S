/*
 * Boot shim for Xiaomi Mi 5s Plus (natrium)
 * Entry point from Android bootloader
 *
 * This code runs in EL1 (kernel mode) and prepares the hardware
 * for XNU kernel boot.
 */

.section ".text.boot"
.global _start

_start:
    /* We enter here from fastboot boot */
    /* x0 = dtb pointer (from ABL) */
    /* x1 = machine type (unused) */
    /* x2 = 0 */
    /* x3 = 0 */

    /* Save ABL's DTB pointer */
    mov     x20, x0

    /* Disable all interrupts */
    msr     daifset, #0xf

    /* Set up initial stack */
    ldr     x0, =__stack_top
    mov     sp, x0

    /* Clear BSS section */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
    sub     x1, x1, x0
    bl      memzero

    /* Initialize platform */
    mov     x0, x20              /* Pass ABL DTB */
    bl      platform_early_init

    /* Load XNU kernel payload */
    bl      load_payload

    /* x0 now contains entry point */
    /* x1 contains ADT pointer */
    mov     x21, x0              /* Save entry */
    mov     x22, x1              /* Save ADT */

    /* Flush caches */
    bl      flush_caches

    /* Disable MMU (XNU sets up its own) */
    bl      disable_mmu

    /* Prepare boot protocol */
    mov     x0, x22              /* ADT pointer */
    mov     x1, xzr              /* Reserved */
    mov     x2, xzr
    mov     x3, xzr

    /* Jump to XNU! */
    br      x21

    /* Should never return */
    b       .

/*
 * memzero - Zero memory region
 * x0 = start address
 * x1 = length
 */
memzero:
    cbz     x1, 2f
1:  strb    wzr, [x0], #1
    subs    x1, x1, #1
    b.ne    1b
2:  ret

/*
 * flush_caches - Flush all caches to PoC
 */
flush_caches:
    /* Data cache clean & invalidate */
    mov     x0, #0
1:  dc      cisw, x0
    add     x0, x0, #1
    cmp     x0, #0x1000
    b.ne    1b

    /* Instruction cache invalidate */
    ic      iallu
    dsb     sy
    isb
    ret

/*
 * disable_mmu - Disable MMU and caches
 */
disable_mmu:
    mrs     x0, sctlr_el1
    bic     x0, x0, #1           /* Disable MMU */
    bic     x0, x0, #(1 << 2)    /* Disable D-cache */
    bic     x0, x0, #(1 << 12)   /* Disable I-cache */
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

.section ".data"
.align 12

/* Stack (16KB) */
.global __stack_bottom
__stack_bottom:
    .space 0x4000
.global __stack_top
__stack_top:

/* BSS markers */
.global __bss_start
.global __bss_end
