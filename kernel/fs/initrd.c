#include "initrd.h"
#include "vfs.h"
#include "string.h"

static void prefix_sys(char *out, size_t n, const char *path)
{
    size_t i = 0, j = 0;
    const char *pref = "/sys";
    while (pref[i] && j + 1 < n)
        out[j++] = pref[i++];
    if (!path || !path[0]) {
        out[j] = 0;
        return;
    }
    if (path[0] != '/' && j + 1 < n)
        out[j++] = '/';
    i = 0;
    while (path[i] && j + 1 < n)
        out[j++] = path[i++];
    out[j] = 0;
}

void initrd_unpack(const uint8_t *data, uint32_t size)
{
    uint32_t off = 0;
    while (off + 8 <= size) {
        uint32_t path_len;
        uint32_t file_len;
        char path[VFS_PATH_MAX];
        char full[VFS_PATH_MAX];
        kmemcpy(&path_len, data + off, 4);
        kmemcpy(&file_len, data + off + 4, 4);
        off += 8;
        if (path_len == 0 || path_len >= VFS_PATH_MAX || off + path_len > size)
            break;
        if (file_len != 0xFFFFFFFFu && off + path_len + file_len > size)
            break;
        kmemcpy(path, data + off, path_len);
        path[path_len] = 0;
        off += path_len;
        prefix_sys(full, sizeof(full), path);
        if (file_len == 0xFFFFFFFFu) {
            vfs_mkdir(full);
        } else {
            char parent[VFS_PATH_MAX];
            size_t i, slash = 0;
            kstrncpy(parent, full, sizeof(parent));
            for (i = 0; parent[i]; i++)
                if (parent[i] == '/')
                    slash = i;
            if (slash > 0) {
                parent[slash] = 0;
                if (!vfs_exists(parent)) {
                    char tmp[VFS_PATH_MAX];
                    size_t j = 0;
                    tmp[0] = 0;
                    for (i = 1; full[i]; i++) {
                        if (full[i] == '/') {
                            tmp[j] = 0;
                            if (j > 0 && !vfs_exists(tmp))
                                vfs_mkdir(tmp);
                        }
                        tmp[j++] = full[i];
                        if (j + 1 >= sizeof(tmp))
                            break;
                    }
                }
            }
            vfs_write_file(full, data + off, file_len);
            off += file_len;
        }
    }
}
