#ifndef EXT4FS_H
#define EXT4FS_H

#include <fs/ext4.h>

struct ext4_fs *ext4_mount(void *private, int (*read_block)(void *, u64, u8 *), u64 block_count);
void ext4_unmount(struct ext4_fs *fs);
struct ext4_inode_info *ext4_iget(struct ext4_fs *fs, u32 ino);
struct ext4_inode_info *ext4_lookup(struct ext4_fs *fs, u32 parent_ino, const char *name);

#endif
