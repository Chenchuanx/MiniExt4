# MiniExt4

MiniExt4 是一个运行在 x86 32 位平台上的教学型操作系统内核，参考 Linux 内核结构实现。项目采用 C/C++ 编写，通过 GRUB 引导，在 QEMU 中运行。

内核实现了 GDT、中断与多任务调度、ATA 块设备驱动、帧缓冲控制台与键盘输入，并在此基础上构建了 VFS 抽象层及 ext4 文件系统（含 inode/目录/块分配、extent、目录哈希索引等）。用户可通过内置 Shell 执行 `ls`、`cd`、`mkdir`、`touch`、`cat`、`rm`、`find`、`dumpe2fs` 等命令，对磁盘进行读写与测试。

更详细的设计说明见 `docs/` 目录。

## 项目结构

```
MiniExt4/
├── init/              # 内核入口与启动（kernelMain、引导加载、中断桩）
├── mm/                # 内存管理（GDT）
├── kernel/            # 内核核心
│   ├── interrupts/    # 中断描述符与分发
│   ├── sched/         # 多任务调度
│   ├── shell.cpp      # 命令行 Shell
│   ├── cmds.cpp       # Shell 命令实现
│   └── syscall.cpp    # 系统调用
├── block/             # 块设备子系统
├── drivers/           # 设备驱动
│   ├── ata/           # ATA 磁盘
│   ├── input/         # 键盘、鼠标
│   ├── video/         # 帧缓冲控制台与字体
│   ├── base/          # 驱动框架
│   ├── pit.cpp        # 定时器
│   └── rtc.cpp        # 实时时钟
├── fs/                # 文件系统
│   └── ext4/          # ext4 文件系统实现
├── lib/               # 内核基础库（printf、字符串、内存、时间等）
├── include/           # 头文件（linux/、fs/、drivers/ 等，与源码目录对应）
├── docs/              # 设计文档
├── scripts/           # 辅助脚本（如创建测试镜像）
├── build.sh           # 一键编译脚本
├── CMakeLists.txt     # 构建配置
├── linker.ld          # 内核链接脚本
└── unifont-16.0.04.bmp  # Unifont 字体资源（CJK 显示）
```

## 编译
    ./build.sh

## 运行
    dd if=/dev/zero of=ext4_disk.img bs=1M count=64 2>/dev/null && qemu-system-i386 -cdrom build/kernel.iso -hda ext4_disk.img -boot d -m 512M -serial stdio -display gtk

## 镜像
    dd if=/dev/zero of=ext4_disk.img bs=1M count=1024 && mkfs.ext4 -F -b 4096 ext4_disk.img

## 挂载
    sudo mount -o loop ext4_disk.img ./mnt

## 可能问题
    CMAKE_BUILD_TYPE:STRING=Release,Release 模式会给编译器加上 -O3 -DNDEBUG。在这个优化级别下，GCC 11 会拒绝原来的内联汇编写法

## 编译依赖

- gcc / g++ 11.4.0（x86_64 主机需支持 `-m32`，如 `gcc-multilib`）
- cmake 3.22.1
- binutils（as / ld / objcopy）2.38
- make 4.3
- grub-mkrescue 2.06

## 运行依赖

- qemu-system-i386 6.2.0
- dd (coreutils) 8.32
