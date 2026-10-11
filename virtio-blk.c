#include "virtio-blk.h"
#include "virtio-pci.h"
#include "uart.h"
#include "string.h"
#include <stdint.h>

#define VIRTIO_BLK_T_IN     0
#define VIRTIO_BLK_T_OUT    1
#define VIRTIO_BLK_S_OK     0

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) virtio_blk_req_hdr_t;

static uint32_t blk_bar = 0;
static virtqueue_t blk_vq;

// DMA缓冲区：全局静态，物理地址固定，用于virtio请求
static virtio_blk_req_hdr_t blk_req;
static uint8_t blk_status;

void virtio_blk_init(void)
{
    uart_puts("virtio-blk: init start\n");
    blk_bar = virtio_pci_find_device(0x1001);
    if(blk_bar == 0)
    {
        uart_puts("virtio-blk: device not found!\n");
        return;
    }

    // ========== 标准virtio设备初始化流程 ==========
    virtio_device_reset(blk_bar);
    virtio_device_set_status(blk_bar, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_device_set_status(blk_bar, VIRTIO_STATUS_DRIVER);
    uint64_t feat = virtio_device_get_features(blk_bar);
    (void)feat;
    // 不协商高级特性，直接accept
    virtio_device_set_features(blk_bar, 0);

    // 初始化队列0，队列大小128
    virtq_init(&blk_vq, blk_bar, 0, 128);

    virtio_device_set_status(blk_bar, VIRTIO_STATUS_DRIVER_OK);
    uart_puts("virtio-blk: init ok\n");
}

int virtio_blk_read(uint64_t sector, void *buf, uint32_t sector_cnt)
{
    uart_puts("virtio-blk read: sector=");
    char tmp[20];
    itoa(sector, tmp,16);
    uart_puts(tmp);
    uart_putc('\n');

    uint8_t *dest = (uint8_t *)buf;
    for(uint32_t i=0; i < sector_cnt; i++)
    {
        // 使用全局静态DMA请求头，不再使用栈变量
        blk_req.type = VIRTIO_BLK_T_IN;
        blk_req.reserved = 0;
        blk_req.sector = sector + i;

        uart_puts("virtio-blk: submit req\n");
        // out: blk_req(请求头)
        // in[0]: dest 512字节扇区数据
        // in[1]: blk_status 1字节返回状态
        virtq_add(&blk_vq, &blk_req, sizeof(blk_req), dest, 512, &blk_status, 2);
        virtq_kick(&blk_vq, blk_bar);
        virtq_poll(&blk_vq);

        if(blk_status != VIRTIO_BLK_S_OK)
        {
            uart_puts("virtio-blk read error! status=");
            uart_putc('0' + blk_status);
            uart_putc('\n');
            return -1;
        }
        dest += 512;
        uart_puts("virtio-blk: one sector ok\n");
    }
    return 0;
}

int virtio_blk_write(uint64_t sector, const void *buf, uint32_t sector_cnt)
{
    uart_puts("virtio-blk write stub\n");
    return 0;
}
