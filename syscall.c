// syscall.c YuxOS ARMv7 32位系统调用表
#include "syscall.h"
#include <stdint.h>

// 前置声明，调用kernel里的串口函数
extern void uart_putc(char c);
extern void uart_puts(const char *s);
extern char uart_getc(void);
extern void virtio_gpu_put_pixel(uint32_t x, uint32_t y, uint32_t color);

// 全局变量，简单模拟PID
static uint32_t g_pid = 1;
// 堆起始地址，给brk使用
static uint32_t heap_start = 0x42000000;
static uint32_t heap_end   = 0x42000000;

// 占位：未实现系统调用
static uint32_t sys_ni_syscall(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    (void)sysnum;
    (void)a0;(void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    return -38; // ENOSYS
}

// ========== 核心实现（只保留5个 + 新增sys_putpixel #400） ==========
// sys_exit  #1  a0=exit_code
static uint32_t sys_exit(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    (void)sysnum;
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    uart_puts("\n[syscall] sys_exit, code=");
    uint32_t code = a0;
    char buf[16];
    int idx = 0;
    if(code == 0)
        buf[idx++] = '0';
    else
    {
        while(code > 0)
        {
            buf[idx++] = '0' + (code % 10);
            code /= 10;
        }
    }
    for(int i = idx-1; i >=0; i--)
        uart_putc(buf[i]);
    uart_puts("\nProcess exit, return to kernel\n");
    // 删掉 system_halt(); 不再停机！
    return 0;
}

// sys_write #4  a0=fd, a1=buf, a2=count
static uint32_t sys_write(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    (void)sysnum;
    (void)a3;(void)a4;(void)a5;
    uint32_t fd = a0;
    uint8_t *buf = (uint8_t *)a1;
    uint32_t count = a2;
    uint32_t i;

    // 只支持stdout(fd=1)
    if(fd != 1)
        return -9; // EBADF

    for(i=0; i<count; i++)
    {
        uart_putc((char)buf[i]);
    }
    return i; // 返回成功写入字节数
}

// sys_read #3  a0=fd, a1=buf, a2=count
static uint32_t sys_read(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    (void)sysnum;
    (void)a3;(void)a4;(void)a5;
    uint32_t fd = a0;
    uint8_t *buf = (uint8_t *)a1;
    uint32_t count = a2;
    uint32_t i = 0;
    if(fd != 0)
        return -9;
    // 简单实现：从串口读字符
    for(; i<count; i++)
    {
        // 等待uart接收
        char ch = uart_getc();
        buf[i] = ch;
        if(ch == '\r') break;
    }
    return i;
}

// sys_getpid #20
static uint32_t sys_getpid(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    (void)sysnum;
    (void)a0;(void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    return g_pid;
}

// sys_brk #45  a0=brk地址
static uint32_t sys_brk(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    (void)sysnum;
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    if(a0 == 0)
    {
        // 查询当前brk
        return heap_end;
    }
    if(a0 > heap_start && a0 < 0x43000000)
    {
        heap_end = a0;
    }
    return heap_end;
}

// 新增：400号系统调用：绘制像素
static uint32_t sys_putpixel(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    (void)sysnum;
    (void)a3;(void)a4;(void)a5;
    uint32_t x = a0;
    uint32_t y = a1;
    uint32_t color = a2;
    virtio_gpu_put_pixel(x, y, color);
    return 0;
}

// 函数指针类型：注意！原型必须和syscall_handler调用匹配
typedef uint32_t (*syscall_func_t)(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5);
#define SYSCALL_MAX 512

// 全局系统调用表，kernel.c extern引用
syscall_func_t syscall_table[SYSCALL_MAX] = {
    [1]  = sys_exit,
    [3]  = sys_read,
    [4]  = sys_write,
    [20] = sys_getpid,
    [45] = sys_brk,
    [400] = sys_putpixel,
};

// 系统调用分发入口

