// boot.S ARMv7‑A 32位启动汇编，YuxOS for QEMU virt (armv7)
.section ".text.boot"

// 【新增】ARMv7 异常向量表，必须放在.text.boot最开头
.global vector_table
vector_table:
    b _start                // Reset 复位向量
    b .                     // Undefined Instruction
    b svc_entry             // SVC(swi) 系统调用异常入口
    b .                     // Prefetch Abort
    b .                     // Data Abort
    b .                     // Reserved
    b .                     // IRQ
    b .                     // FIQ

.extern g_user_exit_flag

.global _start
_start:
    // 关闭 IRQ FIQ
    cpsid i
    cpsid f

    // ========== 关键：设置VBAR向量基址寄存器，告诉CPU向量表在哪里 ==========
    ldr r0, =vector_table
    mcr p15, 0, r0, c12, c0, 0

    // 临时栈放到DRAM高地址，远离内核镜像，0x41000000（16MB位置）
    ldr sp, =0x41000000

    // 清零BSS段
    ldr r0, =__bss_start
    ldr r1, =__bss_end
bss_clear_loop:
    cmp r0, r1
    bge bss_done
    mov r2, #0
    str r2, [r0], #4
    b bss_clear_loop
bss_done:

    // 切换到链接脚本定义的内核栈
    ldr sp, =__stack_top

    // 跳C内核主函数
    bl kernel_main

// 如果kernel_main返回，停机
halt_loop:
    wfi
    b halt_loop

// ===================== SVC异常处理入口【修复版本，删掉非法mrs】=====================
svc_entry:
    // 先保存bl svc_handler_c的返回地址到栈！C函数会破坏lr！
    stmfd sp!, {lr}

    // 构建 trapframe：r0~r12, sp, lr, pc, spsr 一共17个uint32_t
    sub sp, sp, #(17 * 4)
    stmia sp, {r0 - r12}          // 保存 r0 ~ r12

    // ===== 重点：用户进程的sp/lr由proc_start_user预先存入，异常进来不再读取banked寄存器 =====
    // 这里暂时先存【进入异常前的lr】，用户进程启动时会填充trapframe
    str r13, [sp, #13 * 4]         // trapframe[13] = sp（启动用户时覆盖）
    str r14, [sp, #14 * 4]         // trapframe[14] = lr（启动用户时覆盖）

    sub r0, lr, #4                // ✅SVC异常：lr=PC+4，真实PC = lr-4
    str r0, [sp, #15 * 4]         // trapframe[15] = 用户PC

    mrs r0, spsr
    str r0, [sp, #16 * 4]         // trapframe[16] = spsr

    // r0 = trapframe指针，调用C处理函数
    mov r0, sp
    bl svc_handler_c

    // ===== 判断用户进程退出标记 =====
    ldr r0, =g_user_exit_flag
    ldr r1, [r0]
    cmp r1, #1
    beq exit_to_kernel

    // =====【修复】恢复用户上下文，返回用户态=====
    ldr r1, [sp, #16*4]
    msr spsr_cxsf, r1

    ldr r1, [sp, #15*4] // pc
    ldr r2, [sp, #14*4] // lr
    ldr r3, [sp, #13*4] // sp

    ldmia sp, {r0 - r12}
    // 单独恢复sp、lr、pc
    mov r13, r3
    mov r14, r2
    movs pc, r1

exit_to_kernel:
    // 用户调用SYS_exit，直接返回内核
    ldr r0, =g_user_exit_flag
    mov r1, #0
    str r1, [r0]              // 清除退出标记
    add sp, sp, #(17 * 4)     // 释放trapframe

    ldmfd sp!, {lr}           // ✅取回之前保存的返回地址！
    bx lr                     // 回到svc_entry的调用点
