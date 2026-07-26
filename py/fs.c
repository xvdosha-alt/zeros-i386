#include "fs.h"
#include "util.h"

static MpfsSuper fs;
static MpfsFd fds[MPFS_MAX_FD];

static int name_ok(const char *path)
{
    size_t n;
    if (!path || !path[0])
        return 0;
    if (path[0] == '/')
        path++;
    n = mp_strlen(path);
    if (n == 0 || n >= MPFS_NAME_MAX)
        return 0;
    return 1;
}

static const char *norm_name(const char *path)
{
    if (path && path[0] == '/')
        return path + 1;
    return path;
}

static int find_inode(const char *path)
{
    const char *name = norm_name(path);
    int i;
    if (!name_ok(path))
        return -1;
    for (i = 0; i < MPFS_MAX_FILES; i++) {
        if (fs.files[i].used && mp_strcmp(fs.files[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int alloc_inode(void)
{
    int i;
    for (i = 0; i < MPFS_MAX_FILES; i++) {
        if (!fs.files[i].used)
            return i;
    }
    return -1;
}

static int alloc_fd(void)
{
    int i;
    for (i = 0; i < MPFS_MAX_FD; i++) {
        if (!fds[i].used)
            return i;
    }
    return -1;
}

void mpfs_format(void)
{
    int i;
    mp_memset(&fs, 0, sizeof(fs));
    fs.magic = MPFS_MAGIC;
    fs.version = MPFS_VERSION;
    fs.nfiles = 0;
    for (i = 0; i < MPFS_MAX_FD; i++)
        fds[i].used = 0;
}

void mpfs_init(void)
{
    if (fs.magic != MPFS_MAGIC || fs.version != MPFS_VERSION)
        mpfs_format();
}

const char *mpfs_strerror(int err)
{
    switch (err) {
    case MPFS_OK: return "ok";
    case MPFS_ERR_NOENT: return "No such file";
    case MPFS_ERR_EXIST: return "File exists";
    case MPFS_ERR_NOSPC: return "No space left";
    case MPFS_ERR_INVAL: return "Invalid argument";
    case MPFS_ERR_BADFD: return "Bad file descriptor";
    case MPFS_ERR_PERM: return "Permission denied";
    case MPFS_ERR_NAMETOOLONG: return "File name too long";
    default: return "Unknown error";
    }
}

int mpfs_exists(const char *path)
{
    return find_inode(path) >= 0;
}

int mpfs_size(const char *path)
{
    int ino = find_inode(path);
    if (ino < 0)
        return MPFS_ERR_NOENT;
    return (int)fs.files[ino].size;
}

int mpfs_unlink(const char *path)
{
    int ino = find_inode(path);
    int i;
    if (ino < 0)
        return MPFS_ERR_NOENT;
    for (i = 0; i < MPFS_MAX_FD; i++) {
        if (fds[i].used && fds[i].inode == ino)
            return MPFS_ERR_PERM;
    }
    mp_memset(&fs.files[ino], 0, sizeof(fs.files[ino]));
    if (fs.nfiles > 0)
        fs.nfiles--;
    return MPFS_OK;
}

int mpfs_open(const char *path, int flags)
{
    int ino;
    int fd;
    const char *name;

    if (!name_ok(path))
        return MPFS_ERR_NAMETOOLONG;
    name = norm_name(path);
    ino = find_inode(path);

    if (ino < 0) {
        if (!(flags & MPFS_O_CREATE))
            return MPFS_ERR_NOENT;
        ino = alloc_inode();
        if (ino < 0)
            return MPFS_ERR_NOSPC;
        mp_memset(&fs.files[ino], 0, sizeof(fs.files[ino]));
        mp_strncpy(fs.files[ino].name, name, MPFS_NAME_MAX);
        fs.files[ino].used = 1;
        fs.files[ino].size = 0;
        fs.nfiles++;
    }

    if ((flags & MPFS_O_TRUNC) && (flags & MPFS_O_WRITE))
        fs.files[ino].size = 0;

    fd = alloc_fd();
    if (fd < 0)
        return MPFS_ERR_NOSPC;

    fds[fd].used = 1;
    fds[fd].inode = ino;
    fds[fd].flags = flags;
    if (flags & MPFS_O_APPEND)
        fds[fd].offset = fs.files[ino].size;
    else
        fds[fd].offset = 0;
    return fd;
}

int mpfs_close(int fd)
{
    if (fd < 0 || fd >= MPFS_MAX_FD)
        return MPFS_ERR_BADFD;
    fds[fd].used = 0;
    return MPFS_OK;
}

int mpfs_seek(int fd, int32_t off)
{
    MpfsInode *node;
    if (fd < 0 || fd >= MPFS_MAX_FD || !fds[fd].used)
        return MPFS_ERR_BADFD;
    if (off < 0)
        return MPFS_ERR_INVAL;
    node = &fs.files[fds[fd].inode];
    if ((uint32_t)off > node->size)
        return MPFS_ERR_INVAL;
    fds[fd].offset = (uint32_t)off;
    return MPFS_OK;
}

int mpfs_read(int fd, void *buf, size_t n)
{
    MpfsInode *node;
    size_t avail;
    if (fd < 0 || fd >= MPFS_MAX_FD || !fds[fd].used)
        return MPFS_ERR_BADFD;
    if (!(fds[fd].flags & MPFS_O_READ))
        return MPFS_ERR_PERM;
    node = &fs.files[fds[fd].inode];
    if (fds[fd].offset >= node->size)
        return 0;
    avail = node->size - fds[fd].offset;
    if (n > avail)
        n = avail;
    mp_memcpy(buf, node->data + fds[fd].offset, n);
    fds[fd].offset += (uint32_t)n;
    return (int)n;
}

int mpfs_write(int fd, const void *buf, size_t n)
{
    MpfsInode *node;
    size_t room;
    if (fd < 0 || fd >= MPFS_MAX_FD || !fds[fd].used)
        return MPFS_ERR_BADFD;
    if (!(fds[fd].flags & MPFS_O_WRITE))
        return MPFS_ERR_PERM;
    node = &fs.files[fds[fd].inode];
    if (fds[fd].flags & MPFS_O_APPEND)
        fds[fd].offset = node->size;
    if (fds[fd].offset > MPFS_FILE_MAX)
        return MPFS_ERR_NOSPC;
    room = MPFS_FILE_MAX - fds[fd].offset;
    if (n > room)
        return MPFS_ERR_NOSPC;
    mp_memcpy(node->data + fds[fd].offset, buf, n);
    fds[fd].offset += (uint32_t)n;
    if (fds[fd].offset > node->size)
        node->size = fds[fd].offset;
    return (int)n;
}

int mpfs_listdir(char *buf, size_t buflen)
{
    size_t pos = 0;
    int i;
    if (!buf || buflen == 0)
        return MPFS_ERR_INVAL;
    buf[0] = 0;
    for (i = 0; i < MPFS_MAX_FILES; i++) {
        size_t n;
        if (!fs.files[i].used)
            continue;
        n = mp_strlen(fs.files[i].name);
        if (pos + n + 2 > buflen)
            return MPFS_ERR_NOSPC;
        mp_memcpy(buf + pos, fs.files[i].name, n);
        pos += n;
        buf[pos++] = '\n';
        buf[pos] = 0;
    }
    if (pos > 0 && buf[pos - 1] == '\n') {
        pos--;
        buf[pos] = 0;
    }
    return (int)pos;
}
