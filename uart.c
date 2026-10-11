// uart.c PL011驱动 QEMU virt ARMv7
#include <stdint.h>
#include "uart.h"

// ========== QEMU virt PL011 UART 物理地址 ==========
#define UART0_BASE     0x09000000U

#define UART_DR        (*(volatile uint32_t *)(UART0_BASE + 0x000))
#define UART_FR        (*(volatile uint32_t *)(UART0_BASE + 0x018))
#define UART_CR        (*(volatile uint32_t *)(UART0_BASE + 0x030))

#define UART_FR_TXFE   (1U << 7U)
#define UART_CR_UARTEN (1U << 0U)
#define FR_TXFF        (1U << 5U)   // TX FIFO 满
#define FR_RXFE        (1U << 4U)   // RX FIFO 空

void uart_init(void)
{
    // 先关闭UART
    UART_CR &= ~UART_CR_UARTEN;
    // 等待发送FIFO清空
    while( !(UART_FR & UART_FR_TXFE) );
    // 开启UART
    UART_CR |= UART_CR_UARTEN;
}

void uart_hex(uint32_t val)
{
    char buf[9];
    const char *hex_chars = "0123456789abcdef";
    buf[8] = '\0';
    for(int i = 7; i >= 0; i--)
    {
        buf[i] = hex_chars[val & 0x0F];
        val >>= 4;
    }
    uart_puts(buf);
}

// UART输出单个字符：等待发送FIFO不满再写DR
void uart_putc(char c)
{
    while(UART_FR & FR_TXFF);
    UART_DR = (uint32_t)c;
    if(c == '\n') {
        uart_putc('\r');
    }
}

void uart_puts(const char *s)
{
    while(*s)
    {
        uart_putc(*s++);
    }
}

// 读取字符：等待接收FIFO非空
char uart_getc(void)
{
    while(UART_FR & FR_RXFE);
    return (char)(UART_DR & 0xFFU);
}
