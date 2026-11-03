/*
 * UART driver header
 */

#ifndef _UART_H_
#define _UART_H_

#include <stdint.h>

void uart_early_init(void);
void uart_putc(char c);
void uart_puts(const char *str);
void uart_puthex(uint64_t val);
void uart_putdec(uint32_t val);
int uart_getc(void);

#endif /* _UART_H_ */
