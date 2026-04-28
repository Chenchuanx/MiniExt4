/* 简化版 B+Tree 
 *
 * 特性：
 * - 所有实际数据存放在叶子结点，叶子按键递增排列并通过 next 指针串联
 * - 内部结点只存 key 和子指针
 * - 不调用 malloc/free，由上层通过回调提供结点内存
 *
 * 限制：
 * - 当前只实现搜索和插入，未实现删除和合并（对于大多数索引用例已足够）
 */

#include <fs/bptree.h>

/* 辅助：创建并初始化一个新结点 */
static struct bptree_node *bptree_new_node(struct bptree_root *root, int is_leaf)
{
	struct bptree_node *n;
	int i;

	if (!root || !root->alloc_node)
		return (struct bptree_node *)0;

	n = root->alloc_node();
	if (!n)
		return (struct bptree_node *)0;

	n->is_leaf = is_leaf;
	n->num_keys = 0;
	for (i = 0; i < BPTREE_MAX_KEYS; i++) {
		n->keys[i] = 0;
		if (is_leaf)
			n->u.values[i] = (void *)0;
		else
			n->u.children[i] = (struct bptree_node *)0;
	}
	if (!is_leaf)
		n->u.children[BPTREE_MAX_KEYS] = (struct bptree_node *)0;
	n->next = (struct bptree_node *)0;
	return n;
}

void bptree_init(struct bptree_root *root,
		 bptree_alloc_node_fn alloc_node,
		 bptree_free_node_fn free_node)
{
	if (!root)
		return;
	root->root = (struct bptree_node *)0;
	root->alloc_node = alloc_node;
	root->free_node = free_node;
}

/* 在结点内二分查找 key，应路由到第几个下标 */
static int bptree_find_index(struct bptree_node *node, u64 key)
{
	int low = 0;
	int high = node->num_keys - 1;
	int mid;

	while (low <= high) {
		mid = (low + high) / 2;
		if (key < node->keys[mid])
			high = mid - 1;
		else if (key > node->keys[mid])
			low = mid + 1;
		else
			return mid;
	}
	return low; /* 第一个 >= key 的下标（或 num_keys） */
}

void *bptree_search(struct bptree_root *root, u64 key)
{
	struct bptree_node *node;
	int idx;

	if (!root || !root->root)
		return (void *)0;

	node = root->root;
	/* 从根一路向下找到叶子 */
	while (node && !node->is_leaf) {
		idx = bptree_find_index(node, key);
		if (idx < node->num_keys && key >= node->keys[idx]) {
			/* 理论上 B+Tree 内部结点的 keys[i] 为上界，这里采取常见实现： */
			idx++;
		}
		if (idx > node->num_keys)
			idx = node->num_keys;
		node = node->u.children[idx];
	}

	if (!node)
		return (void *)0;

	/* 在线性区间中二分/线性查找 key */
	idx = bptree_find_index(node, key);
	if (idx < node->num_keys && node->keys[idx] == key)
		return node->u.values[idx];
	return (void *)0;
}

/* 叶子内部插入（不考虑溢出，调用者保证有空间或先 split） */
static void bptree_leaf_insert_nosplit(struct bptree_node *leaf, u64 key, void *value)
{
	int i = leaf->num_keys - 1;

	/* 若 key 已存在，则更新 value */
	while (i >= 0 && leaf->keys[i] > key)
		i--;
	if (i >= 0 && leaf->keys[i] == key) {
		leaf->u.values[i] = value;
		return;
	}

	/* i 停在 < key 的位置，插入位置是 i+1 */
	i = leaf->num_keys - 1;
	while (i >= 0 && leaf->keys[i] > key) {
		leaf->keys[i + 1] = leaf->keys[i];
		leaf->u.values[i + 1] = leaf->u.values[i];
		i--;
	}
	leaf->keys[i + 1] = key;
	leaf->u.values[i + 1] = value;
	leaf->num_keys++;
}

