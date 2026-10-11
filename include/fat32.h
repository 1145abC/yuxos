#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stdbool.h>

// 目录项回调：遍历目录的时候，每读到一个文件/文件夹就调用这个
// name: 长文件名 / 8.3文件名；is_dir=true代表目录；size为文件字节大小
typedef void (*fat_dir_callback)(const char *name, bool is_dir, uint32_t size);

// 挂载FAT32分区：读取BPB引导块，解析分区参数
int fat32_mount(void);

// 读取指定路径的目录，遍历所有条目，用callback输出
// path: "/", "/bin" 这种路径
int fat32_readdir(const char *path, fat_dir_callback cb);

// 判断一个路径是否存在，并且是不是文件夹
bool fat32_is_dir(const char *path);

// 读取文件内容
// path: 文件路径；buf:输出缓冲区；offset:文件内偏移；len:读取字节长度
int fat32_read_file(const char *path, uint8_t *buf, uint32_t offset, uint32_t len);

#endif
