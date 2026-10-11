// virtio-gpu.h
#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include <stdint.h>

// 和virtio-gpu.c保持一致的分辨率
#define VIRTIO_GPU_FB_WIDTH   640
#define VIRTIO_GPU_FB_HEIGHT  480

// 颜色格式：B8G8R8A8（QEMU virtio-gpu默认）
// 示例：RED = 0xFF0000FF，GREEN=0xFF00FF00，BLUE=0xFFFF0000
#define VIRTIO_GPU_COLOR_RED     0xFF0000FF
#define VIRTIO_GPU_COLOR_GREEN   0xFF00FF00
#define VIRTIO_GPU_COLOR_BLUE    0xFFFF0000
#define VIRTIO_GPU_COLOR_WHITE   0xFFFFFFFF
#define VIRTIO_GPU_COLOR_BLACK   0x000000FF

void virtio_gpu_init(void);
void virtio_gpu_put_pixel(uint32_t x, uint32_t y, uint32_t color);
// 新增清屏函数声明
void virtio_gpu_clear(uint32_t clear_color);

#endif
