# 本项目实际使用的数据结构与字段

本文档列出 MiniExt4 在**运行时和 mkfs 时真正读写、参与逻辑**的数据，与 `docs/UNUSED_STRUCTURES_SIMPLE_EXT4.md` 互补。

---

## 一、磁盘超级块 `struct ext4_super_block` 中使用的字段

### 挂载时读取（ext4_fill_super）

| 字段 | 用途 |
|------|------|
| `s_magic` | 判断是否为 Ext4，决定是否执行 mkfs |
| `s_log_block_size` | 计算块大小 `1024 << s_log_block_size`，并填入 `sbi->s_log_block_size`、`sb->s_blocksize_bits` |
| `s_blocks_per_group` | 填入 `sbi->s_blocks_per_group`，用于块组计算、块分配 |
| `s_inodes_per_group` | 填入 `sbi->s_inodes_per_group`，用于 inode 定位与分配 |
| `s_inodes_count` | 填入 `sbi->s_inodes_count` |
| `s_blocks_count_lo` | 填入 `sbi->s_blocks_count`，用于块组数等计算 |
| `s_first_data_block` | 填入 `sbi->s_first_data_block`，用于组描述符块号、布局计算 |
| `s_uuid` | 复制到 VFS `sb->s_uuid` |

### mkfs 时写入（ext4_mkfs）

除上述外，mkfs 还会写入以下字段以符合磁盘布局（其中部分仅被工具/兼容性使用，运行时不再读）：

- **布局与容量**：`s_inodes_count`, `s_blocks_count_lo`, `s_r_blocks_count_lo`, `s_free_blocks_count_lo`, `s_free_inodes_count`, `s_first_data_block`, `s_log_block_size`, `s_log_cluster_size`, `s_blocks_per_group`, `s_clusters_per_group`, `s_inodes_per_group`
- **时间与状态**：`s_mtime`, `s_wtime`, `s_mnt_count`, `s_max_mnt_count`, `s_magic`, `s_state`, `s_errors`, `s_lastcheck`, `s_checkinterval`, `s_creator_os`, `s_rev_level`, `s_mkfs_time`
- **Inode/特性**：`s_first_ino`, `s_inode_size`, `s_block_group_nr`, `s_feature_compat/incompat/ro_compat`, `s_def_resuid`, `s_def_resgid`, `s_desc_size`
- **日志等（置 0）**：`s_journal_inum`, `s_journal_dev`, `s_last_orphan`, `s_hash_seed`, `s_def_hash_version`, `s_jnl_backup_type`
- **大容量（置 0）**：`s_blocks_count_hi`, `s_r_blocks_count_hi`, `s_free_blocks_count_hi`
- **标识**：`s_uuid`, `s_volume_name`, `s_last_mounted`（清零）

**小结**：运行时逻辑**只依赖**上面“挂载时读取”的 8 个字段；其余在 mkfs 中写入是为了布局与工具兼容。

---

## 二、块组描述符 `struct ext4_group_desc` 中使用的字段

### 挂载时读取

- 整块通过 `memcpy` 读入 `sbi->s_group_desc`，后续在内存中读写该副本。

### 运行时读写（balloc / ialloc / super）

| 字段 | 用途 |
|------|------|
| `bg_block_bitmap_lo` | 块位图块号，用于读写信块位图（balloc） |
| `bg_inode_bitmap_lo` | Inode 位图块号，用于读写 inode 位图（ialloc） |
| `bg_inode_table_lo` | Inode 表起始块号，用于 `ext4_iget`、`ext4_write_inode` 定位 inode 块 |
| `bg_free_blocks_count_lo` | 空闲块数，分配时减 1，释放时加 1（balloc） |
| `bg_free_inodes_count_lo` | 空闲 inode 数，分配时减 1，释放时加 1（ialloc） |

### mkfs 时写入（未在运行时读取）

- `bg_used_dirs_count_lo`（置 1）、`bg_flags`、`bg_itable_unused_lo` 等仅 mkfs 写入，运行时未使用；校验和、`*_hi` 等仅置 0 保留布局。

**小结**：运行时**真正参与逻辑**的是上面 5 个字段；整块描述符会写回磁盘以保持一致性。

---

## 三、磁盘 Inode `struct ext4_inode` 中使用的字段

### 读（ext4_iget）

| 字段 | 用途 |
|------|------|
| `i_mode` | 填入 VFS `inode->i_mode`，用于判断文件类型、设置 i_op/i_fop |
| `i_size_lo`, `i_size_high` | 合并为 `inode->i_size` |
| `i_blocks_lo`, `i_blocks_hi` | 合并为 `inode->i_blocks` |
| `i_atime`, `i_mtime`, `i_ctime` | 填入 VFS 时间戳 |
| `i_uid`, `i_uid_high`, `i_gid`, `i_gid_high` | 合并为 `inode->i_uid`, `inode->i_gid` |
| `i_links_count` | 填入 `inode->i_nlink` |
| `i_block[15]` | 复制到 `ext4_inode_info->i_block[]`，用于数据块定位 |
| `i_flags` | 填入 `ext4_inode_info->i_flags` |

### 写（ext4_write_inode）

与上面一一对应地写回：`i_mode`, `i_size_lo/high`, `i_blocks_lo/hi`, `i_atime/mtime/ctime`, `i_uid/uid_high`, `i_gid/gid_high`, `i_links_count`, `i_block[]`, `i_flags`。

