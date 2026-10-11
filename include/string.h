#ifndef STRING_H
#define STRING_H

#include <stdint.h>

// 手动定义 size_t
typedef uint32_t size_t;

void *memset(void *dest, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

// 新增：字符串比较、字符串长度
int strcmp(const char *a, const char *b);
size_t strlen(const char *s);

char *itoa(int num, char *buf, int base);
char *uitoa(uint32_t num, char *buf, int base);

#endif
