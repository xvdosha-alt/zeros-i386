#include "vfs.h"
#include "mm.h"
#include "string.h"
#include "fat.h"

static VfsNode nodes[VFS_MAX_NODES];
static VfsFd fds[VFS_MAX_FD];
static VfsNode *root;

static VfsNode *alloc_node(void)
{
    int i;
    for (i = 0; i < VFS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            kmemset(&nodes[i], 0, sizeof(nodes[i]));
            nodes[i].used = 1;
            return &nodes[i];
        }
    }
    return 0;
}

static int alloc_fd(void)
{
    int i;
    for (i = 3; i < VFS_MAX_FD; i++) {
        if (!fds[i].used)
            return i;
    }
    return VFS_ERR_NOSPC;
}

void vfs_normalize(char *out, const char *in, size_t n)
{
    char parts[32][VFS_NAME_MAX];
    int np = 0;
    size_t i = 0, j;

    if (!in || !in[0]) {
        out[0] = '/';
        out[1] = 0;
        return;
    }
    if (in[0] == '/')
        i = 1;
    while (in[i] && np < 32) {
        j = 0;
        while (in[i] && in[i] != '/' && j + 1 < sizeof(parts[0]))
            parts[np][j++] = in[i++];
        parts[np][j] = 0;
        while (in[i] == '/')
            i++;
        if (!parts[np][0] || !kstrcmp(parts[np], "."))
            continue;
        if (!kstrcmp(parts[np], "..")) {
            if (np > 0)
                np--;
            continue;
        }
        np++;
    }
    if (np == 0) {
        out[0] = '/';
        out[1] = 0;
        return;
    }
    j = 0;
    for (i = 0; i < (size_t)np && j + 1 < n; i++) {
        size_t L = kstrlen(parts[i]);
        out[j++] = '/';
        if (j + L >= n)
            break;
        kmemcpy(out + j, parts[i], L);
        j += L;
    }
    out[j] = 0;
}

static VfsNode *find_child(VfsNode *dir, const char *name)
{
    int i;
    for (i = 0; i < dir->nchildren; i++) {
        if (dir->children[i] && !kstrcmp(dir->children[i]->name, name))
            return dir->children[i];
    }
    return 0;
}

VfsNode *vfs_lookup(const char *path)
{
    char norm[VFS_PATH_MAX];
    char part[VFS_NAME_MAX];
    VfsNode *cur = root;
    size_t i = 0, j;
    vfs_normalize(norm, path, sizeof(norm));
    if (!kstrcmp(norm, "/"))
        return root;
    i = 1;
    while (norm[i]) {
        j = 0;
        while (norm[i] && norm[i] != '/' && j + 1 < sizeof(part))
            part[j++] = norm[i++];
        part[j] = 0;
        if (norm[i] == '/')
            i++;
        if (!part[0])
            continue;
        if (!cur->is_dir)
            return 0;
        cur = find_child(cur, part);
        if (!cur)
            return 0;
    }
    return cur;
}

static int ensure_parent(const char *path, char *name_out, VfsNode **parent_out)
{
    char norm[VFS_PATH_MAX];
    char parent_path[VFS_PATH_MAX];
    size_t len, i, slash = 0;
    vfs_normalize(norm, path, sizeof(norm));
    len = kstrlen(norm);
    if (len <= 1)
        return VFS_ERR_INVAL;
    for (i = 1; i < len; i++)
        if (norm[i] == '/')
            slash = i;
    if (slash == 0) {
        kstrncpy(parent_path, "/", sizeof(parent_path));
        kstrncpy(name_out, norm + 1, VFS_NAME_MAX);
    } else {
        kmemcpy(parent_path, norm, slash);
        parent_path[slash] = 0;
        kstrncpy(name_out, norm + slash + 1, VFS_NAME_MAX);
    }
    *parent_out = vfs_lookup(parent_path);
    if (!*parent_out || !(*parent_out)->is_dir)
        return VFS_ERR_NOENT;
    if (!name_out[0] || kstrlen(name_out) >= VFS_NAME_MAX)
        return VFS_ERR_NAMETOOLONG;
    return VFS_OK;
}

void vfs_init(void)
{
    kmemset(nodes, 0, sizeof(nodes));
    kmemset(fds, 0, sizeof(fds));
    root = alloc_node();
    root->is_dir = 1;
    kstrncpy(root->name, "/", VFS_NAME_MAX);
    /* Root is a device/mount table; real filesystems hang underneath. */
    vfs_mkdir("/sys");
    vfs_mkdir("/disk");
}

