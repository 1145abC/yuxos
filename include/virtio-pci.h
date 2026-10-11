#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H
#include <stdint.h>

// VirtIO 0.9.5 Legacy 寄存器偏移
#define VIRTIO_REG_DEVICE_STATUS 0x00
#define VIRTIO_REG_QUEUE_SEL      0x0C
#define VIRTIO_REG_QUEUE_NUM      0x0E
#define VIRTIO_REG_QUEUE_NOTIFY   0x14
#define VIRTIO_REG_QUEUE_PFN      0x10

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};

typedef struct {
    uint16_t queue_size;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t last_used;
    uint16_t free_desc_head;
} virtqueue_t;

uint32_t virtio_pci_find_device(uint16_t dev_id);
uint32_t virtio_read_reg(uint32_t bar_base, uint32_t reg);
void virtio_write_reg(uint32_t bar_base, uint32_t reg, uint32_t val);

void virtio_device_reset(uint32_t bar);
void virtio_device_set_status(uint32_t bar, uint32_t status);
uint64_t virtio_device_get_features(uint32_t bar);
void virtio_device_set_features(uint32_t bar, uint64_t features);

int virtq_init(virtqueue_t *q, uint32_t bar, uint16_t qsel, uint16_t qsize);
int virtq_add(virtqueue_t *q, void *hdr, uint32_t hdr_len, void *data, uint32_t data_len, void *status, uint32_t status_len);
void virtq_kick(virtqueue_t *q, uint32_t bar);
int virtq_poll(virtqueue_t *q);

#endif
