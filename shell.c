// shell.c ：UART、shell命令行输入解析
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "virtio-gpu.h"
#include "uart.h"
#include "fs.h"
#include "string.h"
#include "elf.h"

// 为virtio-gpu cls命令定义黑色
#ifndef VIRTIO_GPU_COLOR_BLACK
#define VIRTIO_GPU_COLOR_BLACK 0x00000000
#endif

// 外部声明：proc_start_user 接收ELF入口虚拟地址
extern void proc_start_user(Elf32_Addr user_entry);

// ARMv7 停机休眠
void system_halt(void)
{
    while(1) {
        __asm__ volatile("wfi");
    }
}

// ========== Shell 命令行 ==========
#define CMD_BUF_LEN 128
char cmd_buf[CMD_BUF_LEN];
int cmd_idx = 0;

// 打印动态路径提示符，读取当前真实cwd
void shell_prompt(void)
{
    uart_puts("\033[?25h");
    fs_node_t *cwd = fs_get_cwd();
    if(cwd->parent == NULL)
    {
        // 根目录
        uart_puts("/");
    }
    else
    {
        uart_puts("/");
        fs_pwd(cwd);
    }
    uart_puts(" > ");
    cmd_idx = 0;
}

// 简易字符串转十进制数字
uint32_t atoi_simple(const char *s)
{
    uint32_t val = 0;
    while(*s >= '0' && *s <= '9')
    {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

// 获取命令第n个参数，n=0是命令本身，n=1第一个参数
const char *get_arg(const char *s, int n)
{
    static char tmp[64];
    int idx = 0;
    while(*s)
    {
        // 跳过空格
        while(*s == ' ') s++;
        if(*s == '\0') break;
        if(idx == n)
        {
            int i = 0;
            while(*s && *s != ' ' && i < 63)
            {
                tmp[i++] = *s++;
            }
            tmp[i] = '\0';
            return tmp;
        }
        // 跳到下一个空格
        while(*s && *s != ' ') s++;
        idx++;
    }
    return NULL;
}

void shell_loop(void)
{
    char ch;
    // 初始化文件系统
    fs_init();
    // 进入shell，打开光标，打印第一个提示符
    uart_puts("\033[?25h");
    shell_prompt();

    while(1)
    {
        // 读取UART串口输入
        ch = uart_getc();

        if(ch == '\r' || ch == '\n')
        {
            uart_putc('\n');
            cmd_buf[cmd_idx] = '\0';

            const char *cmd = get_arg(cmd_buf, 0);
            if(cmd == NULL)
            {
                shell_prompt();
                continue;
            }

            if(strcmp(cmd, "exit") == 0)
            {
                uart_puts("YuxOS: exit command, system halt.\n");
                system_halt();
                while(1);
            }
            else if(strcmp(cmd, "help") == 0)
            {
                uart_puts("YuxOS Shell commands:\n");
                uart_puts("  help              - show this help\n");
                uart_puts("  exit              - halt system\n");
                uart_puts("  pixel x y color   - draw pixel\n");
                uart_puts("  cls               - clear screen\n");
                uart_puts("  ls                - list directory\n");
                uart_puts("  cd [dir]          - change directory\n");
                uart_puts("  pwd               - print current path\n");
                uart_puts("  runbin <path>     - load & run ELF binary(e.g runbin /bin/test)\n");
            }
            else if(strcmp(cmd, "pixel") == 0)
            {
                const char *px = get_arg(cmd_buf, 1);
                const char *py = get_arg(cmd_buf, 2);
                const char *pcolor = get_arg(cmd_buf, 3);
                if(px && py && pcolor)
                {
                    uint32_t x = atoi_simple(px);
                    uint32_t y = atoi_simple(py);
                    uint32_t color = atoi_simple(pcolor);
                    virtio_gpu_put_pixel(x,y,color);
                    uart_puts("Draw pixel ok!\n");
                }
                else
                {
                    uart_puts("Usage: pixel x y color\n");
                    uart_puts("Example: pixel 200 100 255\n");
                }
            }
            else if(strcmp(cmd, "cls") == 0)
            {
                virtio_gpu_clear(VIRTIO_GPU_COLOR_BLACK);
                uart_puts("\033[2J\033[H"); // ANSI清屏+光标回到左上角
                uart_puts("Screen cleared.\n");
            }
            else if(strcmp(cmd, "ls") == 0)
            {
                fs_ls();
            }
            else if(strcmp(cmd, "cd") == 0)
            {
                const char *target = get_arg(cmd_buf,1);
                if(target == NULL)
                {
                    uart_puts("Usage: cd [directory]\n");
                }else{
                    fs_cd(target);
                }
            }
            else if(strcmp(cmd, "pwd") == 0)
            {
                uart_puts("Current path: ");
                fs_node_t *cwd = fs_get_cwd();
                if(cwd->parent == NULL)
                {
                    uart_puts("/");
                }
                else
                {
                    uart_puts("/");
                    fs_pwd(cwd);
                }
                uart_putc('\n');
            }
            else if(strcmp(cmd, "runbin") == 0)
            {
                const char *bin_path = get_arg(cmd_buf, 1);
                if(bin_path == NULL)
                {
                    uart_puts("Usage: runbin <elf_file_path>\n");
                    uart_puts("Example: runbin /bin/test\n");
                }
                else
                {
                    uart_puts("[shell] Trying load ELF: ");
                    uart_puts(bin_path);
                    uart_putc('\n');

                    Elf32_Addr entry = elf_load(bin_path);
                    if(entry == 0U)
                    {
                        uart_puts("[shell] ELF load FAILED\n");
                    }
                    else
                    {
                        uart_puts("[shell] Enter user mode ...\n");
                        proc_start_user(entry);
                        uart_puts("[shell] Returned back to kernel shell\n");
                    }
                }
            }
            else
            {
                if(cmd_idx > 0)
                {
                    uart_puts("unknown command > ");
                    uart_puts(cmd_buf);
                    uart_putc('\n');
                }
            }

            // 切换目录后，自动刷新提示符（读取真实cwd）
            shell_prompt();
        }
        // 退格处理
        else if(ch == '\b' || ch == 127)
        {
            if(cmd_idx > 0)
            {
                cmd_idx--;
                uart_puts("\b \b");
            }
        }
        // 普通字符输入
        else if(cmd_idx < CMD_BUF_LEN - 1 && ch >= 32 && ch < 127)
        {
            cmd_buf[cmd_idx++] = ch;
            uart_putc(ch);
        }
    }
}
