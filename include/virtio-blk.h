#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H
#include <stdint.h>

void virtio_blk_init(void);
int virtio_blk_read(uint64_t sector, void *buf, uint32_t sector_cnt);
int virtio_blk_write(uint64_t sector, const void *buf, uint32_t sector_cnt);

#endif