/* 叶子满节点插入：使用临时数组避免越界写入 */
static int bptree_leaf_insert_split(struct bptree_root *root,
				    struct bptree_node *leaf,
				    u64 key, void *value,
				    struct bptree_node **new_leaf_out,
				    u64 *promote_key_out)
{
	u64 tmp_keys[BPTREE_MAX_KEYS + 1];
	void *tmp_vals[BPTREE_MAX_KEYS + 1];
	struct bptree_node *new_leaf;
	int old_n = leaf->num_keys;
	int pos = 0;
	int i;
	int split;

	/* 满结点场景下 old_n 应为 BPTREE_MAX_KEYS */
	if (old_n != BPTREE_MAX_KEYS)
		return -1;

	/* 找插入位置；相同 key 直接更新并返回，不需要分裂 */
	while (pos < old_n && leaf->keys[pos] < key)
		pos++;
	if (pos < old_n && leaf->keys[pos] == key) {
		leaf->u.values[pos] = value;
		*new_leaf_out = (struct bptree_node *)0;
		return 0;
	}

	/* 合并到临时数组（共 old_n + 1 项） */
	for (i = 0; i < pos; i++) {
		tmp_keys[i] = leaf->keys[i];
		tmp_vals[i] = leaf->u.values[i];
	}
	tmp_keys[pos] = key;
	tmp_vals[pos] = value;
	for (i = pos; i < old_n; i++) {
		tmp_keys[i + 1] = leaf->keys[i];
		tmp_vals[i + 1] = leaf->u.values[i];
	}

	new_leaf = bptree_new_node(root, 1);
	if (!new_leaf)
		return -1;

	/* 9 项 -> 左 4、右 5（当 MAX_KEYS=8） */
	split = (BPTREE_MAX_KEYS + 1) / 2;

	leaf->num_keys = split;
	for (i = 0; i < split; i++) {
		leaf->keys[i] = tmp_keys[i];
		leaf->u.values[i] = tmp_vals[i];
	}
	for (; i < BPTREE_MAX_KEYS; i++) {
		leaf->keys[i] = 0;
		leaf->u.values[i] = (void *)0;
	}

	new_leaf->num_keys = (BPTREE_MAX_KEYS + 1) - split;
	for (i = 0; i < new_leaf->num_keys; i++) {
		new_leaf->keys[i] = tmp_keys[split + i];
		new_leaf->u.values[i] = tmp_vals[split + i];
	}

	new_leaf->next = leaf->next;
	leaf->next = new_leaf;

	*new_leaf_out = new_leaf;
	*promote_key_out = new_leaf->keys[0];
	return 0;
}

/* 内部结点插入一个 (key, child)（不考虑溢出） */
static void bptree_internal_insert_nosplit(struct bptree_node *node,
					   u64 key,
					   struct bptree_node *child)
{
	int i = node->num_keys - 1;

	while (i >= 0 && node->keys[i] > key) {
		node->keys[i + 1] = node->keys[i];
		node->u.children[i + 2] = node->u.children[i + 1];
		i--;
	}
	node->keys[i + 1] = key;
	node->u.children[i + 2] = child;
	node->num_keys++;
}

/* 内部满节点插入：使用临时数组避免越界写入 */
static int bptree_internal_insert_split(struct bptree_root *root,
					struct bptree_node *node,
					u64 key, struct bptree_node *child,
					struct bptree_node **new_node_out,
					u64 *promote_key_out)
{
	u64 tmp_keys[BPTREE_MAX_KEYS + 1];
	struct bptree_node *tmp_children[BPTREE_MAX_KEYS + 2];
	struct bptree_node *new_node;
	int old_n = node->num_keys;
	int ins = 0;
	int i;
	int mid;
	int right_n;

	if (old_n != BPTREE_MAX_KEYS)
		return -1;

	/* 找插入位置：key 插入到 children[ins] 与 children[ins+1] 间 */
	while (ins < old_n && node->keys[ins] < key)
		ins++;

	for (i = 0; i < ins; i++) {
		tmp_keys[i] = node->keys[i];
	}
	tmp_keys[ins] = key;
	for (i = ins; i < old_n; i++) {
		tmp_keys[i + 1] = node->keys[i];
	}

	for (i = 0; i <= ins; i++) {
		tmp_children[i] = node->u.children[i];
	}
	tmp_children[ins + 1] = child;
	for (i = ins + 1; i <= old_n; i++) {
		tmp_children[i + 1] = node->u.children[i];
	}

	new_node = bptree_new_node(root, 0);
	if (!new_node)
		return -1;

	/* 9 keys -> 提升中间 key[4]；左 4 keys，右 4 keys */
	mid = (BPTREE_MAX_KEYS + 1) / 2;
	*promote_key_out = tmp_keys[mid];

	node->num_keys = mid;
	for (i = 0; i < node->num_keys; i++) {
		node->keys[i] = tmp_keys[i];
		node->u.children[i] = tmp_children[i];
	}
	node->u.children[node->num_keys] = tmp_children[node->num_keys];
	for (; i < BPTREE_MAX_KEYS; i++) {
		node->keys[i] = 0;
		node->u.children[i + 1] = (struct bptree_node *)0;
	}

	right_n = (BPTREE_MAX_KEYS + 1) - mid - 1;
	new_node->num_keys = right_n;
	for (i = 0; i < right_n; i++) {
		new_node->keys[i] = tmp_keys[mid + 1 + i];
		new_node->u.children[i] = tmp_children[mid + 1 + i];
	}
	new_node->u.children[right_n] = tmp_children[mid + 1 + right_n];

	*new_node_out = new_node;
	return 0;
}

/* 分裂叶子结点：
 * - 原 leaf 被截断为前半部分
 * - 新叶子 new_leaf 存放后半部分
 * - 返回分裂后“右半部分”的第一个 key 作为提升到父结点的 key
 */
static u64 bptree_split_leaf(struct bptree_root *root,
			     struct bptree_node *leaf,
			     struct bptree_node **new_leaf_out)
{
	int split = (BPTREE_MAX_KEYS + 1) / 2;
	int i, j;
	struct bptree_node *new_leaf;

	new_leaf = bptree_new_node(root, 1);
	if (!new_leaf)
		return 0;

