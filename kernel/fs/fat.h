#ifndef KERNEL_FAT_H
#define KERNEL_FAT_H

#include "types.h"

int fat_init(void);
int fat_mounted(void);
int fat_is_path(const char *path);
int fat_open(const char *path, int flags);
int fat_close(int fd);
int fat_read(int fd, void *buf, size_t n);
int fat_write(int fd, const void *buf, size_t n);
int fat_listdir(const char *path, char *buf, size_t buflen);
int fat_mkdir(const char *path);
int fat_exists(const char *path);
int fat_isdir(const char *path);
int fat_unlink(const char *path);

uint32_t fat_file_size(const char *path);

#endif
