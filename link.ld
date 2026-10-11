/* link_qemu.ld YuxOS ARMv7 QEMU virt 32bit */
ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    /* 强制boot代码放在最开头，只放.text.boot，不要混入普通.text */
    .text.boot : {
        *(.text.boot)
    }
    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
    }
    __bss_end = .;

    . = ALIGN(4096);
    . = . + 0x40000;
    __stack_top = .;
}
