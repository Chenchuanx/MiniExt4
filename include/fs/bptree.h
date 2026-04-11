/* 简化版通用 B+Tree
 *
 * 设计目标：
 * - 通用：以 u64 作为键，值为 void*，可供 ext4 目录索引等多处复用
 * - B+Tree：所有实际数据都存放在叶子结点，叶子按键有序并通过链表串联
 * - 不直接使用 malloc/free：节点由调用者通过回调提供，方便在内核/受限环境下使用
 *
 * 后续如果需要更复杂/专用的 HTree，可以在此基础上做包装。
 */

#ifndef _FS_BPTREE_H
#define _FS_BPTREE_H

#include <linux/types.h>

/* B+Tree 参数：
 * - 阶数（最大关键字数量），这里取一个比较小的常数，方便在小内存环境下使用
 * - MAX_KEYS 表示每个结点最多存放多少 key
 *
 * 注意：阶数越大，单结点越“胖”，树高度越低，但单次 split 成本也略高。
 */
#define BPTREE_MAX_KEYS 8

/* 对外暴露完整结构定义，便于某些调用方在栈/静态区定义节点池 */
struct bptree_node {
	int is_leaf;
	int num_keys;
	u64 keys[BPTREE_MAX_KEYS];
	union {
		struct bptree_node *children[BPTREE_MAX_KEYS + 1];
		void *values[BPTREE_MAX_KEYS];
	} u;
	struct bptree_node *next;
};

/* 节点分配/释放回调
 *
 * - alloc_node: 返回一块可容纳 struct bptree_node 的内存，失败返回 NULL
 * - free_node: 释放由 alloc_node 获得的内存
 */
typedef struct bptree_node *(*bptree_alloc_node_fn)(void);
typedef void (*bptree_free_node_fn)(struct bptree_node *node);

/* B+Tree 根结构 */
struct bptree_root {
	struct bptree_node *root;
	bptree_alloc_node_fn alloc_node;
	bptree_free_node_fn free_node;
};

/* 初始化一棵 B+Tree
 *
 * - root: 根结构体（由调用者分配）
 * - alloc_node / free_node: 结点分配/释放回调，不可为 NULL
 */
void bptree_init(struct bptree_root *root,
		 bptree_alloc_node_fn alloc_node,
		 bptree_free_node_fn free_node);

/* 搜索
 *
 * - 在树中查找 key，对应的值存放在叶子结点的 value 数组中
 * - 找到返回 value（void*），未找到返回 NULL
 */
void *bptree_search(struct bptree_root *root, u64 key);

/* 插入
 *
 * - 若 key 不存在，则插入 (key, value)，成功返回 0
 * - 若 key 已存在，则更新其 value，返回 0
 * - 内存不足或其他错误返回 -1
 */
int bptree_insert(struct bptree_root *root, u64 key, void *value);

/* （可选）后续如需要删除，可在此处扩展：
 * int bptree_delete(struct bptree_root *root, u64 key);
 */

#endif /* _FS_BPTREE_H */

