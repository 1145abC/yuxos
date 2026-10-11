#include "fat32img.h"
#include "virtio-blk.h"
#include <stdint.h>

/*
 * 从linux fs/fat/fatent.c 剥离【只读】逻辑
 * 只实现：读取FAT表项，获取next cluster
 * 不实现写、分配簇、释放簇、锁、buffer cache
 * 支持 FAT12 / FAT16 / FAT32
 */

// 复用全局变量，来自fat32.c
extern uint64_t g_fat_start_sector;
extern uint8_t sector_buf[512] __attribute__((aligned(4)));

// FAT12：一个项占12bit，存在两个字节，有可能跨512字节扇区边界
// entry: 簇号
// out_next: 输出下一个簇号
// return 0成功，‑1读磁盘失败
static int fat12_get_next(uint32_t entry, uint32_t *out_next)
{
    // FAT12: 每个项12bit；每两个项占3字节
    uint64_t byte_off = entry + (entry >> 1U);
    uint64_t sec = g_fat_start_sector + (byte_off / 512U);
    uint32_t off_in_sec = byte_off % 512U;

    if(virtio_blk_read(sec, sector_buf, 1U) != 0)
        return -1;

    uint8_t *p = sector_buf + off_in_sec;
    uint32_t val;

    if((off_in_sec + 1U) < 512U)
    {
        // 不跨扇区
        if(entry & 1U)
        {
            // 奇数项: high4 from p[1], low8 from p[2]
            val = ( (p[0] >> 4) ) | ( (uint32_t)p[1] << 4U );
        }
        else
        {
            // 偶数项: low8 p[0], high4 p[1]低4位
            val = p[0] | ( (uint32_t)(p[1] & 0x0F) << 8U );
        }
    }
    else
    {
        // 跨扇区边界：还要读下一个扇区（fat12特殊坑！原版内核fat12_ent_bread处理这个）
        uint8_t b0 = p[0];
        if(virtio_blk_read(sec + 1U, sector_buf,1UU) !=0)
            return -1;
        uint8_t b1 = sector_buf[0];
        if(entry &1U)
            val = (b0 >>4) | ((uint32_t)b1 <<4U);
        else
            val = b0 | ((uint32_t)(b1 &0x0F) <<8U);
    }

    val &= 0x0FFFU;
    if(val >= BAD_FAT12)
        val = EOF_FAT12;
    *out_next = val;
    return 0;
}

static int fat16_get_next(uint32_t entry, uint32_t *out_next)
{
    uint64_t byte_off = entry * 2U;
    uint64_t sec = g_fat_start_sector + (byte_off /512U);
    uint32_t off = byte_off % 512U;
    if(virtio_blk_read(sec, sector_buf,1U)!=0)
        return -1;
    uint16_t raw = (uint16_t)sector_buf[off] | ((uint16_t)sector_buf[off+1] <<8U);
    uint32_t val = raw;
    if(val >= BAD_FAT16)
        val = EOF_FAT16;
    *out_next = val;
    return 0;
}

static int fat32_get_next(uint32_t entry, uint32_t *out_next)
{
    uint64_t byte_off = entry *4U;
    uint64_t sec = g_fat_start_sector + (byte_off /512U);
    uint32_t off = byte_off % 512U;
    if(virtio_blk_read(sec, sector_buf, 1U)!=0)
        return -1;
    uint32_t raw = (uint32_t)sector_buf[off]
            | ((uint32_t)sector_buf[off+1]<<8U)
            | ((uint32_t)sector_buf[off+2]<<16U)
            | ((uint32_t)sector_buf[off+3]<<24U);
    uint32_t val = raw & 0x0FFFFFFFU;
    if(val >= BAD_FAT32)
        val = EOF_FAT32;
    *out_next = val;
    return 0;
}

/**
 * 统一入口获取下一个簇号
 * fat_bits: 12 /16 /32
 * entry: 当前簇
 * out_next:输出下一个簇；>=EOF_FATxx代表链结束
 * return 0成功，‑1 IO错误
 */
int fat_get_cluster_next(int fat_bits, uint32_t entry, uint32_t *out_next)
{
    switch(fat_bits)
    {
    case 12:
        return fat12_get_next(entry, out_next);
    case 16:
        return fat16_get_next(entry, out_next);
    case 32:
        return fat32_get_next(entry, out_next);
    default:
        return -1;
    }
}