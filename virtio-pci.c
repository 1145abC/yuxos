#include "virtio-pci.h"
#include "uart.h"
#include "string.h"
#include <stdint.h>

// 简化版PCI ECAM，QEMU virt ARMv7
#define PCI_ECAM_BASE 0x40000000
#define PCI_BUS_SHIFT 20
#define PCI_DEV_SHIFT 15
#define PCI_FUNC_SHIFT 12

static volatile uint32_t *pci_cfg_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uintptr_t addr = PCI_ECAM_BASE + ((uintptr_t)bus << PCI_BUS_SHIFT)
        + ((uintptr_t)dev << PCI_DEV_SHIFT)
        + ((uintptr_t)func << PCI_FUNC_SHIFT) + reg;
    return (volatile uint32_t *)addr;
}

uint32_t virtio_pci_find_device(uint16_t dev_id)
{
    uint8_t bus = 0;
    for(uint8_t dev = 0; dev < 32; dev++)
    {
        volatile uint32_t *vendor_reg = pci_cfg_addr(bus, dev, 0, 0);
        uint32_t vendor = *vendor_reg;
        if(vendor == 0xFFFF) continue;

        volatile uint32_t *dev_reg = pci_cfg_addr(bus, dev, 0, 4);
        uint32_t device = (*dev_reg >> 16) & 0xFFFF;
        if(device == dev_id)
        {
            volatile uint32_t *bar0_reg = pci_cfg_addr(bus, dev, 0, 0x10);
            uint32_t bar0 = *bar0_reg;
            return bar0 & ~0xF;
        }
    }
    return 0;
}

uint32_t virtio_read_reg(uint32_t bar_base, uint32_t reg)
{
    return *(volatile uint32_t *)(bar_base + reg);
}

void virtio_write_reg(uint32_t bar_base, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(bar_base + reg) = val;
}

// 重置virtio设备
void virtio_device_reset(uint32_t bar)
{
    virtio_write_reg(bar, VIRTIO_REG_DEVICE_STATUS, 0);
}

// 设置设备状态位
void virtio_device_set_status(uint32_t bar, uint32_t status)
{
    uint32_t old = virtio_read_reg(bar, VIRTIO_REG_DEVICE_STATUS);
    virtio_write_reg(bar, VIRTIO_REG_DEVICE_STATUS, old | status);
}

// 获取设备feature
uint64_t virtio_device_get_features(uint32_t bar)
{
    uint32_t low, high;
    virtio_write_reg(bar, 0x04, 0);
    low = virtio_read_reg(bar, 0x08);
    virtio_write_reg(bar, 0x04, 1);
    high = virtio_read_reg(bar, 0x08);
    return ((uint64_t)high << 32) | low;
}

void virtio_device_set_features(uint32_t bar, uint64_t features)
{
    uint32_t low = (uint32_t)features;
    uint32_t high = (uint32_t)(features >> 32);
    virtio_write_reg(bar, 0x04, 0);
    virtio_write_reg(bar, 0x08, low);
    virtio_write_reg(bar, 0x04, 1);
    virtio_write_reg(bar, 0x08, high);
}

// 队列内存：0x40830000
#define VIRTQ_MEM_BASE 0x40830000

int virtq_init(virtqueue_t *q, uint32_t bar, uint16_t qsel, uint16_t qsize)
{
    virtio_write_reg(bar, VIRTIO_REG_QUEUE_SEL, qsel);
    uint16_t q_sz = virtio_read_reg(bar, VIRTIO_REG_QUEUE_NUM);
    if(q_sz == 0) return -1;
    if(qsize > q_sz) qsize = q_sz;

    q->queue_size = qsize;
    q->desc  = (struct virtq_desc *)(VIRTQ_MEM_BASE + 0x0000);
    q->avail = (struct virtq_avail *)(VIRTQ_MEM_BASE + 0x1000);
    q->used  = (struct virtq_used *)(VIRTQ_MEM_BASE + 0x2000);

    memset(q->desc, 0, qsize * sizeof(struct virtq_desc));
    memset(q->avail, 0, 4096);
    memset(q->used, 0, 4096);

    q->last_used = 0;
    q->free_desc_head = 0;

    // 初始化单向空闲链表
    for(uint16_t i = 0; i < qsize - 1; i++)
    {
        q->desc[i].next = i + 1;
    }
    q->desc[qsize - 1].next = 0xFFFF;

    // 告诉设备队列物理地址（Queue PFN 需要页号，>>12）
    uint64_t pa = (uint64_t)(uintptr_t)q->desc;
    virtio_write_reg(bar, VIRTIO_REG_QUEUE_PFN, (uint32_t)(pa >> 12));
    virtio_write_reg(bar, VIRTIO_REG_QUEUE_SEL, qsel);
    virtio_write_reg(bar, 0x0A, 1);

    uart_puts("virtqueue init ok\n");
    return 0;
}

// 单独分配1个描述符
static uint16_t virtq_desc_alloc(virtqueue_t *q)
{
    uint16_t d = q->free_desc_head;
    if(d == 0xFFFF)
        return 0xFFFF;
    q->free_desc_head = q->desc[d].next;
    return d;
}

// 构造描述符链：hdr(RO) -> data(WO) -> status(WO)
int virtq_add(virtqueue_t *q, void *hdr, uint32_t hdr_len, void *data, uint32_t data_len, void *status, uint32_t status_len)
{
    uint16_t d0 = virtq_desc_alloc(q);
    if(d0 == 0xFFFF)
    {
        uart_puts("virtq_add: no desc d0!\n");
        return -1;
    }
    uint16_t d1 = virtq_desc_alloc(q);
    if(d1 == 0xFFFF)
    {
        uart_puts("virtq_add: no desc d1!\n");
        return -1;
    }
    uint16_t d2 = virtq_desc_alloc(q);
    if(d2 == 0xFFFF)
    {
        uart_puts("virtq_add: no desc d2!\n");
        return -1;
    }

    q->desc[d0].addr = (uint64_t)(uintptr_t)hdr;
    q->desc[d0].len  = hdr_len;
    q->desc[d0].flags= VIRTQ_DESC_F_NEXT;
    q->desc[d0].next = d1;

    q->desc[d1].addr = (uint64_t)(uintptr_t)data;
    q->desc[d1].len  = data_len;
    q->desc[d1].flags= VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    q->desc[d1].next = d2;

    q->desc[d2].addr = (uint64_t)(uintptr_t)status;
    q->desc[d2].len  = status_len;
    q->desc[d2].flags= VIRTQ_DESC_F_WRITE;
    q->desc[d2].next = 0xFFFF;

    uint16_t idx = q->avail->idx % q->queue_size;
    q->avail->ring[idx] = d0;
    q->avail->idx ++;
    uart_puts("virtq_add: ok\n");
    return 0;
}

// Kick设备，通知设备处理请求
void virtq_kick(virtqueue_t *q, uint32_t bar)
{
    (void)q;
    virtio_write_reg(bar, VIRTIO_REG_QUEUE_NOTIFY, 0);
    uart_puts("virtq: kicked queue 0\n");
}

// 等待设备完成
int virtq_poll(virtqueue_t *q)
{
    uart_puts("virtq: poll waiting...\n");
    while(q->used->idx == q->last_used);
    uart_puts("virtq: request done!\n");
    q->last_used = q->used->idx;
    return 0;
}
