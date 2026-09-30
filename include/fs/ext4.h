#ifndef EXT4_H
#define EXT4_H

#include <core/core_types.h>

#define EXT4_SUPER_MAGIC 0xEF53
#define EXT4_BLOCK_SIZE 4096
#define EXT4_INODES_PER_GROUP 1
#define EXT4_DESC_SIZE 32

#define EXT4_S_ISREG 0x8000
#define EXT4_S_ISDIR 0x4000
#define EXT4_S_IFBLK 0x6000
#define EXT4_S_IFCHR 0x2000

#define EXT4_NDIR_BLOCKS 12
#define EXT4_IND_BLOCK 12
#define EXT4_DIND_BLOCK 13
#define EXT4_TIND_BLOCK 14

#define EXT4_FEATURE_INCOMPAT_DIRTY 0x00000001
#define EXT4_FEATURE_INCOMPAT_JOURNAL 0x00000002
#define EXT4_FEATURE_INCOMPAT_RELOC 0x00000004
#define EXT4_FEATURE_INCOMPAT_MRG_PRJ 0x00000008
#define EXT4_FEATURE_INCOMPAT_EXTENT 0x00000010
#define EXT4_FEATURE_INCOMPAT_64BIT 0x00000020
#define EXT4_FEATURE_INCOMPAT_MMP 0x00000040
#define EXT4_FEATURE_INCOMPAT_FLEX_BG 0x00000080
#define EXT4_FEATURE_INCOMPAT_EA_INODE 0x00000100
#define EXT4_FEATURE_INCOMPAT_DIRDATA 0x00000200
#define EXT4_FEATURE_INCOMPAT_CSUM_SECT 0x00000400
#define EXT4_FEATURE_INCOMPAT_LARGE_DIR 0x00000800
#define EXT4_FEATURE_INCOMPAT_INLINE_DATA 0x00001000
#define EXT4_FEATURE_INCOMPAT_ENCRYPT 0x00002000

struct ext4_super_block {
    u32 s_inodes_count;
    u32 s_blocks_count_lo;
    u32 s_r_blocks_count_lo;
    u32 s_free_blocks_count_lo;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_cluster_size;
    u32 s_blocks_per_group;
    u32 s_clusters_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8 s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    u32 s_algorithm_usage_bitmap;
    u8 s_prealloc_blocks;
    u8 s_prealloc_dir_blocks;
    u16 s_reserved_gdt_blocks;
    u8 s_journal_backup_type;
    u8 s_desc_size;
    u16 s_default_mount_opts;
    u32 s_first_meta_bg;
    u32 s_kbytes_written;
    u32 s_blocks_count_hi;
    u32 s_r_blocks_count_hi;
    u32 s_free_blocks_count_hi;
    u16 s_min_extra_isize;
    u16 s_want_extra_isize;
    u32 s_flags;
    u16 s_raid_stride;
    u16 s_mmp_update_interval;
    u32 s_mmp_block;
    u32 s_raid_stripe_width;
    u8 s_groups_per_array;
    u8 s_reserved[188];
} __attribute__((packed));

struct ext4_group_desc {
    u32 bg_block_bitmap_lo;
    u32 bg_inode_bitmap_lo;
    u32 bg_inode_table_lo;
    u32 bg_free_blocks_count_lo;
    u32 bg_free_inodes_count;
    u32 bg_used_dirs_count_lo;
    u32 bg_free_blocks_count_hi;
    u32 bg_free_inodes_count_hi;
    u32 bg_used_dirs_count_hi;
    u32 bg_itable_unused_lo;
    u32 bg_itable_unused_hi;
    u32 bg_checksum;
} __attribute__((packed));

struct ext4_inode {
    u16 i_mode;
    u16 i_uid_low;
    u32 i_size_lo;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid_low;
    u16 i_links_count;
    u32 i_blocks_lo;
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[EXT4_NDIR_BLOCKS + 4];
    u32 i_generation;
    u32 i_file_acl_lo;
    u32 i_size_high;
    u32 i_obso_faddr;
    u16 i_uid_high;
    u16 i_gid_high;
    u16 i_checksum_lo;
    u16 i_sectors;
    u16 i_checksum_high;
    u16 i_ctime_extra;
    u16 i_atime_extra;
    u16 i_mtime_extra;
    u16 i_crtime;
    u16 i_crtime_extra;
    u32 i_version_extra;
} __attribute__((packed));

struct ext4_dir_entry {
    u32 inode;
    u16 rec_len;
    u8 name_len;
    u8 file_type;
    char name[256];
} __attribute__((packed));

#define EXT4_FT_UNKNOWN 0
#define EXT4_FT_REG_FILE 1
#define EXT4_FT_DIR 2
#define EXT4_FT_CHRDEV 3
#define EXT4_FT_BLKDEV 4
#define EXT4_FT_FIFO 5
#define EXT4_FT_SOCK 6
#define EXT4_FT_SYMLINK 7

struct ext4_fs {
    void *private;
    struct ext4_super_block *sb;
    struct ext4_group_desc *gd;
    u64 block_size;
    u64 blocks_per_group;
    u64 inodes_per_group;
    u64 total_blocks;
    u64 total_inodes;
    u64 first_data_block;
    u64 group_count;
    int (*read_block)(void *private, u64 block, u8 *buf);
};

struct ext4_inode_info {
    struct ext4_inode raw;
    u64 size;
    u32 mode;
    u32 uid;
    u32 gid;
    u32 links_count;
    u32 block_count;
    u32 block_group;
    u32 inode_num;
    u64 i_block[EXT4_NDIR_BLOCKS + 4];
    bool valid;
};

struct ext4_fs *ext4_mount(void *private, int (*read_block)(void *, u64, u8 *), u64 block_count);
void ext4_unmount(struct ext4_fs *fs);
struct ext4_inode_info *ext4_iget(struct ext4_fs *fs, u32 ino);
int ext4_read_inode(struct ext4_fs *fs, u32 ino, struct ext4_inode_info *info);
int ext4_read_blocks(struct ext4_fs *fs, u32 inode_num, u32 block_group, u32 *blocks, int max_blocks);
int ext4_bmap(struct ext4_fs *fs, struct ext4_inode_info *info, u32 block);

#endif
