# MiniExt4 目录结构说明

## 📋 目录重组概述

本次重组将项目从**分离式结构**（include/ 和 src/ 分离）改为**Linux内核风格**的**统一结构**，使项目更符合操作系统内核的开发习惯。

---

```
MiniExt4/
├── init/                # 初始化代码
├── kernel/              # 核心内核代码
├── mm/                  # 内存管理
├── drivers/             # 设备驱动
├── lib/                 # 库函数
├── fs/                  # 文件系统 ⭐
└── include/             # 头文件（统一管理）
```

---

## 📁 各目录详细说明

### 1. `init/` - 初始化代码

**作用：** 系统启动和初始化相关代码

**文件：**
- `loader.s` - 引导程序（Multiboot规范）
- `kernel.cpp` - 内核主函数（系统入口）
- `interruptstubs.s` - 中断处理程序汇编存根

**对应Linux内核：** `linux/init/`

**说明：** 这是系统启动的第一批代码，负责初始化基本环境并跳转到内核主函数。

---

### 2. `kernel/` - 核心内核代码

**作用：** 内核的核心功能模块

**子目录和文件：**
```
kernel/
├── interrupts/          # 中断管理
│   └── interrupts.cpp   # 中断描述符表、中断处理
├── sched/               # 任务调度（原multitasking）
│   ├── multitasking.cpp # 任务管理器
│   └── tasks.cpp       # 任务实现
├── syscall.cpp          # 系统调用处理
├── kcli.cpp             # Kernel命令行交互
└── time.cpp            # 时间服务
```

**对应Linux内核：** `linux/kernel/`

**说明：**
- **interrupts/** - 管理硬件中断和软件中断（系统调用）
- **sched/** - 多任务调度系统（当前框架已实现但未激活）
- **syscall.cpp** - 处理用户空间到内核空间的系统调用
- **time.cpp** - 从CMOS RTC读取系统时间

---

### 3. `mm/` - 内存管理

**作用：** 内存管理相关代码

**文件：**
- `gdt.cpp` - 全局描述符表（GDT）实现

**对应Linux内核：** `linux/mm/`

**说明：** 
- GDT是x86架构的内存分段机制
- 定义代码段和数据段的访问权限和内存范围
- 这是内存管理的基础

---

### 4. `drivers/` - 设备驱动

**作用：** 硬件设备驱动程序

**子目录：**
```
drivers/
├── base/                # 驱动框架
│   └── driver.cpp      # 驱动管理器（统一管理所有驱动）
└── input/               # 输入设备驱动
    ├── keyboard.cpp     # PS/2键盘驱动
    ├── mouse.cpp        # PS/2鼠标驱动
    └── handlers.cpp     # 输入事件处理器
```

**对应Linux内核：** `linux/drivers/`

**说明：**
- **base/** - 驱动框架，提供统一的驱动注册和管理接口
- **input/** - 输入设备驱动，处理键盘和鼠标输入
- 采用分层设计，便于扩展新设备

---

### 5. `lib/` - 库函数

**作用：** 内核使用的通用库函数

**文件：**
- `port.cpp` - I/O端口操作（8位、16位、32位端口读写）
- `printf.cpp` - VGA文本模式控制台输出

**对应Linux内核：** `linux/lib/`

**说明：**
- 这些是内核的基础工具函数
- 不依赖其他复杂模块，可被多个模块使用
- `port.cpp` 提供硬件I/O访问能力
- `console.cpp` 提供文本输出能力

---

### 6. `fs/` - 文件系统 ⭐

**作用：** 文件系统实现（项目核心）

**当前状态：** 目录已创建，等待Ext4实现

**对应Linux内核：** `linux/fs/`

**说明：**
- 这是项目的**核心模块**
- 将实现Ext4文件系统的模拟
- 包括超级块、inode、目录、块管理等

**未来结构（建议）：**
```
fs/
└── ext4/
    ├── superblock.c    # 超级块操作
    ├── inode.c         # inode操作
    ├── directory.c     # 目录操作
    ├── block.c         # 块管理
    └── io.c            # 磁盘I/O
```

---

### 7. `include/` - 头文件

**作用：** 所有头文件统一管理

**结构：**
```
include/
├── linux/              # Linux风格内核头文件
│   └── types.h         # 基础类型定义（int8_t, uint32_t等）
├── kernel/             # 核心内核头文件
│   ├── interrupts.h    # 中断管理
│   ├── multitasking.h  # 任务调度
│   ├── tasks.h         # 任务定义
│   ├── syscalls.h      # 系统调用
│   ├── kcli.h          # Kernel命令行
│   └── time.h          # 时间服务
├── mm/                 # 内存管理头文件
│   └── gdt.h           # 全局描述符表
├── drivers/            # 驱动头文件
│   ├── driver.h        # 驱动框架
│   ├── keyboard.h      # 键盘驱动
│   ├── mouse.h         # 鼠标驱动
│   └── handlers.h      # 事件处理
└── lib/                # 库函数头文件
    ├── port.h          # I/O端口
    └── printf.h       # 控制台
```

**对应Linux内核：** `linux/include/`

**说明：**
- 头文件按模块组织，与源文件目录对应
- `include/linux/` 存放通用的内核头文件
- 其他目录的头文件与对应的源文件目录结构一致

---

## 🎯 重组的核心思想

### **模块化组织**
每个目录代表一个功能模块，职责清晰：
- `init/` - 启动
- `kernel/` - 核心功能
- `mm/` - 内存
- `drivers/` - 驱动
- `fs/` - 文件系统
- `lib/` - 工具库
---

## 🔗 模块依赖关系

```
init/
  └─> kernel/ (中断、调度)
      └─> mm/ (内存管理)
          └─> drivers/ (设备驱动)
              └─> lib/ (库函数)
                  └─> fs/ (文件系统) ⭐
```

**依赖说明：**
1. `init/` 初始化所有模块
2. `kernel/` 提供核心功能（中断、调度）
3. `mm/` 提供内存管理基础
4. `drivers/` 使用 `lib/` 的I/O功能
5. `fs/` 是最高层，依赖所有底层模块
