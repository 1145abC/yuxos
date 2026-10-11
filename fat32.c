#include "fat32.h"
#include "virtio-blk.h"
#include "uart.h"
#include "string.h"
#include <stddef.h>
#include <stdint.h>

// ========== 【前置！放到最前面，解决隐式声明崩溃】==========
static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
           | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 数字转字符串，替代itoa，消除未定义符号
static void uint32_to_str(uint32_t val, char *buf)
{
    int i = 0;
    if(val == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        while(val > 0)
        {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        }
    }
    buf[i] = '\0';
    // 反转
    int l = 0, r = i-1;
    while(l < r)
    {
        char t = buf[l];
        buf[l] = buf[r];
        buf[r] = t;
        l++; r--;
    }
}

// FAT32 BPB 引导扇区结构体（512字节扇区）
typedef struct {
    uint8_t  jump[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;    // 每个扇区字节，一般=512
    uint8_t  sectors_per_cluster; // 每簇几个扇区
    uint16_t reserved_sectors;    // 保留扇区数量
    uint8_t  fat_count;           // FAT表个数，一般是2
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

// FAT目录项
typedef struct {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t size;
} __attribute__((packed)) fat_dir_entry_t;

// 全局变量
static fat32_bpb_t g_bpb;
static uint64_t g_fat_start_sector;
static uint64_t g_data_start_sector;
// 缓冲区4字节对齐
static uint8_t sector_buf[512] __attribute__((aligned(4)));

static uint32_t fat_get_next_cluster(uint32_t cluster)
{
    uint64_t fat_sector = g_fat_start_sector + (cluster * 4) / 512;
    uint32_t offset = (cluster *4) % 512;
    virtio_blk_read(fat_sector, sector_buf, 1);
    uint32_t next = read_le32(sector_buf + offset);
    return next & 0x0FFFFFFFU;
}

// 挂载：读取引导扇区，全部校验关闭
int fat32_mount(void)
{
    uart_puts("FAT32: mounting partition\n");
    if(virtio_blk_read(0, sector_buf, 1) != 0)
    {
        uart_puts("FAT32: read boot sector fail!\n");
        return -1;
    }
    uart_puts("[DBG] read sector0 done\n");

    // ===================== 【插入的dump调试代码】 =====================
    uart_puts("\n==== DUMP sector_buf 0~63 ====\n");
    for(int i = 0; i < 64; i++)
    {
        uint8_t v = sector_buf[i];
        // 高4位
        if((v >> 4) >= 10)
            uart_putc('A' + (v >> 4) - 10);
        else
            uart_putc('0' + (v >> 4));
        // 低4位
        if((v & 0x0F) >= 10)
            uart_putc('A' + (v & 0x0F) - 10);
        else
            uart_putc('0' + (v & 0x0F));
        uart_putc(' ');
        if((i+1) % 16 == 0)
            uart_putc('\n');
    }
    uart_puts("==============================\n");
    // =================================================================

    // 直接从sector_buf读取，分步调试，不一次性拷贝结构体
    uint16_t bytes_per_sector = read_le16(sector_buf + 0x0B);
    uart_puts("[DBG] parse bytes_per_sector ok\n");
    uint8_t sectors_per_cluster = sector_buf[0x0D];
    uart_puts("[DBG] parse sectors_per_cluster ok\n");
    uint16_t reserved_sectors = read_le16(sector_buf + 0x0E);
    uart_puts("[DBG] parse reserved_sectors ok\n");
    uint8_t fat_count = sector_buf[0x10];
    uart_puts("[DBG] parse fat_count ok\n");
    uint32_t fat_size_32 = read_le32(sector_buf + 0x24);
    uart_puts("[DBG] parse fat_size_32 ok\n");
    uint32_t root_cluster = read_le32(sector_buf + 0x2C);
    uart_puts("[DBG] parse root_cluster ok\n");

    g_fat_start_sector = reserved_sectors;
    uart_puts("[DBG] calc fat_start_sector ok\n");
    g_data_start_sector = g_fat_start_sector + (uint64_t)fat_count * fat_size_32;
    uart_puts("[DBG] calc data_start_sector ok\n");

    uart_puts("DBG: reserved_sectors = ");
    char tmp[20];
    uint32_to_str(reserved_sectors, tmp); uart_puts(tmp); uart_putc('\n');

    uart_puts("DBG: fat_count = ");
    uint32_to_str(fat_count, tmp); uart_puts(tmp); uart_putc('\n');

    uart_puts("DBG: fat_size_32 = ");
    uint32_to_str(fat_size_32, tmp); uart_puts(tmp); uart_putc('\n');

    uart_puts("DBG: g_fat_start_sector = ");
    uint32_to_str(g_fat_start_sector, tmp); uart_puts(tmp); uart_putc('\n');

    uart_puts("DBG: g_data_start_sector = ");
    uint32_to_str(g_data_start_sector, tmp); uart_puts(tmp); uart_putc('\n');

    // ========== 修复：mkfs生成镜像root_cluster=0，替换为簇2 ==========
    if(root_cluster == 0)
    {
        uart_puts("[FAT32 INFO] root cluster field was zero, use cluster #2\n");
        root_cluster = 2;
    }
    // ===============================================================
    // 存入全局bpb
    g_bpb.bytes_per_sector = bytes_per_sector;
    g_bpb.sectors_per_cluster = sectors_per_cluster;
    g_bpb.reserved_sectors = reserved_sectors;
    g_bpb.fat_count = fat_count;
    g_bpb.fat_size_32 = fat_size_32;
    g_bpb.root_cluster = root_cluster;

    uart_puts("FAT32: mount success\n");
    uart_puts("FAT32: root cluster = ");
    uint32_to_str(root_cluster, tmp);
    uart_puts(tmp);
    uart_putc('\n');
    return 0;
}

// 遍历单个簇内目录项，增加安全读取，规避非对齐访问
static void fat_read_dir_entry(const uint8_t *buf, fat_dir_callback cb)
{
    fat_dir_entry_t ent;
    // 逐个拷贝字段，避免直接指针强转非对齐
    memcpy(&ent.name, buf + 0, 11);
    ent.attr = buf[0x0B];
    ent.nt_res = buf[0x0C];
    ent.create_time_tenth = buf[0x0D];
    ent.create_time = read_le16(buf + 0x0E);
    ent.create_date = read_le16(buf + 0x10);
    ent.access_date = read_le16(buf + 0x12);
    ent.cluster_high = read_le16(buf + 0x14);
    ent.modify_time = read_le16(buf + 0x16);
    ent.modify_date = read_le16(buf + 0x18);
    ent.cluster_low = read_le16(buf + 0x1A);
    ent.size = read_le32(buf + 0x1C);

    if(ent.name[0] == 0x00) return;
    if(ent.name[0] == 0xE5) return;
    if(ent.attr & 0x0F) return; // 跳过长文件名项

    char name[13];
    int p=0;
    for(int k=0;k<8;k++){
        if(ent.name[k] == ' ') break;
        name[p++] = ent.name[k];
    }
    if(ent.name[8] != ' ')
    {
        name[p++] = '.';
        for(int k=8;k<11;k++){
            if(ent.name[k] == ' ') break;
            name[p++] = ent.name[k];
        }
    }
    name[p] = '\0';
    bool is_dir = (ent.attr & 0x10) !=0;
    cb(name, is_dir, ent.size);
}

// 遍历目录簇链（支持跨多个簇）
static int fat_read_cluster_dir(uint32_t start_cluster, fat_dir_callback cb)
{
    uint32_t cluster = start_cluster;
    while(cluster < 0x0FFFFFF8U) // FAT32 EOF标记
    {
        uint64_t sector = g_data_start_sector + (uint64_t)(cluster - 2) * g_bpb.sectors_per_cluster;
        for(uint8_t s=0; s < g_bpb.sectors_per_cluster; s++)
        {
            virtio_blk_read(sector + s, sector_buf, 1);
            for(int i=0; i<512; i += 32)
            {
                if(sector_buf[i] == 0x00) return 0;
                fat_read_dir_entry(sector_buf + i, cb);
            }
        }
        cluster = fat_get_next_cluster(cluster);
    }
    return 0;
}

int fat32_readdir(const char *path, fat_dir_callback cb)
{
    if(strcmp(path, "/") == 0)
    {
        return fat_read_cluster_dir(g_bpb.root_cluster, cb);
    }
    return -1;
}

bool fat32_is_dir(const char *path)
{
    return false;
}

// 基础版：读取文件内容，按簇链读取
int fat32_read_file(const char *path, uint8_t *buf, uint32_t offset, uint32_t len)
{
    // 简化实现：只支持根目录文件，后续可以完善路径解析
    // 先查找文件获取起始簇 + 文件大小
    // 这里先预留框架，当前返回-1，后面可以补全查找逻辑
    (void)path;
    (void)buf;
    (void)offset;
    (void)len;
    return -1;
}
