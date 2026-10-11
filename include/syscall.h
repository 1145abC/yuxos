#ifndef SYSCALL_H
#define SYSCALL_H
#include <stdint.h>
char uart_getc(void)  ;  // ✅ 正确，返回char
// 系统调用：sysnum + 6个参数 a0~a5，一共7个入参
typedef uint32_t (*syscall_func_t)(uint32_t sysnum, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

extern syscall_func_t syscall_table[];

// 内核函数声明
void uart_putc(char c);
void uart_puts(const char *s);
void system_halt(void);

#endif