	/* 把后半部分搬到 new_leaf */
	j = 0;
	for (i = split; i < leaf->num_keys; i++, j++) {
		new_leaf->keys[j] = leaf->keys[i];
		new_leaf->u.values[j] = leaf->u.values[i];
	}
	new_leaf->num_keys = leaf->num_keys - split;
	leaf->num_keys = split;

	/* 维护叶子链表：leaf -> new_leaf -> 原 next */
	new_leaf->next = leaf->next;
	leaf->next = new_leaf;

	*new_leaf_out = new_leaf;
	return new_leaf->keys[0]; /* 提升到父结点的 key */
}

/* 分裂内部结点：
 * - 原 node 保留前半部分和中间 key 左侧
 * - 中间 key（middle_key）提升到父结点
 * - new_node 存放右半部分
 */
static u64 bptree_split_internal(struct bptree_root *root,
				 struct bptree_node *node,
				 struct bptree_node **new_node_out)
{
	int split = node->num_keys / 2;
	int i, j;
	struct bptree_node *new_node;
	u64 middle_key = node->keys[split];

	new_node = bptree_new_node(root, 0);
	if (!new_node)
		return 0;

	/* 右半部分 keys 与 children 迁移到 new_node */
	j = 0;
	for (i = split + 1; i < node->num_keys; i++, j++) {
		new_node->keys[j] = node->keys[i];
		new_node->u.children[j] = node->u.children[i];
	}
	new_node->u.children[j] = node->u.children[node->num_keys];
	new_node->num_keys = node->num_keys - split - 1;

	/* 原 node 保留左半部分 */
	node->num_keys = split;

	*new_node_out = new_node;
	return middle_key;
}

/* 递归插入，可能导致根被替换
 *
 * - node: 当前子树根
 * - key/value: 要插入的数据
 * - new_child/new_key：如果子树发生分裂，返回提升到上一层的 child/key
 *
 * 返回 0 表示成功，-1 表示失败
 */
static int bptree_insert_recursive(struct bptree_root *root,
				   struct bptree_node *node,
				   u64 key,
				   void *value,
				   struct bptree_node **new_child,
				   u64 *new_key)
{
	int idx;
	int ret;

	if (node->is_leaf) {
		/* 叶子结点 */
		if (node->num_keys < BPTREE_MAX_KEYS) {
			/* 还有空间，直接插入 */
			bptree_leaf_insert_nosplit(node, key, value);
			*new_child = (struct bptree_node *)0;
			return 0;
		} else {
			/* 满结点插入必须走 split 辅助，避免对 keys[8] 越界写。 */
			if (bptree_leaf_insert_split(root, node, key, value, new_child, new_key) < 0)
				return -1;
			return 0;
		}
	}

	/* 内部结点：先找到下沉的子结点 */
	idx = bptree_find_index(node, key);
	if (idx < node->num_keys && key >= node->keys[idx])
		idx++;
	if (idx > node->num_keys)
		idx = node->num_keys;

	if (!node->u.children[idx]) {
		/* 理论上不该发生，防御性返回错误 */
		return -1;
	}

	{
		struct bptree_node *child_new = (struct bptree_node *)0;
		u64 child_key = 0;

		ret = bptree_insert_recursive(root, node->u.children[idx],
					      key, value, &child_new, &child_key);
		if (ret < 0)
			return ret;

		/* 子树没有分裂，则直接返回 */
		if (!child_new) {
			*new_child = (struct bptree_node *)0;
			return 0;
		}

		/* 把 child_key / child_new 插入当前内部结点 */
		if (node->num_keys < BPTREE_MAX_KEYS) {
			bptree_internal_insert_nosplit(node, child_key, child_new);
			*new_child = (struct bptree_node *)0;
			return 0;
		} else {
			/* 当前内部结点已满，走 split 辅助避免 children/keys 越界写。 */
			if (bptree_internal_insert_split(root, node, child_key, child_new, new_child, new_key) < 0)
				return -1;
			return 0;
		}
	}
}

int bptree_insert(struct bptree_root *root, u64 key, void *value)
{
	struct bptree_node *new_child = (struct bptree_node *)0;
	struct bptree_node *old_root;
	u64 new_key = 0;
	int ret;

	if (!root || !root->alloc_node)
		return -1;

	/* 空树：创建根叶子 */
	if (!root->root) {
		struct bptree_node *leaf = bptree_new_node(root, 1);
		if (!leaf)
			return -1;
		leaf->keys[0] = key;
		leaf->u.values[0] = value;
		leaf->num_keys = 1;
		root->root = leaf;
		return 0;
	}

	old_root = root->root;
	ret = bptree_insert_recursive(root, old_root, key, value,
				      &new_child, &new_key);
	if (ret < 0)
		return ret;

	/* 若根没有分裂，直接返回 */
	if (!new_child)
		return 0;

	/* 根结点分裂：创建新根 */
	{
		struct bptree_node *new_root = bptree_new_node(root, 0);
		if (!new_root)
			return -1;

		new_root->keys[0] = new_key;
		new_root->u.children[0] = old_root;
		new_root->u.children[1] = new_child;
		new_root->num_keys = 1;

		root->root = new_root;
	}

	return 0;
}

