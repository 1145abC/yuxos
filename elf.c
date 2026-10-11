// elf.c YuxOS ELF loader (ARM32‑ET_EXEC only)
#include "elf.h"
#include "fat32.h"
#include "uart.h"
#include "string.h"

#define PAGE_SIZE       4096U
#define ELF_PAGESTART(_v)      ((_v) & ~(PAGE_SIZE - 1U))
#define ELF_PAGEOFFSET(_v)     ((_v) &  (PAGE_SIZE - 1U))
#define ELF_PAGEALIGN(_v)      (((_v) + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U))

/*
 * 成功：返回ELF入口虚拟地址
 * 失败：返回 0
 * 限制：只支持静态 ET_EXEC；不支持 PT_INTERP / PIE
 */
Elf32_Addr elf_load(const char *path)
{
    Elf32_Ehdr ehdr;
    int ret;

    // 1. 读取ELF文件头 ✅ 顺序：path, buf, offset, len
    ret = fat32_read_file(path, (uint8_t *)&ehdr, 0U, sizeof(Elf32_Ehdr));
    if(ret != (int)sizeof(Elf32_Ehdr))
    {
        uart_puts("[ELF] read ehdr fail\n");
        return 0U;
    }

    // 魔数校验 0x7f "ELF"
    if(ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
       ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
       ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
       ehdr.e_ident[EI_MAG3] != ELFMAG3)
    {
        uart_puts("[ELF] bad magic\n");
        return 0U;
    }

    // 32位、小端、版本
    if(ehdr.e_ident[EI_CLASS] != ELFCLASS32)
    {
        uart_puts("[ELF] not ELF32\n");
        return 0U;
    }
    if(ehdr.e_ident[EI_DATA] != ELFDATA2LSB)
    {
        uart_puts("[ELF] not little‑endian\n");
        return 0U;
    }
    if(ehdr.e_ident[EI_VERSION] != EV_CURRENT)
    {
        uart_puts("[ELF] bad version\n");
        return 0U;
    }

    // ARM 架构 EM_ARM = 40
    if(ehdr.e_machine != EM_ARM)
    {
        uart_puts("[ELF] not ARM32 binary\n");
        return 0U;
    }

    // 只接受静态ET_EXEC
    if(ehdr.e_type != ET_EXEC)
    {
        uart_puts("[ELF] reject: not ET_EXEC (no PIE/dynamic)\n");
        return 0U;
    }

    // 校验程序头
    if(ehdr.e_phentsize != sizeof(Elf32_Phdr))
    {
        uart_puts("[ELF] e_phentsize mismatch\n");
        return 0U;
    }
    if(ehdr.e_phnum == 0U)
    {
        uart_puts("[ELF] no program header\n");
        return 0U;
    }

    // 2. 读取全部Program Header
    #define PHDR_BUF_MAX_COUNT 16
    if(ehdr.e_phnum > PHDR_BUF_MAX_COUNT)
    {
        uart_puts("[ELF] too many program headers\n");
        return 0U;
    }

    Elf32_Phdr phdrs[PHDR_BUF_MAX_COUNT];
    uint32_t ph_table_size = ehdr.e_phnum * sizeof(Elf32_Phdr);

    // ✅ path, buf, offset, len
    ret = fat32_read_file(path, (uint8_t *)phdrs, ehdr.e_phoff, ph_table_size);
    if(ret != (int)ph_table_size)
    {
        uart_puts("[ELF] read phdr table fail\n");
        return 0U;
    }

    // 禁止PT_INTERP动态链接
    for(unsigned int i = 0; i < ehdr.e_phnum; i++)
    {
        if(phdrs[i].p_type == PT_INTERP)
        {
            uart_puts("[ELF] reject PT_INTERP, dynamic not supported\n");
            return 0U;
        }
    }

    // 3. 遍历PT_LOAD段，加载到内存
    for(unsigned int i = 0; i < ehdr.e_phnum; i++)
    {
        Elf32_Phdr *ph = &phdrs[i];
        if(ph->p_type != PT_LOAD)
            continue;

        uart_puts("[ELF] PT_LOAD: vaddr=");
        uart_hex(ph->p_vaddr);
        uart_puts(" filesz=");
        uart_hex(ph->p_filesz);
        uart_puts(" memsz=");
        uart_hex(ph->p_memsz);
        uart_puts("\n");

        uint32_t vaddr = ph->p_vaddr;
        uint32_t poff = ELF_PAGESTART(vaddr);
        uint32_t file_off = ph->p_offset - (vaddr - poff);
        uint32_t dst_va = poff;
        uint32_t read_len = ph->p_filesz + (vaddr - poff);

        if(read_len > 0U)
        {
            // ✅ path, buf, offset, len
            ret = fat32_read_file(path, (uint8_t *)dst_va, file_off, read_len);
            if(ret != (int)read_len)
            {
                uart_puts("[ELF] PT_LOAD read error\n");
                return 0U;
            }
        }

        // BSS清零
        if(ph->p_memsz > ph->p_filesz)
        {
            uint8_t *bss_ptr = (uint8_t *)vaddr + ph->p_filesz;
            uint32_t bss_len = ph->p_memsz - ph->p_filesz;
            memset(bss_ptr, 0, bss_len);
        }
    }

    uart_puts("[ELF] load ok, entry=");
    uart_hex(ehdr.e_entry);
    uart_puts("\n");

    return ehdr.e_entry;
}
