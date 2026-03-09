/* 
 * VFS Dentry 缓存管理
 * 提供统一的 dentry 分配、查找、引用计数管理
 */

#include <linux/fs.h>
#include <fs/dentry.h>
#include <lib/printf.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 简化的内存操作函数 */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	for (size_t i = 0; i < n; i++) {
		p[i] = (unsigned char)c;
	}
	return s;
}

static void *simple_memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	for (size_t i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dest;
}

static int simple_memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	for (size_t i = 0; i < n; i++) {
		if (p1[i] != p2[i]) {
			return p1[i] - p2[i];
		}
	}
	return 0;
}

#define memset simple_memset
#define memcpy simple_memcpy
#define memcmp simple_memcmp

/* Dentry 池大小 */
#define DCACHE_MAX_DENTRIES 256

/* Dentry 静态池 */
static struct dentry dcache_pool[DCACHE_MAX_DENTRIES];
static bool dcache_used[DCACHE_MAX_DENTRIES];

/**
 * d_alloc - 分配一个新的 dentry
 * @parent: 父目录 dentry
 * @name: 名称（qstr）
 * 
 * 返回新分配的 dentry，失败返回 NULL
 */
struct dentry *d_alloc(struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry;
	
	if (!name) {
		return NULL;
	}
	
	/* 从池中分配一个空闲 dentry */
	for (int i = 0; i < DCACHE_MAX_DENTRIES; i++) {
		if (!dcache_used[i]) {
			dcache_used[i] = true;
			dentry = &dcache_pool[i];
			
			/* 初始化 dentry */
			memset(dentry, 0, sizeof(*dentry));
			dentry->d_count = 1;
			dentry->d_flags = 0;
			dentry->d_seq = 0;
			
			/* 设置名称 */
			if (name->len < sizeof(dentry->d_iname)) {
				/* 短名称，使用内联存储 */
				memcpy(dentry->d_iname, name->name, name->len);
				dentry->d_iname[name->len] = '\0';
				dentry->d_name.name = dentry->d_iname;
			} else {
				/* 长名称，需要外部存储（简化版，暂时不支持） */
				return NULL;
			}
			dentry->d_name.len = name->len;
			dentry->d_name.hash = name->hash;
			
			/* 设置父子关系 */
			dentry->d_parent = parent;
			INIT_LIST_HEAD(&dentry->d_child);
			INIT_LIST_HEAD(&dentry->d_subdirs);
			
			/* 设置超级块 */
			if (parent) {
				dentry->d_sb = parent->d_sb;
			}
			
			return dentry;
		}
	}
	
	return NULL;
}

/**
 * d_lookup - 在父目录中查找指定名称的 dentry
 * @parent: 父目录 dentry
 * @name: 要查找的名称
 * 
 * 返回找到的 dentry，未找到返回 NULL
 */
struct dentry *d_lookup(struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry;
	struct list_head *p;
	
	if (!parent || !name) {
		return NULL;
	}
	
	/* 遍历父目录的子项链表 */
	list_for_each(p, &parent->d_subdirs) {
		dentry = list_entry(p, struct dentry, d_child);
		
		/* 比较名称 */
		if (dentry->d_name.len == name->len &&
		    memcmp(dentry->d_name.name, name->name, name->len) == 0) {
			/* 增加引用计数 */
			dentry->d_count++;
			return dentry;
		}
	}
	
	return NULL;
}

/**
 * dget - 增加 dentry 引用计数
 * @dentry: dentry 指针
 * 
 * 返回 dentry 指针
 */
struct dentry *dget(struct dentry *dentry)
{
	if (dentry) {
		dentry->d_count++;
	}
	return dentry;
}

/**
 * dput - 减少 dentry 引用计数，如果为 0 则释放
 * @dentry: dentry 指针
 */
void dput(struct dentry *dentry)
{
	if (!dentry) {
		return;
	}
	
	if (dentry->d_count > 0) {
		dentry->d_count--;
	}
	
	/* 如果引用计数为 0，释放 dentry */
	if (dentry->d_count == 0) {
		/* 从父目录的子项链表中移除 */
		if (!list_empty(&dentry->d_child)) {
			list_del(&dentry->d_child);
		}
		
		/* 释放关联的 inode（如果存在） */
		if (dentry->d_inode) {
			/* 这里应该调用 iput，但简化版先不处理 */
		}
		
		/* 标记为未使用 */
		for (int i = 0; i < DCACHE_MAX_DENTRIES; i++) {
			if (&dcache_pool[i] == dentry) {
				dcache_used[i] = false;
				break;
			}
		}
	}
}

/**
 * d_add - 将 dentry 添加到父目录
 * @dentry: 要添加的 dentry
 * @inode: 关联的 inode
 */
void d_add(struct dentry *dentry, struct inode *inode)
{
	if (!dentry) {
		return;
	}
	
	/* 设置 inode */
	dentry->d_inode = inode;
	
	/* 添加到父目录的子项链表 */
	if (dentry->d_parent && inode) {
		list_add(&dentry->d_child, &dentry->d_parent->d_subdirs);
	}
}

/**
 * d_instantiate - 实例化 dentry（关联 inode）
 * @dentry: dentry 指针
 * @inode: inode 指针
 */
void d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if (!dentry) {
		return;
	}
	
	dentry->d_inode = inode;
	if (inode) {
		inode->i_count++;
	}
}

/**
 * d_hash_string - 计算字符串的哈希值（简化版）
 * @str: 字符串
 * @len: 长度
 */
u32 d_hash_string(const char *str, int len)
{
	u32 hash = 0;
	
	for (int i = 0; i < len; i++) {
		hash = (hash << 5) - hash + (unsigned char)str[i];
	}
	
	return hash;
}

/**
 * qstr_init - 初始化 qstr
 * @qstr: qstr 指针
 * @name: 名称字符串
 * @len: 名称长度
 */
void qstr_init(struct qstr *qstr, const char *name, int len)
{
	if (!qstr || !name) {
		return;
	}
	
	qstr->name = (const unsigned char *)name;
	qstr->len = len;
	qstr->hash = d_hash_string(name, len);
}
