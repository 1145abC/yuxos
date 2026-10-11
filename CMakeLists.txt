# 【第1行！！必须放在最顶部】
cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0175 OLD)

# 【第2部分：project紧跟在min_version之后】
project(YuxOS C ASM)

# ===================== 工具链配置 =====================
set(CMAKE_C_COMPILER clang)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_LINKER ld.lld)

set(TARGET_TRIPLE "armv7a-none-eabi")

# ===================== 源码列表 =====================
set(ASM_SOURCES boot.S)
set(C_SOURCES
    kernel.c
    shell.c
    uart.c
    mmu.c
    virtio-gpu.c
    string.c
    syscall.c
    fs.c
    fat32.c
    virtio-blk.c
    virtio-pci.c
    elf.c
)

# 构建ELF目标
add_executable(yuxos_qemu.elf ${ASM_SOURCES} ${C_SOURCES})

# ========== 头文件目录（小写include）==========
target_include_directories(yuxos_qemu.elf PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# ========== 【核心修复】C语言编译选项 ==========
target_compile_options(yuxos_qemu.elf PRIVATE
    --target=${TARGET_TRIPLE}
    -ffreestanding
    -nostdlib
    -mcpu=cortex-a15
    -mfpu=vfpv3
    -mfloat-abi=soft
    -marm
    -Wall
    -Wextra
    -Wno-unused-parameter
    -fno-builtin-memset
)

# 汇编单独flag
target_compile_options(yuxos_qemu.elf PRIVATE
    $<$<COMPILE_LANGUAGE:ASM>:--target=${TARGET_TRIPLE} -mcpu=cortex-a15 -mfpu=vfpv3 -mfloat-abi=soft -marm>
)

# ========== 获取 compiler-rt builtins 库路径 ==========
set(EXTRA_LINK_LIB "")
execute_process(
    COMMAND ${CMAKE_C_COMPILER} --target=${TARGET_TRIPLE} --print-libgcc-file-name
    OUTPUT_VARIABLE BUILTINS_LIB
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(EXISTS ${BUILTINS_LIB})
    message(STATUS "Found builtins lib: ${BUILTINS_LIB}")
    set(EXTRA_LINK_LIB ${BUILTINS_LIB})
else()
    set(EXTRA_LINK_LIB "")
    
endif()

# 【重点修复：增加 --target=${TARGET_TRIPLE} 给链接器】
target_link_options(yuxos_qemu.elf PRIVATE
    --target=${TARGET_TRIPLE}
    -T ${CMAKE_SOURCE_DIR}/link.ld
    -nostdlib
)
target_link_libraries(yuxos_qemu.elf PRIVATE ${EXTRA_LINK_LIB})

# ========== POST_BUILD 生成bin ==========
add_custom_command(
    TARGET yuxos_qemu.elf POST_BUILD
    COMMAND llvm-objcopy -O binary yuxos_qemu.elf yuxos_qemu.bin
    COMMENT "llvm-objcopy: elf -> yuxos_qemu.bin"
)

# mkbootimg：找到才添加命令，找不到不中断编译
find_program(MKBOOTIMG_EXE mkbootimg)
if(MKBOOTIMG_EXE)
add_custom_command(
    TARGET yuxos_qemu.elf POST_BUILD
    COMMAND ${MKBOOTIMG_EXE} --kernel yuxos_qemu.bin --kernel_offset 0x40000000 -o yuxos.img
    COMMENT "mkbootimg: build yuxos.img"
)
else()
    message(STATUS "mkbootimg not found, skip build yuxos.img")
endif()

# ===================== 自定义目标 run / run-bootimg =====================
set(QEMU_ARGS
    -M virt -cpu cortex-a15
    -serial stdio
    -device virtio-gpu-pci
    -device virtio-blk-pci,drive=disk0
    -drive id=disk0,file=disk.img,if=none,format=raw
    -display vnc=:0
)

add_custom_target(run
    COMMAND qemu-system-arm ${QEMU_ARGS} -kernel $<TARGET_FILE:yuxos_qemu.elf>
    DEPENDS yuxos_qemu.elf
    USES_TERMINAL
)

if(MKBOOTIMG_EXE)
add_custom_target(run-bootimg
    COMMAND qemu-system-arm ${QEMU_ARGS} -kernel ${CMAKE_BINARY_DIR}/yuxos.img
    DEPENDS yuxos_qemu.elf
    USES_TERMINAL
)
endif()

add_custom_target(all_targets DEPENDS yuxos_qemu.elf)
add_custom_target(bootimg DEPENDS yuxos_qemu.elf)
