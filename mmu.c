// mmu.c 内存管理模块 ARMv7‑A QEMU virt 32bit
#include <stdint.h>
#include <stdbool.h>

#define NULL ((void*)0)

#define PAGE_SIZE       (1U << 12U)

// QEMU virt：页表放在0x41000000，16KB，4K对齐
#define PGTABLE_BASE_PA    0x41000000U
#define PGTABLE_BASE_VA    0x41000000U
// 堆紧跟页表后面
#define HEAP_START      0x41010000U
#define HEAP_SIZE       (1024U*1024U) //1MB

// ARMv7 L1 Section(1MB大页) bit1=1 bit0=0
#define L1_SECTION          (1U << 1U)
// AP[1:0] = 00：内核RW，用户禁止访问
#define L1_AP_KERN_RW       (0U << 10U)
// TEX=0 C=1 B=1 普通可缓存RAM
#define L1_CACHE_RAM        ((1U << 3U) | (1U << 2U))
// Device：非缓存强序设备MMIO
#define L1_DEV_NOCACHE      (0U << 3U | 0U << 2U)

static uint32_t* l1_table = (uint32_t*)PGTABLE_BASE_VA;

typedef struct block {
    uint32_t size;
    struct block *next;
    bool free;
} block_t;

static block_t *heap_head = NULL;

// 使无效全部TLB、I‑Cache D‑Cache
static void mmu_invalidate_all(void)
{
    __asm__ volatile(
        "mcr p15, 0, %0, c8, c7, 0\n"  /* invalidate TLB */
        "mcr p15, 0, %0, c7, c5, 0\n"  /* invalidate I cache */
        "mcr p15, 0, %0, c7, c6, 0\n"  /* invalidate D cache */
        "dsb\n"
        "isb\n"
        : : "r"(0U)
    );
}

void mmu_init(void)
{
    // 清空L1页表 4096项 *4 =16KB
    for(int i = 0; i < 4096; i++){
        l1_table[i] = 0U;
    }

    // =========恒等映射 VA=PA =========
    // 1) RAM区域 0x40000000 ~ 0x60000000 (512MB) ：1MB section，开启Cache
    for(uint32_t va = 0x40000000U; va < (0x40000000U + (512U << 20U)); va += (1U << 20U))
    {
        uint32_t pa = va;
        uint32_t idx = va >> 20U;
        uint32_t pte = (pa & 0xFFF00000U)
                     | L1_SECTION
                     | L1_AP_KERN_RW
                     | L1_CACHE_RAM;
        l1_table[idx] = pte;
    }

    // 2) QEMU MMIO外设区域：0x08000000 ~ 0x0FFFFFFF，Device无缓存
    // PL011 UART 0x09000000 就在这个区间
    for(uint32_t va = 0x08000000U; va <= 0x0FFFFFFFU; va += (1U << 20U))
    {
        uint32_t pa = va;
        uint32_t idx = va >> 20U;
        uint32_t pte = (pa & 0xFFF00000U)
                     | L1_SECTION
                     | L1_AP_KERN_RW
                     | L1_DEV_NOCACHE;
        l1_table[idx] = pte;
    }

    mmu_invalidate_all();

    // TTBR0 写入物理地址！！
    __asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(PGTABLE_BASE_PA));
    // DACR 全部域为client
    __asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(0xFFFFFFFFU));

    uint32_t sctlr;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));

    sctlr |=  (1U << 0U);   // MMU ON
    sctlr |=  (1U << 12U);  // I‑Cache ON
    sctlr |=  (1U << 2U);   // D‑Cache ON
    sctlr &= ~(1U << 1U);  // 禁止对齐错误异常

    // 开启MMU，加dsb+isb + 跳转刷新流水线（核心修复！）
    __asm__ volatile(
        "mcr p15,0,%0,c1,c0,0\n"
        "dsb\n"
        "isb\n"
        "mov r0, pc\n"
        "add r0, r0, #4\n"
        "bx r0\n"
        : : "r"(sctlr) : "r0"
    );
}

// 堆初始化
void heap_init(void)
{
    heap_head = (block_t*)HEAP_START;
    heap_head->size = HEAP_SIZE - sizeof(block_t);
    heap_head->next = NULL;
    heap_head->free = true;
}

// malloc 简单4字节对齐
void* malloc(uint32_t size)
{
    if(!size) return NULL;
    // 向上4字节对齐
    uint32_t align = 4U;
    size = (size + align -1U) & ~(align-1U);

    block_t *curr = heap_head;
    while(curr)
    {
        if(curr->free && curr->size >= size)
        {
            if(curr->size > size + sizeof(block_t))
            {
                block_t *newblk = (block_t*)((uint8_t*)curr + sizeof(block_t) + size);
                newblk->size = curr->size - size - sizeof(block_t);
                newblk->next = curr->next;
                newblk->free = true;

                curr->next = newblk;
                curr->size = size;
            }
            curr->free = false;
            return (void*)((uint8_t*)curr + sizeof(block_t));
        }
        curr = curr->next;
    }
    return NULL;
}

void free(void *ptr)
{
    if(!ptr) return;
    block_t *blk = (block_t*)((uint8_t*)ptr - sizeof(block_t));
    blk->free = true;

    //向后合并空闲块
    if(blk->next != NULL && blk->next->free)
    {
        blk->size += blk->next->size + sizeof(block_t);
        blk->next = blk->next->next;
    }
}
