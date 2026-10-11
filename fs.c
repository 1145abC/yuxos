// fs.c 【API兼容旧版本，底层读取FAT32磁盘】
#include <stdint.h>
#include <stddef.h>
#include "fs.h"
#include "uart.h"
#include "fat32.h"

// 简易字符串比较
int strcmp(const char *a, const char *b);

// 当前工作目录节点
static fs_node_t *fs_cwd;

// 目录项缓存：FAT32读出的目录项临时存这里，链表挂载到fs_node->child
#define MAX_DIR_ENTRY 16
static fs_node_t dir_cache[MAX_DIR_ENTRY];
static int entry_count;

// 回调函数改名，避开typedef重名
static void fat_dir_cb(const char *name, bool is_dir, uint32_t size)
{
    if(entry_count >= MAX_DIR_ENTRY)
        return;

    fs_node_t *ent = &dir_cache[entry_count];
    // 直接拷贝文件名到 ent->name 数组
    int i = 0;
    while(name[i] && i < 15)
    {
        ent->name[i] = name[i];
        i++;
    }
    ent->name[i] = '\0';

    ent->type = is_dir ? FS_NODE_DIR : FS_NODE_FILE;
    ent->size = size;
    ent->parent = fs_cwd;
    ent->child = NULL;
    ent->next = NULL;

    // 链表串联
    if(entry_count > 0)
    {
        dir_cache[entry_count - 1].next = ent;
    }
    entry_count++;
}

// 从磁盘加载当前目录，刷新缓存
static void load_dir_from_disk(const char *path)
{
    entry_count = 0;
    fs_cwd->child = NULL;
    // 传入回调函数指针 fat_dir_cb
    fat32_readdir(path, fat_dir_cb);
    if(entry_count > 0)
    {
        fs_cwd->child = &dir_cache[0];
    }
}

// 初始化文件系统：挂载FAT32磁盘，创建根节点
void fs_init(void)
{
    uart_puts("FS: mount FAT32 disk...\n");
    fat32_mount();

    // 根目录节点
    static fs_node_t root_node = {
        .name = "/",
        .type = FS_NODE_DIR,
        .parent = NULL,
        .child = NULL,
        .next = NULL,
        .size = 0
    };
    fs_cwd = &root_node;

    // 加载根目录内容
    load_dir_from_disk("/");
    uart_puts("FS: disk filesystem ready.\n");
}

// 获取当前工作目录节点
fs_node_t* fs_get_cwd(void)
{
    return fs_cwd;
}

// 在dir目录查找子节点
fs_node_t* fs_find_child(fs_node_t *dir, const char *name)
{
    if(dir == NULL || dir->type != FS_NODE_DIR)
        return NULL;

    fs_cwd = dir;
    load_dir_from_disk(dir->name);

    fs_node_t *p = dir->child;
    while(p != NULL)
    {
        if(strcmp(p->name, name) == 0)
        {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

// ls 列出当前目录
void fs_ls(void)
{
    fs_node_t *cwd = fs_get_cwd();
    if(cwd->type != FS_NODE_DIR)
    {
        uart_puts("ls: not a directory\n");
        return;
    }
    load_dir_from_disk(cwd->name);

    fs_node_t *p = cwd->child;
    if(!p)
    {
        uart_puts("(empty)\n");
        return;
    }
    while(p != NULL)
    {
        if(p->type == FS_NODE_DIR)
        {
            uart_puts("[DIR]  ");
        }
        else
        {
            uart_puts("[FILE] ");
        }
        uart_puts(p->name);
        uart_puts("\n");
        p = p->next;
    }
}

// cd 切换目录
void fs_cd(const char *target)
{
    fs_node_t *cwd = fs_get_cwd();
    if(strcmp(target, "..") == 0)
    {
        if(cwd->parent != NULL)
        {
            fs_cwd = cwd->parent;
            load_dir_from_disk(fs_cwd->name);
        }
        return;
    }
    if(strcmp(target, "/") == 0)
    {
        // 回到根
        while(fs_cwd->parent != NULL)
        {
            fs_cwd = fs_cwd->parent;
        }
        load_dir_from_disk("/");
        return;
    }
    fs_node_t *node = fs_find_child(cwd, target);
    if(node == NULL)
    {
        uart_puts("cd: no such file or directory: ");
        uart_puts(target);
        uart_putc('\n');
        return;
    }
    if(node->type != FS_NODE_DIR)
    {
        uart_puts("cd: not a directory: ");
        uart_puts(target);
        uart_putc('\n');
        return;
    }
    fs_cwd = node;
    load_dir_from_disk(node->name);
}

// pwd递归打印路径（完全沿用你原来代码，不用修改）
void fs_pwd(fs_node_t *node)
{
    if(node == NULL)
    {
        uart_puts("(null)");
        return;
    }
    if (node->parent == NULL)
    {
        // 根节点，直接return，不输出/
        return;
    }
    fs_pwd(node->parent);
    uart_puts(node->name);
    uart_puts("/");
}
