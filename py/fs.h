#ifndef MP_FS_H
#define MP_FS_H

#include "config.h"

#define MPFS_MAGIC 0x5346504Du
#define MPFS_VERSION 1
#define MPFS_MAX_FILES MPFS_MAX_FILES_CFG
#define MPFS_NAME_MAX 28
#define MPFS_FILE_MAX MPFS_FILE_MAX_CFG
#define MPFS_MAX_FD 8
#define MPFS_O_READ 1
#define MPFS_O_WRITE 2
#define MPFS_O_APPEND 4
#define MPFS_O_TRUNC 8
#define MPFS_O_CREATE 16

typedef struct {
    char name[MPFS_NAME_MAX];
    uint32_t size;
    uint8_t used;
    uint8_t pad[3];
    uint8_t data[MPFS_FILE_MAX];
} MpfsInode;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t nfiles;
    uint32_t pad;
    MpfsInode files[MPFS_MAX_FILES];
} MpfsSuper;

typedef struct {
    int used;
    int inode;
    uint32_t offset;
    int flags;
} MpfsFd;

void mpfs_init(void);
void mpfs_format(void);

int mpfs_open(const char *path, int flags);
int mpfs_close(int fd);
int mpfs_read(int fd, void *buf, size_t n);
int mpfs_write(int fd, const void *buf, size_t n);
int mpfs_seek(int fd, int32_t off);
int mpfs_unlink(const char *path);
int mpfs_exists(const char *path);
int mpfs_size(const char *path);
int mpfs_listdir(char *buf, size_t buflen);

const char *mpfs_strerror(int err);

enum {
    MPFS_OK = 0,
    MPFS_ERR_NOENT = -1,
    MPFS_ERR_EXIST = -2,
    MPFS_ERR_NOSPC = -3,
    MPFS_ERR_INVAL = -4,
    MPFS_ERR_BADFD = -5,
    MPFS_ERR_ISDIR = -6,
    MPFS_ERR_PERM = -7,
    MPFS_ERR_NAMETOOLONG = -8
};

#endif
