// kernel.c YuxOS 主内核 【QEMU virt ARMv7‑A 32位】
// 内核初始化、进程、syscall，shell逻辑移到shell.c
#include <stdint.h>
#include <stdbool.h>
#include "virtio-gpu.h"
#include "syscall.h"
#include "uart.h"
#define NULL ((void*)0)



// kernel.c 全局区域
void (*user_app_entry)(void) = NULL;



// 外部声明
extern void shell_loop(void);
extern uint32_t (*syscall_table[])(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5);
uint32_t syscall_handler(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5);

void proc_start_user(void);



// 外部声明：暂时未实现，后续再补
extern void mmu_init(void);
extern void heap_init(void);

#define NULL ((void*)0)

// ========== SVC / 系统调用兼容层 【ARMv7 32bit】 ==========

// 保存用户态上下文的结构体，简易版
struct trapframe {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;  // syscall number
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t sp;
    uint32_t lr;
    uint32_t pc;
    uint32_t cpsr;
};

// SVC异常处理C入口，由汇编调用
void svc_handler_c(struct trapframe *tf)
{
    uint32_t sysnum = tf->r7;
    uint32_t a0 = tf->r0;
    uint32_t a1 = tf->r1;
    uint32_t a2 = tf->r2;
    uint32_t a3 = tf->r3;
    uint32_t a4 = tf->r4;
    uint32_t a5 = tf->r5;

    uint32_t ret = syscall_handler(sysnum, a0, a1, a2, a3, a4, a5);
    // 系统调用返回值写入r0
    tf->r0 = ret;
}

// 汇编SVC入口，放在boot.S，这里只是声明
extern void svc_entry(void);

#define PROC_MAX 4
enum proc_state {
    PROC_FREE = 0,
    PROC_RUNNING,
    PROC_ZOMBIE
};

struct proc {
    uint32_t pid;
    uint32_t state;
    uint32_t brk_start;
    uint32_t brk_end;
};
static struct proc proc_table[PROC_MAX];
static uint32_t g_pid = 1;
static int current_proc_idx = -1;

// ========= 新增全局标记：用户进程调用exit标志 =========
bool g_user_exit_flag = false;

// 分配PCB
static int proc_alloc(void)
{
    for(int i = 0; i < PROC_MAX; i++)
    {
        if(proc_table[i].state == PROC_FREE)
        {
            proc_table[i].pid = g_pid++;
            proc_table[i].state = PROC_RUNNING;
            proc_table[i].brk_start = 0x42000000 + (proc_table[i].pid * 0x100000);
            proc_table[i].brk_end = proc_table[i].brk_start;
            return i;
        }
    }
    return -1;
}

// 销毁进程：标记Zombie，返回0成功，-1失败
int proc_destroy(uint32_t pid)
{
    for(int i = 0; i < PROC_MAX; i++)
    {
        if(proc_table[i].pid == pid && proc_table[i].state == PROC_RUNNING)
        {
            proc_table[i].state = PROC_ZOMBIE;
            uart_puts("\n[proc] Process ");
            uint32_t p = pid;
            char buf[16];
            int idx = 0;
            if(p == 0) buf[idx++]='0';
            else while(p>0){ buf[idx++]='0'+(p%10); p /=10; }
            for(int j=idx-1;j>=0;j--) uart_putc(buf[j]);
            uart_puts(" marked zombie\n");
            return 0;
        }
    }
    return -1;
}

// 判断是否还有存活进程
static int has_alive_process(void)
{
    for(int i = 0; i < PROC_MAX; i++)
    {
        if(proc_table[i].state == PROC_RUNNING)
            return 1;
    }
    return 0;
}

// 全局：用户程序入口（在汇编里定义，swi测试app）

