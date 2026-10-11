// virtio-gpu.c
 #include "virtio-gpu.h"
 #define VIRTIO_GPU_BASE 0x0A000000
 #define FB_WIDTH  640
 #define FB_HEIGHT 480
 // -------- Virtio MMIO 寄存器定义 --------
 #define VIRTIO_MMIO_MAGIC_VALUE     (VIRTIO_GPU_BASE + 0x000)
 #define VIRTIO_MMIO_VERSION         (VIRTIO_GPU_BASE + 0x004)
 #define VIRTIO_MMIO_DEVICE_ID       (VIRTIO_GPU_BASE + 0x008)
 #define VIRTIO_MMIO_VENDOR_ID       (VIRTIO_GPU_BASE + 0x00C)
 #define VIRTIO_MMIO_DEVICE_STATUS   (VIRTIO_GPU_BASE + 0x070)
 #define VIRTIO_MMIO_QUEUE_SEL       (VIRTIO_GPU_BASE + 0x080)
 #define VIRTIO_MMIO_QUEUE_NUM_MAX   (VIRTIO_GPU_BASE + 0x084)
 #define VIRTIO_MMIO_QUEUE_NUM       (VIRTIO_GPU_BASE + 0x088)
 #define VIRTIO_MMIO_QUEUE_PFN       (VIRTIO_GPU_BASE + 0x090)
 #define VIRTIO_MMIO_QUEUE_NOTIFY    (VIRTIO_GPU_BASE + 0x100)
 // Virtio GPU 命令
 #define VIRTIO_GPU_CMD_GET_DISPLAY_INFO     0x0100
 #define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D   0x0101
 #define VIRTIO_GPU_CMD_RESOURCE_FLUSH       0x0104
 #define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0105
 #define VIRTIO_GPU_FLAG_FENCE (1 << 0)
 // 设备状态位
 #define VIRTIO_STATUS_ACK       1
 #define VIRTIO_STATUS_DRIVER     2
 #define VIRTIO_STATUS_DRIVER_OK  4
 #define VIRTIO_STATUS_FEAT_OK    8
 // 资源ID
 #define RES_ID 1
 // ========== 【提前定义结构体】==========
 typedef struct {
     uint32_t type;
     uint32_t flags;
     uint64_t fence_id;
     uint32_t ctx_id;
     uint32_t resource_id;
 } VirtioGpuCtrlHdr;
 typedef struct {
     VirtioGpuCtrlHdr hdr;
     uint32_t width;
     uint32_t height;
     uint32_t format;
 } VirtioGpuResourceCreate2D;
 typedef struct {
     VirtioGpuCtrlHdr hdr;
     uint32_t x;
     uint32_t y;
     uint32_t width;
     uint32_t height;
 } VirtioGpuResourceFlush;
 // 简单virtio描述符，用于GPU命令队列
 typedef struct {
     uint64_t addr;
     uint32_t len;
     uint16_t flags;
     uint16_t next;
 } VirtioDesc;
 typedef struct {
     uint16_t flags;
     uint16_t idx;
     uint16_t ring[];
 } VirtioAvail;
 // 帧缓冲，线性RGBA
 static uint32_t framebuffer[FB_WIDTH * FB_HEIGHT];
 #define QSIZE 16
 static VirtioDesc desc[QSIZE];
 static VirtioAvail avail;
 // 寄存器读写
 static inline void mmio_write32(uint32_t reg, uint32_t val)
 {
     *(volatile uint32_t *)reg = val;
 }
 static inline uint32_t mmio_read32(uint32_t reg)
 {
     return *(volatile uint32_t *)reg;
 }
 // 提交GPU命令
 static void virtio_gpu_submit_cmd(void *cmd, uint32_t cmd_len, void *data, uint32_t data_len)
 {
     desc[0].addr = (uint64_t)(uint32_t)cmd;
     desc[0].len = cmd_len;
     desc[0].flags = 1; // VRING_DESC_F_NEXT
     desc[0].next = 1;
     desc[1].addr = (uint64_t)(uint32_t)data;
     desc[1].len = data_len;
     desc[1].flags = 0;
     avail.ring[avail.idx % QSIZE] = 0;
     avail.idx++;
     mmio_write32(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
 }
 void virtio_gpu_init(void)
 {
     // 手动清空帧缓冲，避免编译器自动插入memset
     for(uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++)
     {
         framebuffer[i] = 0;
     }
     // 1. 设备初始化流程
     mmio_write32(VIRTIO_MMIO_DEVICE_STATUS, 0);
     mmio_write32(VIRTIO_MMIO_DEVICE_STATUS, VIRTIO_STATUS_ACK);
     mmio_write32(VIRTIO_MMIO_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
     // 选择队列0
     mmio_write32(VIRTIO_MMIO_QUEUE_SEL, 0);
     uint32_t qmax = mmio_read32(VIRTIO_MMIO_QUEUE_NUM_MAX);
     if(qmax > QSIZE) qmax = QSIZE;
     mmio_write32(VIRTIO_MMIO_QUEUE_NUM, qmax);
     // 把队列物理地址告诉硬件
     uint32_t q_pa = (uint32_t)desc;
     mmio_write32(VIRTIO_MMIO_QUEUE_PFN, q_pa >> 12);
     mmio_write32(VIRTIO_MMIO_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);
     // 创建2D资源
     VirtioGpuResourceCreate2D res_create = {
         .hdr = {VIRTIO_GPU_CMD_RESOURCE_CREATE_2D, 0, 0, 0, RES_ID},
         .width = FB_WIDTH,
         .height = FB_HEIGHT,
         .format = 0x08000000 // VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM
     };
     virtio_gpu_submit_cmd(&res_create, sizeof(res_create), framebuffer, sizeof(framebuffer));
 }
 void virtio_gpu_put_pixel(uint32_t x, uint32_t y, uint32_t color)
 {
     if(x >= FB_WIDTH || y >= FB_HEIGHT) return;
     framebuffer[y * FB_WIDTH + x] = color;
     // 提交刷新命令，VNC刷新画面
     VirtioGpuResourceFlush flush = {
         .hdr = {VIRTIO_GPU_CMD_RESOURCE_FLUSH, 0,0,0, RES_ID},
         .x = 0,
         .y = 0,
         .width = FB_WIDTH,
         .height = FB_HEIGHT
     };
     virtio_gpu_submit_cmd(&flush, sizeof(flush), (void*)0, 0);
 }
 // ========== 清屏函数 ==========
 void virtio_gpu_clear(uint32_t clear_color)
 {
     // 填充整个帧缓冲
     for(uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++)
     {
         framebuffer[i] = clear_color;
     }
     // 提交刷新，更新到VNC
     VirtioGpuResourceFlush flush = {
         .hdr = {VIRTIO_GPU_CMD_RESOURCE_FLUSH, 0,0,0, RES_ID},
         .x = 0,
         .y = 0,
         .width = FB_WIDTH,
         .height = FB_HEIGHT
     };
     virtio_gpu_submit_cmd(&flush, sizeof(flush), (void*)0, 0);
 }