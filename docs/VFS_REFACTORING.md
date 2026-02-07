# VFS 层重构说明

## 📋 重构目标

参考 Linux 内核源码（https://github.com/torvalds/linux），重构 VFS 层，使其成为真正的抽象层，便于在实现 ext4 后可以轻松添加其他文件系统（如 ext2、fat32 等）。

## 🔧 主要改动

### 1. VFS Dentry 缓存管理 (`fs/dcache.cpp`)

**新增功能：**
- `d_alloc()` - 统一分配 dentry，替代各文件系统自己的实现
- `d_lookup()` - 在父目录中查找 dentry
- `dget()` / `dput()` - dentry 引用计数管理
- `d_add()` / `d_instantiate()` - 关联 dentry 和 inode
- `d_hash_string()` - 字符串哈希计算
- `qstr_init()` - 初始化 qstr 结构

**设计理念：**
- 所有文件系统共享统一的 dentry 池
- 文件系统不需要自己管理 dentry 分配
- 参考 Linux 内核 `fs/dcache.c` 的设计

### 2. VFS 挂载接口 (`fs/super.c`)

**新增功能：**
- `get_sb_bdev()` - 从块设备获取超级块
- `mount_bdev()` - 挂载块设备文件系统的统一接口
- `deactivate_super()` - 停用超级块

**设计理念：**
- 文件系统只需要实现 `fill_super()` 函数
- VFS 层负责创建 super_block、调用 fill_super、管理超级块链表
- 文件系统在 `fill_super()` 中创建根 dentry 并设置到 `sb->s_root`

### 3. Ext4 重构 (`fs/ext4/super.cpp`)

**改动：**
- 移除了 `alloc_dentry()` 函数（由 VFS 统一管理）
- `ext4_mount()` 现在使用 `mount_bdev()` 统一接口
- `ext4_fill_super()` 在最后创建根 dentry 并设置到 `sb->s_root`

**之前的问题：**
```cpp
// ❌ 之前：ext4 自己管理 dentry
static struct dentry *alloc_dentry(void) { ... }
root_dentry = alloc_dentry();
```

**现在的设计：**
```cpp
// ✅ 现在：使用 VFS 统一接口
struct dentry *root_dentry = d_alloc(nullptr, &root_name);
d_instantiate(root_dentry, root_inode);
sb->s_root = root_dentry;
```

### 4. 头文件更新

**`include/linux/fs.h`：**
- 添加了所有新的 VFS 函数声明
- 提供统一的 VFS 接口

**`include/fs/fs_types.h`：**
- 添加了文件系统标志定义（`FS_REQUIRES_DEV` 等）

## 📐 架构设计

### 文件系统实现流程

```
用户调用 mount()
    ↓
VFS: mount_bdev()
    ↓
VFS: get_sb_bdev()
    ↓
VFS: 创建 super_block
    ↓
文件系统: fill_super()  ← 文件系统只需要实现这个
    ├─ 读取磁盘 superblock
    ├─ 填充 sb->s_op, sb->s_fs_info
    └─ 创建根 dentry，设置 sb->s_root
    ↓
VFS: 返回 sb->s_root
```

### 添加新文件系统的步骤

以添加 ext2 为例：

1. **实现文件系统类型：**
```cpp
struct file_system_type ext2_fs_type = {
    "ext2",
    FS_REQUIRES_DEV,
    ext2_mount,
    ext2_kill_sb,
    ...
};
```

2. **实现 fill_super：**
```cpp
static int ext2_fill_super(struct super_block *sb, void *data)
{
    // 读取 ext2 superblock
    // 填充 sb->s_op, sb->s_fs_info
    // 创建根 dentry
    struct qstr root_name;
    qstr_init(&root_name, "/", 1);
    struct dentry *root = d_alloc(nullptr, &root_name);
    struct inode *root_inode = ext2_iget(sb, 2);
    d_instantiate(root, root_inode);
    sb->s_root = root;
    return 0;
}
```

3. **实现 mount：**
```cpp
static struct dentry *ext2_mount(...)
{
    return mount_bdev(fs_type, flags, dev_name, data, ext2_fill_super);
}
```

4. **注册文件系统：**
```cpp
register_filesystem(&ext2_fs_type);
```

## ✅ 优势

1. **统一接口**：所有文件系统通过相同的 VFS 接口工作
2. **代码复用**：dentry 管理、挂载逻辑等由 VFS 统一处理
3. **易于扩展**：添加新文件系统只需实现操作函数表
4. **符合 Linux 设计**：参考 Linux 内核的 VFS 架构

## 🔄 后续工作

- [ ] 实现 inode_operations（lookup, create, mkdir 等）
- [ ] 实现 file_operations（read, write, open 等）
- [ ] 实现路径解析（namei）
- [ ] 实现文件描述符管理
- [ ] 添加其他文件系统（ext2, fat32 等）作为示例

## 📚 参考

- Linux 内核源码：https://github.com/torvalds/linux
- `fs/dcache.c` - Dentry 缓存管理
- `fs/super.c` - 超级块和挂载管理
- `include/linux/fs.h` - VFS 核心定义

