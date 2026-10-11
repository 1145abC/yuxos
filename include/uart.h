#ifndef UART_H
#define UART_H
#include <stdint.h>
void uart_hex(uint32_t val);
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);  // 补上这一行！

#endif
