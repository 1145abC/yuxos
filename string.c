#include <stdint.h>
#include <string.h>

// 裸机字符串比较，替代libc strcmp
 int strcmp(const char *a, const char *b)
{
    while(*a != '\0' && *b != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return (uint8_t)*a - (uint8_t)*b;
}

 size_t strlen(const char *s)
{
    size_t len = 0;
    while(s[len]) len++;
    return len;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for(size_t i = 0; i < n; i++)
    {
        d[i] = s[i];
    }
    return dest;
}

// ARM EABI 内存清零，解决 __aeabi_memclr8 未定义
void __aeabi_memclr8(void *dest, uint32_t n)
{
    uint8_t *p = (uint8_t *)dest;
    for(uint32_t i = 0; i < n; i++){
        p[i] = 0;
    }
}



// 内存填充
void *memset(void *dest, int c, uint32_t n)
{
    uint8_t *p = (uint8_t *)dest;
    for(uint32_t i = 0; i < n; i++)
    {
        p[i] = (uint8_t)c;
    }
    return dest;
}



// 实现 memset


// 实现 memcpy


// 实现 itoa：数字转字符串
// num:待转数字，buf输出缓冲区，base进制(10/16)
char *itoa(int num, char *buf, int base)
{
    char *p = buf;
    char *p1 = buf;
    uint32_t n;
    if(num < 0 && base == 10)
    {
        *p++ = '-';
        n = (uint32_t)(-num);
    }
    else
    {
        n = (uint32_t)num;
    }

    do
    {
        uint32_t rem = n % base;
        if(rem < 10)
            *p++ = rem + '0';
        else
            *p++ = rem - 10 + 'a';
        n = n / base;
    }while(n != 0);

    *p = 0;
    // 反转字符串
    p1 = buf;
    if(num < 0 && base ==10)
        p1++;
    p--;
    while(p1 < p)
    {
        char t = *p1;
        *p1 = *p;
        *p = t;
        p1++;
        p--;
    }
    return buf;
}

// ARM EABI 兼容符号，解决 __aeabi_memcpy 未定义
void __aeabi_memcpy(void *dest, const void *src, size_t n)
{
    memcpy(dest, src, n);
}