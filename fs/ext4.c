#include "kernel/ext4.h"
#include "kernel/blk.h"
#include "kernel/kernel.h"

#define EXT4_SUPER_MAGIC 0xEF53
#define EXT4_NDIR_BLOCKS 12
#define EXT4_EXTENTS_FL 0x00080000
#define EXT4_EXT_MAGIC 0xf30a

#define EXT4_MAX_ROOT_ENTRIES 64
#define EXT4_NAME_MAX 64

struct ext4_super {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t pad0[0xFE - 0x68 - 4];
    uint16_t s_desc_size;
} __attribute__((packed));

struct ext4_group_desc {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
} __attribute__((packed));

struct ext4_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint8_t i_block[60];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
} __attribute__((packed));

struct ext4_extent_header {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} __attribute__((packed));

struct ext4_extent_idx {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} __attribute__((packed));

struct ext4_extent {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} __attribute__((packed));

struct ext4_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
} __attribute__((packed));

struct ext4_file_entry {
    char name[EXT4_NAME_MAX];
    uint32_t inode;
};

static struct ext4_super sb;
static uint32_t block_size;
static uint32_t inode_size;
static uint32_t groups_count;
static uint32_t desc_size;
static bool mounted;

static struct ext4_file_entry root_files[EXT4_MAX_ROOT_ENTRIES];
static int root_file_count;

static uint8_t block_buf[4096];
static uint8_t block_buf2[4096];
static uint8_t inode_buf[4096];

static uint32_t str_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int read_block(uint32_t block, void *buf)
{
    uint32_t sectors = block_size / 512;
    uint32_t first_sector = block * sectors;
    uint8_t *p = (uint8_t *) buf;

    for (uint32_t i = 0; i < sectors; i++) {
        if (blk_read(first_sector + i, p + i * 512) < 0)
            return -1;
    }
    return 0;
}

static int load_group_desc(uint32_t group, struct ext4_group_desc *gd)
{
    uint32_t gd_table_block = (block_size == 1024) ? 2 : 1;
    uint32_t gds_per_block = block_size / desc_size;
    uint32_t block = gd_table_block + group / gds_per_block;
    uint32_t index_in_block = group % gds_per_block;

    if (read_block(block, block_buf) < 0)
        return -1;

    memcpy(gd, block_buf + index_in_block * desc_size, sizeof(*gd));
    return 0;
}

static int read_inode(uint32_t inum, struct ext4_inode *out)
{
    if (inum == 0)
        return -1;

    uint32_t idx = inum - 1;
    uint32_t group = idx / sb.s_inodes_per_group;
    uint32_t local = idx % sb.s_inodes_per_group;

    struct ext4_group_desc gd;
    if (load_group_desc(group, &gd) < 0)
        return -1;

    if (gd.bg_inode_table_hi != 0)
        return -1;

    uint32_t inode_table_block = gd.bg_inode_table_lo;
    uint32_t inode_off = local * inode_size;
    uint32_t block = inode_table_block + inode_off / block_size;
    uint32_t off_in_block = inode_off % block_size;

    if (read_block(block, block_buf) < 0)
        return -1;

    if (off_in_block + inode_size <= block_size) {
        memcpy(inode_buf, block_buf + off_in_block, inode_size);
    } else {
        uint32_t first = block_size - off_in_block;
        memcpy(inode_buf, block_buf + off_in_block, first);
        if (read_block(block + 1, block_buf2) < 0)
            return -1;
        memcpy(inode_buf + first, block_buf2, inode_size - first);
    }

    memcpy(out, inode_buf, sizeof(*out));
    return 0;
}

static int extent_find_physical(const struct ext4_inode *ino, uint32_t lblock, uint32_t *pblock)
{
    const struct ext4_extent_header *eh = (const struct ext4_extent_header *) ino->i_block;
    if (eh->eh_magic != EXT4_EXT_MAGIC)
        return -1;

    if (eh->eh_depth == 0) {
        const struct ext4_extent *ex = (const struct ext4_extent *) (ino->i_block + sizeof(*eh));
        for (uint16_t i = 0; i < eh->eh_entries; i++) {
            uint32_t start = ex[i].ee_block;
            uint32_t len = ex[i].ee_len & 0x7fff;
            if (lblock >= start && lblock < start + len) {
                if (ex[i].ee_start_hi != 0)
                    return -1;
                *pblock = ex[i].ee_start_lo + (lblock - start);
                return 0;
            }
        }
        return -1;
    }

    if (eh->eh_depth == 1) {
        const struct ext4_extent_idx *ix =
            (const struct ext4_extent_idx *) (ino->i_block + sizeof(*eh));

        const struct ext4_extent_idx *chosen = NULL;
        for (uint16_t i = 0; i < eh->eh_entries; i++) {
            if (ix[i].ei_block <= lblock)
                chosen = &ix[i];
            else
                break;
        }
        if (!chosen)
            return -1;

        if (chosen->ei_leaf_hi != 0)
            return -1;
        if (read_block(chosen->ei_leaf_lo, block_buf) < 0)
            return -1;

        const struct ext4_extent_header *leh = (const struct ext4_extent_header *) block_buf;
        if (leh->eh_magic != EXT4_EXT_MAGIC || leh->eh_depth != 0)
            return -1;

        const struct ext4_extent *ex =
            (const struct ext4_extent *) (block_buf + sizeof(*leh));
        for (uint16_t i = 0; i < leh->eh_entries; i++) {
            uint32_t start = ex[i].ee_block;
            uint32_t len = ex[i].ee_len & 0x7fff;
            if (lblock >= start && lblock < start + len) {
                if (ex[i].ee_start_hi != 0)
                    return -1;
                *pblock = ex[i].ee_start_lo + (lblock - start);
                return 0;
            }
        }
        return -1;
    }

    return -1;
}

