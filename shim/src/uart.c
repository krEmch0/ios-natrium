/*
 * UART driver for MSM8996 (Qualcomm UART DM)
 * Minimal implementation for boot-time debugging
 */

#include "uart.h"

/* UART2 base address (BLSP1 UART2) */
#define UART_DM_BASE    0x075B0000

/* UART DM registers */
#define UART_DM_MR1     (UART_DM_BASE + 0x00)
#define UART_DM_MR2     (UART_DM_BASE + 0x04)
#define UART_DM_SR      (UART_DM_BASE + 0x08)
#define UART_DM_CR      (UART_DM_BASE + 0x10)
#define UART_DM_ISR     (UART_DM_BASE + 0x14)
#define UART_DM_IPR     (UART_DM_BASE + 0x18)
#define UART_DM_TFWR    (UART_DM_BASE + 0x1C)
#define UART_DM_RFWR    (UART_DM_BASE + 0x20)
#define UART_DM_DMEN    (UART_DM_BASE + 0x3C)
#define UART_DM_NCF_TX  (UART_DM_BASE + 0x40)
#define UART_DM_BADR    (UART_DM_BASE + 0x44)
#define UART_DM_TF      (UART_DM_BASE + 0x70)
#define UART_DM_RF      (UART_DM_BASE + 0x70)

/* Status register bits */
#define UART_DM_SR_RXRDY        (1 << 0)
#define UART_DM_SR_RXFULL       (1 << 1)
#define UART_DM_SR_TXRDY        (1 << 2)
#define UART_DM_SR_TXEMT        (1 << 3)
#define UART_DM_SR_OVERRUN      (1 << 4)

static inline void writel(uint32_t val, uintptr_t addr) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t readl(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

/**
 * uart_early_init - Initialize UART for early boot output
 *
 * Assumes clocks are already enabled by ABL or platform init
 */
void uart_early_init(void) {
    /* Reset UART */
    writel(0x3, UART_DM_CR);  /* Reset RX */
    writel(0x2, UART_DM_CR);  /* Reset TX */
    writel(0x1, UART_DM_CR);  /* Reset error */
    writel(0x0, UART_DM_CR);  /* Clear command */

    /* Disable DMA */
    writel(0x0, UART_DM_DMEN);

    /* Configure mode */
    writel(0x34, UART_DM_MR1);  /* 8N1, no parity */
    writel(0x04, UART_DM_MR2);  /* Normal mode */

    /* Set baud rate (115200) */
    /* IPR = (UART_CLK / (16 * baud_rate)) - 1 */
    /* For 7.372800 MHz clock: (7372800 / (16 * 115200)) - 1 = 3 */
    writel(0x3, UART_DM_IPR);

    /* Set watermarks */
    writel(0x0, UART_DM_TFWR);  /* TX watermark */
    writel(0x0, UART_DM_RFWR);  /* RX watermark */

    /* Enable TX */
    writel(0x5, UART_DM_CR);  /* Enable TX and RX */
}

/**
 * uart_putc - Output a single character
 */
void uart_putc(char c) {
    /* Wait for TX ready */
    while (!(readl(UART_DM_SR) & UART_DM_SR_TXRDY));

    /* Set number of characters to transmit */
    writel(0x1, UART_DM_NCF_TX);

    /* Write character */
    writel(c, UART_DM_TF);
}

/**
 * uart_puts - Output a string
 */
void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');  /* Add CR for LF */
        }
        uart_putc(*str++);
    }
}

/**
 * uart_puthex - Output a 64-bit hex value
 */
void uart_puthex(uint64_t val) {
    const char *hex = "0123456789ABCDEF";

    uart_putc('0');
    uart_putc('x');

    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

/**
 * uart_putdec - Output a decimal value
 */
void uart_putdec(uint32_t val) {
    if (val == 0) {
        uart_putc('0');
        return;
    }

    char buf[16];
    int i = 0;

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    /* Print in reverse */
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

/**
 * uart_getc - Read a character (non-blocking)
 *
 * Returns: character or -1 if none available
 */
int uart_getc(void) {
    if (readl(UART_DM_SR) & UART_DM_SR_RXRDY) {
        return readl(UART_DM_RF) & 0xFF;
    }
    return -1;
}
