// fs.h
#ifndef FS_H
#define FS_H
#include <stdint.h>

#define FS_NODE_DIR  0
#define FS_NODE_FILE 1

typedef struct fs_node {
    char name[16];
    uint8_t type;
    uint32_t size;
    struct fs_node *parent;
    struct fs_node *child;
    struct fs_node *next;
} fs_node_t;

void fs_init(void);
fs_node_t* fs_get_cwd(void);
fs_node_t* fs_find_child(fs_node_t *dir, const char *name);
void fs_ls(void);
void fs_cd(const char *target);
void fs_pwd(fs_node_t *node);

#endif