static int inode_read_data(const struct ext4_inode *ino, uint32_t off, void *buf, uint32_t len)
{
    if (ino->i_size_high != 0)
        return -1;
    uint32_t file_size = ino->i_size_lo;
    if (off >= file_size)
        return 0;

    uint32_t remaining = (off + len > file_size) ? (file_size - off) : len;
    uint8_t *dst = (uint8_t *) buf;
    uint32_t done = 0;

    while (done < remaining) {
        uint32_t pos = off + done;
        uint32_t lblock = pos / block_size;
        uint32_t boff = pos % block_size;
        uint32_t chunk = block_size - boff;
        if (chunk > remaining - done)
            chunk = remaining - done;

        uint32_t pblock;
        if (extent_find_physical(ino, lblock, &pblock) < 0)
            break;
        if (read_block(pblock, block_buf) < 0)
            break;

        memcpy(dst + done, block_buf + boff, chunk);
        done += chunk;
    }

    return (int) done;
}

static int scan_root_dir(void)
{
    struct ext4_inode root;
    if (read_inode(2, &root) < 0)
        return -1;

    if (root.i_size_high != 0)
        return -1;
    uint32_t size = root.i_size_lo;
    uint32_t off = 0;
    root_file_count = 0;

    while (off < size && root_file_count < EXT4_MAX_ROOT_ENTRIES) {
        int n = inode_read_data(&root, off, block_buf, block_size);
        if (n <= 0)
            break;

        uint32_t p = 0;
        while (p + sizeof(struct ext4_dirent) <= (uint32_t) n) {
            struct ext4_dirent *de = (struct ext4_dirent *) (block_buf + p);
            if (de->rec_len == 0)
                break;

            if (de->inode != 0 && de->name_len > 0 && de->name_len < EXT4_NAME_MAX) {
                char name[EXT4_NAME_MAX];
                memcpy(name, block_buf + p + sizeof(*de), de->name_len);
                name[de->name_len] = '\0';

                if (!(name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))) {
                    memcpy(root_files[root_file_count].name, name, de->name_len + 1);
                    root_files[root_file_count].inode = de->inode;
                    root_file_count++;
                    if (root_file_count >= EXT4_MAX_ROOT_ENTRIES)
                        break;
                }
            }

            p += de->rec_len;
        }

        off += block_size;
    }

    return 0;
}

int ext4_mount(void)
{
    if (mounted)
        return 0;

    if (blk_init() < 0)
        return -10;

    uint8_t sbraw[1024];
    if (blk_read(2, sbraw) < 0)
        return -11;
    if (blk_read(3, sbraw + 512) < 0)
        return -12;

    memcpy(&sb, sbraw, sizeof(sb));
    if (sb.s_magic != EXT4_SUPER_MAGIC)
        return -13;

    block_size = 1024U << sb.s_log_block_size;
    if (block_size > sizeof(block_buf))
        return -14;

    inode_size = sb.s_inode_size ? sb.s_inode_size : 128;
    desc_size = sb.s_desc_size ? sb.s_desc_size : 32;

    uint32_t blocks = sb.s_blocks_count_lo;
    groups_count = (blocks + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    if (groups_count == 0)
        return -15;

    if (scan_root_dir() < 0)
        return -16;

    mounted = true;
    return 0;
}

int ext4_lookup(const char *name)
{
    if (!mounted)
        return -1;

    for (int i = 0; i < root_file_count; i++) {
        if (str_eq(root_files[i].name, name))
            return i;
    }
    return -1;
}

int ext4_read_file(int file_index, uint32_t offset, void *buf, uint32_t len)
{
    if (!mounted || file_index < 0 || file_index >= root_file_count)
        return -1;

    struct ext4_inode ino;
    if (read_inode(root_files[file_index].inode, &ino) < 0)
        return -1;

    return inode_read_data(&ino, offset, buf, len);
}

int ext4_listdir(char *buf, uint32_t len)
{
    if (!mounted || !buf || len == 0)
        return -1;

    uint32_t out = 0;
    for (int i = 0; i < root_file_count; i++) {
        uint32_t n = str_len(root_files[i].name);
        if (out + n + 1 >= len)
            break;
        memcpy(buf + out, root_files[i].name, n);
        out += n;
        buf[out++] = '\n';
    }
    if (out < len)
        buf[out] = '\0';
    return (int) out;
}
