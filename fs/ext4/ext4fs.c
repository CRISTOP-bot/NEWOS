#include <fs/ext4.h>
#include <mm/mm_heap.h>
#include <lib/kernel/iru_string.h>
#include <core/core_printk.h>

struct ext4_fs *ext4_mount(void *private, int (*read_block)(void *, u64, u8 *), u64 block_count) {
    struct ext4_fs *fs = (struct ext4_fs *)kmalloc(sizeof(struct ext4_fs));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(struct ext4_fs));

    fs->private = private;
    fs->read_block = read_block;
    fs->total_blocks = block_count;

    u8 *sb_buf = (u8 *)kmalloc(EXT4_BLOCK_SIZE);
    if (!sb_buf) { kfree(fs); return NULL; }
    if (read_block(private, 1, sb_buf) != 0) { kfree(sb_buf); kfree(fs); return NULL; }

    struct ext4_super_block *sb = (struct ext4_super_block *)sb_buf;
    if (sb->s_magic != EXT4_SUPER_MAGIC) {
        kfree(sb_buf); kfree(fs); return NULL;
    }

    fs->sb = sb;
    fs->block_size = 1024 << sb->s_log_block_size;
    fs->blocks_per_group = sb->s_blocks_per_group;
    fs->inodes_per_group = sb->s_inodes_per_group;
    fs->total_blocks = sb->s_blocks_count_lo;
    fs->total_inodes = sb->s_inodes_count;
    fs->first_data_block = sb->s_first_data_block;
    fs->group_count = (fs->total_blocks + fs->blocks_per_group - 1) / fs->blocks_per_group;

    kfree(sb_buf);

    u64 gd_size = fs->group_count * EXT4_DESC_SIZE;
    fs->gd = (struct ext4_group_desc *)kmalloc(gd_size);
    if (!fs->gd) { ext4_unmount(fs); return NULL; }
    memset(fs->gd, 0, gd_size);

    u32 desc_blocks = (gd_size + fs->block_size - 1) / fs->block_size;
    for (u32 i = 0; i < desc_blocks; i++) {
        u64 block = fs->first_data_block + 1 + i;
        u8 *buf = (u8 *)kmalloc(fs->block_size);
        if (!buf) { ext4_unmount(fs); return NULL; }
        if (read_block(private, block, buf) != 0) { kfree(buf); ext4_unmount(fs); return NULL; }
        u32 count = (i == desc_blocks - 1) ? (gd_size - i * fs->block_size) : fs->block_size;
        memcpy((u8 *)fs->gd + i * fs->block_size, buf, count);
        kfree(buf);
    }

    return fs;
}

void ext4_unmount(struct ext4_fs *fs) {
    if (!fs) return;
    if (fs->sb) kfree(fs->sb);
    if (fs->gd) kfree(fs->gd);
    kfree(fs);
}

int ext4_read_inode(struct ext4_fs *fs, u32 ino, struct ext4_inode_info *info) {
    if (!fs || !info || ino < 1 || ino > fs->total_inodes) return -1;
    memset(info, 0, sizeof(struct ext4_inode_info));
    info->inode_num = ino;

    u32 group = (ino - 1) / fs->inodes_per_group;
    u32 idx = (ino - 1) % fs->inodes_per_group;
    u64 block = fs->gd[group].bg_inode_table_lo * (fs->block_size / 512) + (idx * fs->sb->s_inode_size) / 512;
    u64 offset = (idx * fs->sb->s_inode_size) % fs->block_size;

    u8 *buf = (u8 *)kmalloc(fs->block_size);
    if (!buf) return -1;
    if (fs->read_block(fs->private, block, buf) != 0) { kfree(buf); return -1; }
    memcpy(&info->raw, buf + offset, sizeof(struct ext4_inode));
    kfree(buf);

    info->size = info->raw.i_size_lo;
    info->mode = info->raw.i_mode;
    info->uid = (info->raw.i_uid_high << 16) | info->raw.i_uid_low;
    info->gid = (info->raw.i_gid_high << 16) | info->raw.i_gid_low;
    info->links_count = info->raw.i_links_count;
    info->block_count = info->raw.i_blocks_lo;
    info->block_group = group;
    info->valid = true;

    for (int i = 0; i < EXT4_NDIR_BLOCKS + 4; i++) {
        info->i_block[i] = info->raw.i_block[i];
    }

    return 0;
}

struct ext4_inode_info *ext4_iget(struct ext4_fs *fs, u32 ino) {
    struct ext4_inode_info *info = (struct ext4_inode_info *)kmalloc(sizeof(struct ext4_inode_info));
    if (!info) return NULL;
    if (ext4_read_inode(fs, ino, info) != 0) { kfree(info); return NULL; }
    return info;
}

int ext4_bmap(struct ext4_fs *fs, struct ext4_inode_info *info, u32 block_num) {
    if (!info || block_num > 1023 * 1024) return -1;

    if (block_num < EXT4_NDIR_BLOCKS) {
        return info->i_block[block_num];
    }

    if (block_num == EXT4_IND_BLOCK) {
        u8 *buf = (u8 *)kmalloc(fs->block_size);
        if (!buf) return -1;
        if (fs->read_block(fs->private, info->i_block[EXT4_IND_BLOCK], buf) != 0) { kfree(buf); return -1; }
        u32 *ptr = (u32 *)buf;
        int result = ptr[0];
        kfree(buf);
        return result;
    }

    if (block_num == EXT4_DIND_BLOCK) {
        u8 *buf = (u8 *)kmalloc(fs->block_size);
        if (!buf) return -1;
        if (fs->read_block(fs->private, info->i_block[EXT4_DIND_BLOCK], buf) != 0) { kfree(buf); return -1; }
        u32 *ptr = (u32 *)buf;
        int result = ptr[0];
        kfree(buf);
        return result;
    }

    return -1;
}

struct ext4_inode_info *ext4_lookup(struct ext4_fs *fs, u32 parent_ino, const char *name) {
    struct ext4_inode_info *parent = ext4_iget(fs, parent_ino);
    if (!parent) return NULL;

    u32 block = parent->i_block[0];
    if (block == 0) { kfree(parent); return NULL; }

    u8 *dir_buf = (u8 *)kmalloc(fs->block_size);
    if (!dir_buf) { kfree(parent); return NULL; }
    if (fs->read_block(fs->private, block, dir_buf) != 0) { kfree(dir_buf); kfree(parent); return NULL; }

    struct ext4_dir_entry *de = (struct ext4_dir_entry *)dir_buf;
    struct ext4_inode_info *result = NULL;
    u32 offset = 0;

    while (offset < fs->block_size) {
        if (de->inode == 0 || de->rec_len == 0) break;
        if (de->name_len == strlen(name) && memcmp(de->name, name, de->name_len) == 0) {
            result = ext4_iget(fs, de->inode);
            break;
        }
        offset += de->rec_len;
        de = (struct ext4_dir_entry *)((u8 *)de + de->rec_len);
    }

    kfree(dir_buf);
    kfree(parent);
    return result;
}