int vfs_mkdir(const char *path)
{
    VfsNode *parent, *n;
    char name[VFS_NAME_MAX];
    int r;
    if (fat_is_path(path))
        return fat_mkdir(path);
    if (vfs_lookup(path))
        return VFS_ERR_EXIST;
    r = ensure_parent(path, name, &parent);
    if (r < 0)
        return r;
    if (parent->nchildren >= VFS_MAX_CHILDREN)
        return VFS_ERR_NOSPC;
    n = alloc_node();
    if (!n)
        return VFS_ERR_NOSPC;
    n->is_dir = 1;
    n->parent = parent;
    kstrncpy(n->name, name, VFS_NAME_MAX);
    parent->children[parent->nchildren++] = n;
    return VFS_OK;
}

int vfs_write_file(const char *path, const void *data, size_t n)
{
    VfsNode *node = vfs_lookup(path);
    VfsNode *parent;
    char name[VFS_NAME_MAX];
    int r;
    if (!node) {
        r = ensure_parent(path, name, &parent);
        if (r < 0)
            return r;
        if (parent->nchildren >= VFS_MAX_CHILDREN)
            return VFS_ERR_NOSPC;
        node = alloc_node();
        if (!node)
            return VFS_ERR_NOSPC;
        node->parent = parent;
        kstrncpy(node->name, name, VFS_NAME_MAX);
        parent->children[parent->nchildren++] = node;
    }
    if (node->is_dir)
        return VFS_ERR_ISDIR;
    if (n > VFS_FILE_MAX)
        return VFS_ERR_NOSPC;
    if (node->cap < n) {
        void *p = mm_alloc_pages((n + PAGE_SIZE - 1) / PAGE_SIZE);
        if (!p)
            return VFS_ERR_NOSPC;
        if (node->data)
            mm_free_pages(node->data, (node->cap + PAGE_SIZE - 1) / PAGE_SIZE);
        node->data = (uint8_t *)p;
        node->cap = (uint32_t)(((n + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE);
    }
    if (n)
        kmemcpy(node->data, data, n);
    node->size = (uint32_t)n;
    return VFS_OK;
}

int vfs_read_file(const char *path, void *buf, size_t buflen)
{
    VfsNode *node;
    size_t n;
    if (fat_is_path(path)) {
        int fd = fat_open(path, VFS_O_READ);
        int got;
        if (fd < 0)
            return fd;
        got = fat_read(fd, buf, buflen);
        fat_close(fd);
        return got;
    }
    node = vfs_lookup(path);
    if (!node)
        return VFS_ERR_NOENT;
    if (node->is_dir)
        return VFS_ERR_ISDIR;
    n = node->size;
    if (n > buflen)
        n = buflen;
    if (n)
        kmemcpy(buf, node->data, n);
    return (int)n;
}

int vfs_open(const char *path, int flags)
{
    VfsNode *node;
    int fd;
    if (fat_is_path(path))
        return fat_open(path, flags);
    node = vfs_lookup(path);
    if (!node) {
        if (!(flags & VFS_O_CREATE))
            return VFS_ERR_NOENT;
        if (vfs_write_file(path, "", 0) < 0)
            return VFS_ERR_NOSPC;
        node = vfs_lookup(path);
        if (!node)
            return VFS_ERR_NOENT;
    }
    if (node->is_dir && !(flags & VFS_O_DIR))
        return VFS_ERR_ISDIR;
    if ((flags & VFS_O_TRUNC) && (flags & VFS_O_WRITE) && !node->is_dir)
        node->size = 0;
    fd = alloc_fd();
    if (fd < 0)
        return fd;
    fds[fd].used = 1;
    fds[fd].node = node;
    fds[fd].offset = (flags & VFS_O_APPEND) ? node->size : 0;
    fds[fd].flags = flags;
    return fd;
}

int vfs_close(int fd)
{
    if (fd >= 100)
        return fat_close(fd);
    if (fd < 0 || fd >= VFS_MAX_FD || !fds[fd].used)
        return VFS_ERR_BADFD;
    fds[fd].used = 0;
    return VFS_OK;
}

int vfs_read(int fd, void *buf, size_t n)
{
    VfsFd *f;
    size_t avail;
    if (fd >= 100)
        return fat_read(fd, buf, n);
    if (fd < 0 || fd >= VFS_MAX_FD || !fds[fd].used)
        return VFS_ERR_BADFD;
    f = &fds[fd];
    if (!(f->flags & VFS_O_READ))
        return VFS_ERR_INVAL;
    if (f->node->is_dir)
        return VFS_ERR_ISDIR;
    if (f->offset >= f->node->size)
        return 0;
    avail = f->node->size - f->offset;
    if (n > avail)
        n = avail;
    kmemcpy(buf, f->node->data + f->offset, n);
    f->offset += (uint32_t)n;
    return (int)n;
}

int vfs_write(int fd, const void *buf, size_t n)
{
    VfsFd *f;
    uint32_t need;
    if (fd >= 100)
        return fat_write(fd, buf, n);
    if (fd < 0 || fd >= VFS_MAX_FD || !fds[fd].used)
        return VFS_ERR_BADFD;
    f = &fds[fd];
    if (!(f->flags & VFS_O_WRITE))
        return VFS_ERR_INVAL;
    if (f->node->is_dir)
        return VFS_ERR_ISDIR;
    need = f->offset + (uint32_t)n;
    if (need > VFS_FILE_MAX)
        return VFS_ERR_NOSPC;
    if (need > f->node->cap) {
        uint32_t ncap = ((need + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
        void *p = mm_alloc_pages(ncap / PAGE_SIZE);
        if (!p)
            return VFS_ERR_NOSPC;
        if (f->node->data && f->node->size)
            kmemcpy(p, f->node->data, f->node->size);
        if (f->node->data)
            mm_free_pages(f->node->data, f->node->cap / PAGE_SIZE);
        f->node->data = (uint8_t *)p;
        f->node->cap = ncap;
    }
    kmemcpy(f->node->data + f->offset, buf, n);
    f->offset += (uint32_t)n;
    if (f->offset > f->node->size)
        f->node->size = f->offset;
    return (int)n;
}

int vfs_unlink(const char *path)
{
    VfsNode *node;
    VfsNode *p;
    int i, j;
    if (fat_is_path(path))
        return fat_unlink(path);
    node = vfs_lookup(path);
    if (!node || node == root)
        return VFS_ERR_NOENT;
    /* Mount points stay; wiping them removes the whole userland. */
    if (node->parent == root &&
        (!kstrcmp(node->name, "sys") || !kstrcmp(node->name, "disk")))
        return VFS_ERR_INVAL;
    if (node->is_dir && node->nchildren)
        return VFS_ERR_INVAL;
    p = node->parent;
    if (!p)
        return VFS_ERR_INVAL;
    for (i = 0; i < p->nchildren; i++) {
        if (p->children[i] == node) {
            for (j = i; j + 1 < p->nchildren; j++)
                p->children[j] = p->children[j + 1];
            p->nchildren--;
            break;
        }
    }
    if (node->data)
        mm_free_pages(node->data, (node->cap + PAGE_SIZE - 1) / PAGE_SIZE);
    node->used = 0;
    return VFS_OK;
}

int vfs_exists(const char *path)
{
    if (fat_is_path(path))
        return fat_exists(path);
    return vfs_lookup(path) != 0;
}

int vfs_isdir(const char *path)
{
    VfsNode *n;
    if (fat_is_path(path))
        return fat_isdir(path);
    n = vfs_lookup(path);
    return n && n->is_dir;
}

int vfs_size(const char *path)
{
    VfsNode *n;
    if (fat_is_path(path)) {
        if (fat_isdir(path))
            return VFS_ERR_ISDIR;
        if (!fat_exists(path))
            return VFS_ERR_NOENT;
        return (int)fat_file_size(path);
    }
    n = vfs_lookup(path);
    if (!n)
        return VFS_ERR_NOENT;
    if (n->is_dir)
        return VFS_ERR_ISDIR;
    return (int)n->size;
}

int vfs_listdir(const char *path, char *buf, size_t buflen)
{
    VfsNode *n;
    size_t pos = 0;
    int i;
    if (fat_is_path(path))
        return fat_listdir(path, buf, buflen);
    n = vfs_lookup(path);
    if (!n)
        return VFS_ERR_NOENT;
    if (!n->is_dir)
        return VFS_ERR_NOTDIR;
    buf[0] = 0;
    for (i = 0; i < n->nchildren; i++) {
        size_t len = kstrlen(n->children[i]->name);
        if (pos + len + 2 > buflen)
            break;
        kmemcpy(buf + pos, n->children[i]->name, len);
        pos += len;
        buf[pos++] = '\n';
        buf[pos] = 0;
    }
    return (int)pos;
}
