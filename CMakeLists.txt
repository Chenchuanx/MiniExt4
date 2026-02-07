cmake_minimum_required(VERSION 3.10)
project(MiniExt4)

# 设置C++标准（虽然内核不使用标准库，但设置编译器）
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译器和工具链
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_ASM_COMPILER as)

# 32位编译参数
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} --32")

# 内核特定的编译选项
set(KERNEL_CXX_FLAGS
    -I${CMAKE_SOURCE_DIR}/include
    -fno-use-cxa-atexit
    -nostdlib
    -fno-builtin
    -fno-rtti
    -fno-exceptions
    -fno-leading-underscore
)

# 添加所有源文件（参考Linux内核结构）
set(KERNEL_SOURCES
    init/kernel.cpp
    init/loader.s
    init/interruptstubs.s
    mm/gdt.cpp
    lib/port.cpp
    fs/super.c
    fs/inode.c
    fs/dcache.c
    fs/ext4/super.c
    fs/ext4/inode.c
    fs/ext4/io.c
    drivers/ata.cpp
    kernel/interrupts/interrupts.cpp
    kernel/sched/multitasking.cpp
    kernel/sched/tasks.cpp
    kernel/syscall.cpp
    kernel/time.cpp
    drivers/base/driver.cpp
    drivers/input/keyboard.cpp
    drivers/input/mouse.cpp
    drivers/input/handlers.cpp
    lib/console.cpp
    lib/shell.cpp
)

# 启用汇编语言支持
enable_language(ASM)
# 设置汇编文件编译规则
set(CMAKE_ASM_COMPILE_OBJECT "as --32 <SOURCE> -o <OBJECT>")
set(CMAKE_ASM_FLAGS "--32")

# 创建对象文件库（不链接）
add_library(kernel_objs OBJECT ${KERNEL_SOURCES})

# 设置编译选项
target_compile_options(kernel_objs PRIVATE ${KERNEL_CXX_FLAGS})

# 获取所有对象文件路径
set(OBJ_FILES "")
foreach(SOURCE ${KERNEL_SOURCES})
    get_filename_component(SOURCE_NAME ${SOURCE} NAME_WE)
    get_filename_component(SOURCE_DIR ${SOURCE} DIRECTORY)
    string(REPLACE "${CMAKE_SOURCE_DIR}/" "" REL_DIR ${SOURCE_DIR})
    list(APPEND OBJ_FILES "${CMAKE_BINARY_DIR}/CMakeFiles/kernel_objs.dir/${SOURCE}.o")
endforeach()

# 创建内核可执行文件（使用自定义链接命令）
add_custom_target(kernel.bin ALL
    COMMAND ld -melf_i386 -T ${CMAKE_SOURCE_DIR}/linker.ld ${OBJ_FILES} -o ${CMAKE_BINARY_DIR}/kernel.bin
    DEPENDS kernel_objs
    COMMENT "Linking kernel.bin with custom linker script"
)

# 创建GRUB配置文件脚本
file(WRITE ${CMAKE_BINARY_DIR}/create_iso.sh
"#!/bin/bash
mkdir -p iso/boot/grub
cp ${CMAKE_BINARY_DIR}/kernel.bin iso/boot/kernel.bin
echo 'set timeout=0' > iso/boot/grub/grub.cfg
echo 'set default=0' >> iso/boot/grub/grub.cfg
echo '' >> iso/boot/grub/grub.cfg
echo 'menuentry \"My operating System\" {' >> iso/boot/grub/grub.cfg
echo '  multiboot /boot/kernel.bin' >> iso/boot/grub/grub.cfg
echo '  boot' >> iso/boot/grub/grub.cfg
echo '}' >> iso/boot/grub/grub.cfg
grub-mkrescue --output=${CMAKE_BINARY_DIR}/kernel.iso iso
rm -rf iso
")

# 自定义目标：创建ISO镜像
add_custom_target(kernel.iso
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/create_iso.sh
    COMMAND bash ${CMAKE_BINARY_DIR}/create_iso.sh
    DEPENDS kernel.bin
    COMMENT "Creating kernel ISO image"
)

# 清理目标
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/kernel.bin
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/kernel.iso
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/iso
    COMMENT "Cleaning all build files"
)
