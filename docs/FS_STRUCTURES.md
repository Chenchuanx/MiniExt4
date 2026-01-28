# 文件系统基础结构说明

## 📋 概述

本文档说明为 MiniExt4 项目创建的文件系统基础结构，这些结构为后续实现 Ext4 文件系统提供了必要的基础。

---

## 📁 已创建的文件

### 1. `include/fs/inode.h` - Inode 结构

**核心结构：**
- `struct inode` - 索引节点，表示文件或目录
- `struct inode_operations` - inode 操作函数表

**主要字段：**
- `i_ino` - inode 编号
- `i_mode` - 文件类型和权限
- `i_size` - 文件大小
- `i_blocks` - 分配的块数
- `i_atime/i_mtime/i_ctime` - 时间戳
- `i_sb` - 所属超级块
- `i_private` - 文件系统私有数据（Ext4 特定）

**操作函数：**
- `create()` - 创建文件
- `lookup()` - 查找目录项
- `mkdir()` - 创建目录
- `unlink()` - 删除文件
- `rename()` - 重命名
- `getattr()` / `setattr()` - 获取/设置属性

**文件类型和权限：**
- 定义了完整的文件类型宏（S_IFREG, S_IFDIR 等）
- 定义了权限位宏（S_IRWXU, S_IRGRP 等）
- 提供了文件类型检查宏（S_ISREG, S_ISDIR 等）

---

### 2. `include/fs/dentry.h` - Dentry 结构

**核心结构：**
- `struct dentry` - 目录项缓存
- `struct dentry_operations` - dentry 操作函数表
- `struct qstr` - 快速字符串结构

**主要字段：**
- `d_name` - 名称（qstr 结构）
- `d_inode` - 关联的 inode
- `d_parent` - 父目录 dentry
- `d_child` - 父目录子项链表
- `d_subdirs` - 子目录链表
- `d_sb` - 所属超级块
- `d_fsdata` - 文件系统私有数据

**操作函数：**
- `d_revalidate()` - 验证 dentry 有效性
- `d_compare()` - 比较两个 dentry
- `d_delete()` - 删除 dentry
- `d_release()` - 释放 dentry
- `d_hash()` - 生成哈希值

**优化特性：**
- `d_iname` - 短名称内联存储（32 字节），避免额外内存分配

---

### 3. `include/fs/fs_types.h` - 文件系统类型

**核心结构：**
- `struct file_system_type` - 文件系统类型描述
- `struct block_device` - 块设备结构

**file_system_type 字段：**
- `name` - 文件系统名称（如 "ext4"）
- `mount()` - 挂载函数
- `kill_sb()` - 卸载函数
- `fs_supers` - 超级块链表

**block_device 字段：**
- `bd_dev` - 设备号
- `bd_size` - 设备大小
- `bd_block_size` - 块大小
- `bd_super` - 关联的超级块

---

### 4. `include/fs/stat.h` - 统计和属性

**核心结构：**
- `struct kstatfs` - 文件系统统计信息
- `struct kstat` - 文件统计信息
- `struct iattr` - inode 属性

**kstatfs 字段（用于 statfs）：**
- `f_blocks` - 总块数
- `f_bfree` - 空闲块数
- `f_bavail` - 可用块数
- `f_files` - 总 inode 数
- `f_ffree` - 空闲 inode 数
- `f_namelen` - 最大文件名长度

**kstat 字段（用于 stat）：**
- `ino` - inode 编号
- `mode` - 文件类型和权限
- `size` - 文件大小
- `uid/gid` - 用户/组 ID
- 时间戳（atime, mtime, ctime）

**iattr 字段（用于设置属性）：**
- `ia_mode` - 文件类型和权限
- `ia_uid/ia_gid` - 用户/组 ID
- `ia_size` - 文件大小
- 时间戳字段

---

### 5. `include/fs/writeback.h` - 回写控制

**核心结构：**
- `struct writeback_control` - 回写控制结构

**主要字段：**
- `nr_to_write` - 要写入的页数
- `sync_mode` - 同步模式（异步/同步）
- `for_kupdate` - 内核更新标志
- `for_background` - 后台回写标志
- `for_sync` - 同步回写标志

**同步模式：**
- `WB_SYNC_NONE` - 异步回写
- `WB_SYNC_ALL` - 同步回写所有数据

---

## 🔗 结构关系图

```
super_block
  ├── s_root (dentry *)          ──┐
  ├── s_op (super_operations *)      │
  └── s_fs_info (void *)            │
                                    │
dentry                               │
  ├── d_inode (inode *) ────────────┼──┐
  ├── d_parent (dentry *)           │  │
  ├── d_child (list_head)           │  │
  └── d_subdirs (list_head)         │  │
                                    │  │
inode                                │  │
  ├── i_sb (super_block *) ─────────┘  │
  ├── i_op (inode_operations *)        │
  ├── i_size (loff_t)                  │
  ├── i_blocks (unsigned long)         │
  └── i_private (void *) ──────────────┘
                                      │
file_system_type                      │
  ├── mount()                         │
  └── kill_sb()                       │
                                      │
block_device                          │
  ├── bd_super (super_block *) ───────┘
  └── bd_size (loff_t)
```

---

## 📊 简化说明

### 已移除的高级特性

1. **RCU 机制** - 简化引用计数，使用普通 int
2. **复杂锁机制** - 移除 percpu_rwsem 等复杂同步原语
3. **页缓存** - address_space 仅保留前向声明
4. **扩展属性** - 移除 xattr 相关结构
5. **ACL 支持** - 移除 posix_acl 相关结构
6. **安全模块** - 移除 security 相关字段

### 保留的核心功能

1. **基本文件操作** - create, lookup, mkdir, unlink, rename
2. **属性管理** - getattr, setattr
3. **目录结构** - dentry 树形结构
4. **时间戳** - atime, mtime, ctime
5. **权限管理** - mode, uid, gid
6. **统计信息** - kstatfs, kstat

---

## 🎯 使用示例

### 1. 创建 inode

```c
struct inode *inode = sb->s_op->alloc_inode(sb);
inode->i_ino = 12345;
inode->i_mode = S_IFREG | S_IRUSR | S_IWUSR;
inode->i_size = 1024;
inode->i_sb = sb;
```

### 2. 创建 dentry

```c
struct dentry *dentry;
struct qstr name = {
    .name = "test.txt",
    .len = 8,
    .hash = hash_string("test.txt")
};
dentry->d_name = name;
dentry->d_inode = inode;
dentry->d_parent = parent_dentry;
```

### 3. 查找目录项

```c
struct dentry *dentry = dir->i_op->lookup(dir, child_dentry, 0);
if (dentry && dentry->d_inode) {
    // 找到文件
}
```

---
---