// 拉起用户进程
void proc_start_user(void)
{
    g_user_exit_flag = false;

    uart_puts("\n[proc] Spawn new user process\n");
    int idx = proc_alloc();
    if(idx < 0)
    {
        uart_puts("[proc] No free PCB, create process fail!\n");
        return;
    }
    current_proc_idx = idx;
    uart_puts("[proc] Process spawn ok, pid=");
    uint32_t p = proc_table[idx].pid;
    char buf[16];
    int i = 0;
    if(p ==0) buf[i++]='0';
    else while(p>0){ buf[i++]='0'+(p%10); p /=10; }
    for(int j=i-1;j>=0;j--) uart_putc(buf[j]);
    uart_puts("\n");

    uint32_t user_sp = 0x41FFFF00;
    uint32_t usr_cpsr = 0xD0; // USR模式，IRQ/FIQ关闭
    uint32_t user_entry = (uint32_t)user_app_entry;

    __asm__ volatile (
        // 构造异常返回帧，放在SVC栈
        "sub sp, sp, #16\n"
        "str %[usr_sp], [sp, #0]\n"
        "str %[usr_cpsr], [sp, #4]\n"
        "str %[user_entry], [sp, #8]\n"
        "str lr, [sp, #12]\n"

        "ldr r0, [sp, #4]\n"
        "msr spsr_cxsf, r0\n"
        "ldr lr, [sp, #8]\n"
        "movs pc, lr\n"
        :
        : [usr_sp] "r"(user_sp),
          [usr_cpsr] "r"(usr_cpsr),
          [user_entry] "r"(user_entry)
        : "memory"
    );

    uart_puts("[proc] User process returned to kernel shell\n");
    uart_puts("\033[?25h");
}


// 系统调用处理总入口，对接syscall_table
uint32_t syscall_handler(uint32_t sysnum, uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5)
{
    #define SYS_exit 1
    if(sysnum == SYS_exit)
    {
        uart_puts("\n[syscall] sys_exit, code=");
        uint32_t exit_code = a0;
        char buf[16];
        int idx = 0;
        if(exit_code ==0) buf[idx++]='0';
        else while(exit_code>0){ buf[idx++]='0'+(exit_code%10); exit_code /=10; }
        for(int j=idx-1;j>=0;j--) uart_putc(buf[j]);
        uart_puts("\nProcess exit, return to kernel\n");

        if(current_proc_idx >=0)
        {
            proc_destroy(proc_table[current_proc_idx].pid);
            current_proc_idx = -1;
        }
        g_user_exit_flag = true; // 标记用户进程要退出
        return 0; // SYS_exit处理完直接return，不再执行后面syscall_table逻辑
    }

    extern syscall_func_t syscall_table[];
    if(sysnum > 999 || syscall_table[sysnum] == NULL)
    {
        return -38; // ENOSYS
    }
    return syscall_table[sysnum](sysnum, a0,a1,a2,a3,a4,a5);
}

// ========== 内核主入口 ==========
void kernel_main(void)
{
    uart_init();
    uart_puts("\n=====================================\n");
    uart_puts("        YuxOS BareMetal ARMv7‑A      \n");
    uart_puts("          QEMU virt 32bit            \n");
    uart_puts("=====================================\n");
    uart_puts("[0] Bootloader finished, enter kernel_main\n");
    uart_puts("[1] UART PL011 base:0x09000000\n");
    uart_puts("[2] heap_init() stub, not implemented\n");

    // heap_init(); // 还未实现，否则链接报错
    uart_puts("[3] Heap uninitialized\n");
    uart_puts("[4] MMU is skipped temporarily\n");
    // mmu_init(); // 后续实现ARMv7 MMU再打开
    uart_puts("[5] Running in physical address mode\n");

    // 初始化virtio-gpu显卡
    uart_puts("[6] virtio-gpu init start\n");
    virtio_gpu_init();
    uart_puts("[7] virtio-gpu init done\n");

    uart_puts("[8] System initialization complete\n");
    uart_puts("[9] YuxOS Shell ready, type 'help'\n");
    uart_puts("=====================================\n");

    shell_loop();
}