**小结**：磁盘 inode 中**参与逻辑**的即上述字段；`i_dtime`, `i_osd1`, `i_generation`, `i_file_acl_*` 等仅在 mkfs 时置 0，运行时不读不写。

---

## 四、目录项 `struct ext4_dir_entry` 中使用的字段

| 字段 | 用途 |
|------|------|
| `inode` | 目录项对应的 inode 号（查找、添加、删除、遍历） |
| `rec_len` | 记录长度，用于遍历、插入、删除时维护目录块布局 |
| `name_len` | 文件名长度（本项目中为 1 字节 `__u8`） |
| `file_type` | 文件类型（如 DT_DIR、DT_UNKNOWN） |
| `name[]` | 文件名内容，用于比较与拷贝 |

**小结**：目录项结构在 `ext4_find_entry`、`ext4_add_entry`、`ext4_remove_entry`、`ext4_dir_foreach` 中全程使用。

---

## 五、内存结构：实际用到的字段

### 5.1 `struct ext4_sb_info`（全部使用）

| 字段 | 用途 |
|------|------|
| `s_blocks_per_group` | 块组大小、块分配范围 |
| `s_inodes_per_group` | inode 所在块组与块内偏移计算 |
| `s_inodes_count` | inode 总数 |
| `s_blocks_count` | 块总数、块组数计算 |
| `s_first_data_block` | 组描述符块号等布局计算 |
| `s_log_block_size` | 块大小对数 |
| `s_block_size` | 块大小（字节），读写块、inode 表、位图 |
| `s_groups_count` | 块组数（当前简化版主要用 1） |
| `s_group_desc` | 指向第一个块组的描述符，用于位图、inode 表、空闲计数 |
| `s_root_ino` | 根目录 inode 号（2），挂载时加载根 inode |

### 5.2 `struct ext4_inode_info`（全部使用）

| 字段 | 用途 |
|------|------|
| `i_block[15]` | 数据块号（含目录块、文件数据块），用于读目录、读写文件、分配新块 |
| `i_flags` | Ext4 inode 标志，从磁盘读入并写回 |

### 5.3 VFS `struct super_block`（本项目用到的）

| 字段 | 用途 |
|------|------|
| `s_list` | 超级块链表 |
| `s_dev` | 设备号 |
| `s_blocksize`, `s_blocksize_bits` | 块大小 |
| `s_maxbytes` | 最大文件大小 |
| `s_type`, `s_op` | 文件系统类型与操作表 |
| `s_flags`, `s_magic` | 挂载标志与魔数 |
| `s_root` | 根目录 dentry，路径解析起点 |
| `s_count`, `s_active` | 引用计数（简化） |
| `s_umount_flag` | 卸载标志 |
| `s_bdev` | 块设备 |
| `s_fs_info` | 指向 `ext4_sb_info` |
| `s_time_gran` | 时间粒度 |
| `s_id`, `s_uuid` | 标识与 UUID |

### 5.4 VFS `struct inode`（本项目用到的）

常用字段：`i_ino`, `i_mode`, `i_size`, `i_blocks`, `i_atime/mtime/ctime`, `i_uid`, `i_gid`, `i_nlink`, `i_sb`, `i_op`, `i_fop`, `i_private`（指向 `ext4_inode_info`）, `i_mapping`（仅指针），以及创建/删除时设置的 `i_ino`、`i_mode`、`i_size`、`i_blocks`、`i_nlink` 等。

### 5.5 VFS `struct dentry`（本项目用到的）

路径解析、lookup、create、mkdir 等会用到：`d_name`, `d_inode`, `d_parent`, `d_child`, `d_sb` 等。

---

## 六、按功能归纳

| 功能 | 使用的数据 |
|------|------------|
| **挂载** | 超级块：magic, log_block_size, blocks_per_group, inodes_per_group, inodes_count, blocks_count_lo, first_data_block, uuid；sbi 全部字段；sb 的 s_root、s_fs_info、s_op 等 |
| **块分配/释放** | sbi（含 s_group_desc）, bg_block_bitmap_lo, bg_free_blocks_count_lo；位图块读写 |
| **Inode 分配/释放** | sbi, bg_inode_bitmap_lo, bg_free_inodes_count_lo；inode 位图块读写 |
| **Inode 读** | sbi, bg_inode_table_lo, 磁盘 inode 的 mode/size/blocks/时间/uid/gid/nlink/block[]/flags |
| **Inode 写** | 同上，将 VFS inode 与 ext4_inode_info 写回磁盘 inode |
| **目录查找/添加/删除/遍历** | ext4_dir_entry 的 inode/rec_len/name_len/file_type/name；ei->i_block[] 定位目录块 |
| **文件读写** | ei->i_block[] 定位数据块，inode->i_size 等 |

---

## 七、小结

- **磁盘上**：超级块中约 8 个字段参与挂载与布局计算；块组描述符中 5 个字段参与分配与 inode 表定位；磁盘 inode 中约 15 个字段参与读写；目录项全部字段参与目录操作。
- **内存中**：`ext4_sb_info`、`ext4_inode_info` 全部字段均被使用；VFS 的 `super_block`、`inode`、`dentry` 使用其核心子集。
- 与 `UNUSED_STRUCTURES_SIMPLE_EXT4.md` 对照可知：未使用的多为日志、配额、xattr、ACL、加密、校验和、错误统计、大容量/扩展等高级特性相关字段；本项目只实现“简单 Ext4”所需的最小数据集合。
