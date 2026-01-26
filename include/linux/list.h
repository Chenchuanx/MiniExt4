/* 
 * 简化版双向链表实现
 * 适用于 MiniExt4 项目
 */
#ifndef _LINUX_LIST_H
#define _LINUX_LIST_H

#include <linux/types.h>

/**
 * list_head - 双向链表节点
 */
struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

/**
 * LIST_HEAD_INIT - 初始化链表头（静态初始化）
 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }

/**
 * LIST_HEAD - 声明并初始化链表头
 */
#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

/**
 * INIT_LIST_HEAD - 初始化链表头（动态初始化）
 */
static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

/**
 * list_add - 在链表头部添加节点
 * @new_node: 要添加的新节点
 * @head: 链表头
 */
static inline void list_add(struct list_head *new_node, struct list_head *head)
{
	new_node->next = head->next;
	new_node->prev = head;
	head->next->prev = new_node;
	head->next = new_node;
}

/**
 * list_add_tail - 在链表尾部添加节点
 * @new_node: 要添加的新节点
 * @head: 链表头
 */
static inline void list_add_tail(struct list_head *new_node, struct list_head *head)
{
	new_node->next = head;
	new_node->prev = head->prev;
	head->prev->next = new_node;
	head->prev = new_node;
}

/**
 * list_del - 从链表中删除节点
 * @entry: 要删除的节点
 */
static inline void list_del(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

/**
 * list_del_init - 从链表中删除节点并重新初始化
 * @entry: 要删除的节点
 */
static inline void list_del_init(struct list_head *entry)
{
	list_del(entry);
	INIT_LIST_HEAD(entry);
}

/**
 * list_empty - 检查链表是否为空
 * @head: 链表头
 */
static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

/**
 * list_entry - 获取包含链表节点的结构体指针
 * @ptr: 链表节点指针
 * @type: 结构体类型
 * @member: 链表节点在结构体中的成员名
 */
#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

/**
 * list_first_entry - 获取链表中第一个元素
 * @ptr: 链表头指针
 * @type: 结构体类型
 * @member: 链表节点在结构体中的成员名
 */
#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

/**
 * list_for_each - 遍历链表
 * @pos: 当前节点指针
 * @head: 链表头
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * list_for_each_entry - 遍历链表并获取结构体指针
 * @pos: 当前结构体指针
 * @head: 链表头
 * @member: 链表节点在结构体中的成员名
 */
#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry(pos->member.next, typeof(*pos), member))

/**
 * list_for_each_entry_safe - 安全遍历链表（允许删除节点）
 * @pos: 当前结构体指针
 * @n: 临时指针，用于保存下一个节点
 * @head: 链表头
 * @member: 链表节点在结构体中的成员名
 */
#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member), \
	     n = list_entry(pos->member.next, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

#endif /* _LINUX_LIST_H */

