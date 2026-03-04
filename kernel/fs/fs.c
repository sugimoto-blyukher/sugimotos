#include "kernel/kernel.h"
#include "kernel/ext4.h"
#include "kernel/syscall.h"

#define RAMFS_MAX_FILES 32
#define RAMFS_NAME_MAX 32
#define RAMFS_DATA_MAX 1024

struct ramfs_inode {
    int used;
    char name[RAMFS_NAME_MAX];
    uint8_t data[RAMFS_DATA_MAX];
    uint32_t size;
};

static struct ramfs_inode inodes[RAMFS_MAX_FILES];
static bool fs_initialized;
static bool ext4_ready;

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

static void str_copy(char *dst, const char *src)
{
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

static int normalize_name(const char *path, char *name_out)
{
    if (!path || !*path)
        return -1;

    const char *p = path;
    while (*p == '/')
        p++;
    if (!*p)
        return -1;

    int i = 0;
    while (*p && *p != '/') {
        if (i + 1 >= RAMFS_NAME_MAX)
            return -1;
        name_out[i++] = *p++;
    }
    if (*p != '\0')
        return -1;

    name_out[i] = '\0';
    return 0;
}

static int find_inode_by_name(const char *name)
{
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (inodes[i].used && str_eq(inodes[i].name, name))
            return i;
    }
    return -1;
}

static int alloc_inode(void)
{
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!inodes[i].used)
            return i;
    }
    return -1;
}

static int alloc_fd(struct process *proc)
{
    for (int i = 0; i < FD_MAX; i++) {
        if (!proc->fds[i].used)
            return i;
    }
    return -1;
}

static int is_ext_fd(const struct file_desc *d)
{
    return d->inode < 0;
}

void fs_init(void)
{
    if (fs_initialized)
        return;

    memset(inodes, 0, sizeof(inodes));

    int hello = alloc_inode();
    if (hello >= 0) {
        inodes[hello].used = 1;
        str_copy(inodes[hello].name, "README");
        const char *msg = "ramfs ready\n";
        uint32_t n = str_len(msg);
        memcpy(inodes[hello].data, msg, n);
        inodes[hello].size = n;
    }

    int rc = ext4_mount();
    ext4_ready = (rc == 0);
    if (!ext4_ready) {
        printf("ext4 mount failed rc=%d\n", rc);
    } else {
        printf("ext4 mount ok\n");
    }
    fs_initialized = true;
}

int fs_open(const char *path, int flags)
{
    char name[RAMFS_NAME_MAX];
    if (normalize_name(path, name) < 0)
        return -1;

    int inode = find_inode_by_name(name);
    int mode = flags & 0x3;

    if (inode < 0 && mode == O_RDONLY && !(flags & O_CREAT) && ext4_ready) {
        int ext_index = ext4_lookup(name);
        if (ext_index >= 0) {
            int fd = alloc_fd(current_proc);
            if (fd < 0)
                return -1;
            current_proc->fds[fd].used = 1;
            current_proc->fds[fd].inode = -ext_index - 1;
            current_proc->fds[fd].offset = 0;
            current_proc->fds[fd].flags = flags;
            return fd;
        }
    }

    if (inode < 0) {
        if (!(flags & O_CREAT))
            return -1;
        inode = alloc_inode();
        if (inode < 0)
            return -1;
        memset(&inodes[inode], 0, sizeof(inodes[inode]));
        inodes[inode].used = 1;
        str_copy(inodes[inode].name, name);
    }

    if (flags & O_TRUNC)
        inodes[inode].size = 0;

    int fd = alloc_fd(current_proc);
    if (fd < 0)
        return -1;

    current_proc->fds[fd].used = 1;
    current_proc->fds[fd].inode = inode;
    current_proc->fds[fd].flags = flags;
    current_proc->fds[fd].offset = (flags & O_APPEND) ? inodes[inode].size : 0;
    return fd;
}

int fs_close(int fd)
{
    if (fd < 0 || fd >= FD_MAX)
        return -1;
    if (!current_proc->fds[fd].used)
        return -1;
    memset(&current_proc->fds[fd], 0, sizeof(current_proc->fds[fd]));
    return 0;
}

int fs_read(int fd, void *buf, uint32_t len)
{
    if (fd < 0 || fd >= FD_MAX || !buf)
        return -1;
    struct file_desc *d = &current_proc->fds[fd];
    if (!d->used)
        return -1;
    if ((d->flags & 0x3) == O_WRONLY)
        return -1;

    if (is_ext_fd(d)) {
        int ext_idx = -d->inode - 1;
        int n = ext4_read_file(ext_idx, d->offset, buf, len);
        if (n > 0)
            d->offset += (uint32_t) n;
        return n;
    }

    struct ramfs_inode *inode = &inodes[d->inode];
    if (d->offset >= inode->size)
        return 0;

    uint32_t remain = inode->size - d->offset;
    uint32_t n = (len < remain) ? len : remain;
    memcpy(buf, &inode->data[d->offset], n);
    d->offset += n;
    return (int) n;
}

int fs_write(int fd, const void *buf, uint32_t len)
{
    if (fd < 0 || fd >= FD_MAX || !buf)
        return -1;
    struct file_desc *d = &current_proc->fds[fd];
    if (!d->used)
        return -1;
    if (is_ext_fd(d))
        return -1;

    int mode = d->flags & 0x3;
    if (!(mode == O_WRONLY || mode == O_RDWR))
        return -1;

    struct ramfs_inode *inode = &inodes[d->inode];
    if (d->flags & O_APPEND)
        d->offset = inode->size;

    if (d->offset >= RAMFS_DATA_MAX)
        return 0;

    uint32_t cap = RAMFS_DATA_MAX - d->offset;
    uint32_t n = (len < cap) ? len : cap;
    memcpy(&inode->data[d->offset], buf, n);
    d->offset += n;
    if (d->offset > inode->size)
        inode->size = d->offset;
    return (int) n;
}

int fs_unlink(const char *path)
{
    char name[RAMFS_NAME_MAX];
    if (normalize_name(path, name) < 0)
        return -1;

    int inode = find_inode_by_name(name);
    if (inode < 0)
        return -1;

    for (int p = 0; p < PROC_MAX; p++) {
        for (int fd = 0; fd < FD_MAX; fd++) {
            if (procs[p].fds[fd].used && procs[p].fds[fd].inode == inode)
                procs[p].fds[fd].used = 0;
        }
    }

    memset(&inodes[inode], 0, sizeof(inodes[inode]));
    return 0;
}

int fs_rename(const char *old_path, const char *new_path)
{
    char old_name[RAMFS_NAME_MAX];
    char new_name[RAMFS_NAME_MAX];
    if (normalize_name(old_path, old_name) < 0 || normalize_name(new_path, new_name) < 0)
        return -1;
    if (str_eq(old_name, new_name))
        return 0;

    int inode = find_inode_by_name(old_name);
    if (inode < 0)
        return -1;
    if (find_inode_by_name(new_name) >= 0)
        return -1;

    str_copy(inodes[inode].name, new_name);
    return 0;
}

int fs_listdir(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return -1;

    uint32_t out = 0;

    if (ext4_ready) {
        int n = ext4_listdir(buf, len);
        if (n > 0)
            out = (uint32_t) n;
    }

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!inodes[i].used)
            continue;

        uint32_t n = str_len(inodes[i].name);
        if (out + n + 1 >= len)
            break;
        memcpy(&buf[out], inodes[i].name, n);
        out += n;
        buf[out++] = '\n';
    }

    if (out < len)
        buf[out] = '\0';
    return (int) out;
}
