#ifndef _EXT4_HTREE_H
#define _EXT4_HTREE_H

#include <linux/fs.h>
#include <fs/dentry.h>

uint32_t ext4_htree_name_hash32(const struct inode *dir, const struct qstr *name);

int ext4_htree_pick_leaf_from_root(struct inode *dir, uint32_t block_size,
				   char *root_buf, uint32_t h32,
				   uint32_t *out_leaf_lblk);

int ext4_htree_try_insert_leaf(char *leaf, uint32_t block_size,
			       const struct qstr *name, unsigned long ino,
			       uint16_t need_rec, uint32_t *out_de_off);

int ext4_htree_split_leaf(struct inode *dir, struct super_block *sb,
			  struct ext4_inode_info *ei, uint32_t block_size,
			  char *root_buf, uint32_t root_phys,
			  uint32_t leaf_lblk, const struct qstr *name,
			  unsigned long ino, uint16_t need_rec);

#endif

