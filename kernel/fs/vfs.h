#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include "types.h"

#define VFS_NAME_MAX 48
#define VFS_PATH_MAX 128
#define VFS_MAX_NODES 256
#define VFS_FILE_MAX (64 * 1024)
#define VFS_MAX_FD 32
#define VFS_MAX_CHILDREN 128

#define VFS_O_READ 1
#define VFS_O_WRITE 2
#define VFS_O_APPEND 4
#define VFS_O_TRUNC 8
#define VFS_O_CREATE 16
#define VFS_O_DIR 32

enum {
    VFS_OK = 0,
    VFS_ERR_NOENT = -1,
    VFS_ERR_EXIST = -2,
    VFS_ERR_NOSPC = -3,
    VFS_ERR_INVAL = -4,
    VFS_ERR_BADFD = -5,
    VFS_ERR_ISDIR = -6,
    VFS_ERR_NOTDIR = -7,
    VFS_ERR_NAMETOOLONG = -8
};

typedef struct VfsNode {
    char name[VFS_NAME_MAX];
    int is_dir;
    uint32_t size;
    uint8_t *data;
    uint32_t cap;
    struct VfsNode *parent;
    struct VfsNode *children[VFS_MAX_CHILDREN];
    int nchildren;
    int used;
} VfsNode;

typedef struct {
    int used;
    VfsNode *node;
    uint32_t offset;
    int flags;
} VfsFd;

void vfs_init(void);
void vfs_normalize(char *out, const char *in, size_t n);
int vfs_mkdir(const char *path);
int vfs_open(const char *path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, size_t n);
int vfs_write(int fd, const void *buf, size_t n);
int vfs_unlink(const char *path);
int vfs_exists(const char *path);
int vfs_isdir(const char *path);
int vfs_size(const char *path);
int vfs_listdir(const char *path, char *buf, size_t buflen);
int vfs_write_file(const char *path, const void *data, size_t n);
int vfs_read_file(const char *path, void *buf, size_t buflen);
VfsNode *vfs_lookup(const char *path);

#endif
