#include "fat.h"
#include "ata.h"
#include "mm.h"
#include "string.h"
#include "tty.h"
#include "vfs.h"

#define FAT_FD_BASE 100
#define FAT_MAX_FD 8
#define FAT_SEC 512

typedef struct {
    uint16_t bytes_per_sec;
    uint8_t sec_per_clus;
    uint16_t reserved;
    uint8_t fats;
    uint16_t root_ents;
    uint16_t tot_sec16;
    uint16_t fatsz16;
    uint32_t tot_sec32;
    uint32_t fatsz32;
    uint32_t root_clus;
    int is_fat32;
    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;
    uint32_t root_secs;
    uint32_t clusters;
} FatFs;

typedef struct {
    int used;
    int flags;
    int is_dir;
    uint32_t start;
    uint32_t size;
    uint32_t pos;
    uint32_t dir_sec;
    uint32_t dir_off;
} FatFd;

static FatFs fs;
static FatFd fdt[FAT_MAX_FD];
static int mounted;
static uint8_t secbuf[FAT_SEC];

static int read_sec(uint32_t lba)
{
    return ata_read(lba, secbuf, 1);
}

static int write_sec(uint32_t lba)
{
    return ata_write(lba, secbuf, 1);
}

static uint32_t clus_to_lba(uint32_t cl)
{
    return fs.data_start + (cl - 2) * fs.sec_per_clus;
}

static uint32_t clus_from_ent(const uint8_t *e)
{
    uint32_t lo = (uint32_t)e[26] | ((uint32_t)e[27] << 8);
    if (fs.is_fat32) {
        uint32_t hi = (uint32_t)e[20] | ((uint32_t)e[21] << 8);
        return (hi << 16) | lo;
    }
    return lo;
}

static void clus_to_ent(uint8_t *e, uint32_t cl)
{
    e[26] = (uint8_t)(cl & 0xFF);
    e[27] = (uint8_t)((cl >> 8) & 0xFF);
    if (fs.is_fat32) {
        e[20] = (uint8_t)((cl >> 16) & 0xFF);
        e[21] = (uint8_t)((cl >> 24) & 0xFF);
    }
}

static uint32_t fat_get(uint32_t cl)
{
    uint32_t off, lba, ent;
    if (fs.is_fat32) {
        off = cl * 4;
        lba = fs.fat_start + off / FAT_SEC;
        if (read_sec(lba) < 0)
            return 0x0FFFFFFF;
        ent = *(uint32_t *)(secbuf + (off % FAT_SEC));
        return ent & 0x0FFFFFFF;
    }
    off = cl * 2;
    lba = fs.fat_start + off / FAT_SEC;
    if (read_sec(lba) < 0)
        return 0xFFFF;
    return *(uint16_t *)(secbuf + (off % FAT_SEC));
}

static int fat_set(uint32_t cl, uint32_t val)
{
    uint32_t off, lba, fatsz, i;
    fatsz = fs.is_fat32 ? fs.fatsz32 : fs.fatsz16;
    if (fs.is_fat32) {
        off = cl * 4;
        for (i = 0; i < fs.fats; i++) {
            lba = fs.fat_start + i * fatsz + off / FAT_SEC;
            if (read_sec(lba) < 0)
                return -1;
            *(uint32_t *)(secbuf + (off % FAT_SEC)) =
                (*(uint32_t *)(secbuf + (off % FAT_SEC)) & 0xF0000000) | (val & 0x0FFFFFFF);
            if (write_sec(lba) < 0)
                return -1;
        }
        return 0;
    }
    off = cl * 2;
    for (i = 0; i < fs.fats; i++) {
        lba = fs.fat_start + i * fatsz + off / FAT_SEC;
        if (read_sec(lba) < 0)
            return -1;
        *(uint16_t *)(secbuf + (off % FAT_SEC)) = (uint16_t)val;
        if (write_sec(lba) < 0)
            return -1;
    }
    return 0;
}

static int is_eoc(uint32_t v)
{
    if (fs.is_fat32)
        return v >= 0x0FFFFFF8;
    return v >= 0xFFF8;
}

static void name83(const char *src, char *out)
{
    int i, j = 0;
    char base[9], ext[4];
    kmemset(base, ' ', 8);
    kmemset(ext, ' ', 3);
    base[8] = 0;
    ext[3] = 0;
    for (i = 0; src[i] && src[i] != '.' && j < 8; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        base[j++] = c;
    }
    if (src[i] == '.') {
        i++;
        j = 0;
        for (; src[i] && j < 3; i++) {
            char c = src[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            ext[j++] = c;
        }
    }
    kmemcpy(out, base, 8);
    kmemcpy(out + 8, ext, 3);
}

static void decode83(const uint8_t *e, char *out, size_t n)
{
    int i, p = 0;
    for (i = 0; i < 8 && e[i] != ' '; i++) {
        char c = (char)e[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (p + 1 < (int)n)
            out[p++] = c;
    }
    if (e[8] != ' ') {
        if (p + 1 < (int)n)
            out[p++] = '.';
        for (i = 8; i < 11 && e[i] != ' '; i++) {
            char c = (char)e[i];
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            if (p + 1 < (int)n)
                out[p++] = c;
        }
    }
    out[p] = 0;
}

static int next_component(const char **path, char *comp, size_t n)
{
    const char *p = *path;
    size_t i = 0;
    while (*p == '/')
        p++;
    if (!*p) {
        *path = p;
        return 0;
    }
    while (*p && *p != '/' && i + 1 < n)
        comp[i++] = *p++;
    comp[i] = 0;
    *path = p;
    return 1;
}

static int find_in_dir(uint32_t dir_clus, const char *name, uint8_t *ent_out,
                       uint32_t *sec_out, uint32_t *off_out)
{
    char want[11];
    uint32_t cl, sec, i, root_left;
    name83(name, want);
    if (!fs.is_fat32 && dir_clus == 0) {
        root_left = fs.root_secs;
        for (sec = fs.root_start; root_left; sec++, root_left--) {
            if (read_sec(sec) < 0)
                return -1;
            for (i = 0; i < FAT_SEC; i += 32) {
                uint8_t *e = secbuf + i;
                if (e[0] == 0)
                    return 0;
                if (e[0] == 0xE5)
                    continue;
                if (e[11] == 0x0F)
                    continue;
                if (!kmemcmp(e, want, 11)) {
                    if (ent_out)
                        kmemcpy(ent_out, e, 32);
                    if (sec_out)
                        *sec_out = sec;
                    if (off_out)
                        *off_out = i;
                    return 1;
                }
            }
        }
        return 0;
    }
    cl = dir_clus ? dir_clus : fs.root_clus;
    while (!is_eoc(cl) && cl >= 2) {
        uint32_t s;
        for (s = 0; s < fs.sec_per_clus; s++) {
            if (read_sec(clus_to_lba(cl) + s) < 0)
                return -1;
            for (i = 0; i < FAT_SEC; i += 32) {
                uint8_t *e = secbuf + i;
                if (e[0] == 0)
                    return 0;
                if (e[0] == 0xE5)
                    continue;
                if (e[11] == 0x0F)
                    continue;
                if (!kmemcmp(e, want, 11)) {
                    if (ent_out)
                        kmemcpy(ent_out, e, 32);
                    if (sec_out)
                        *sec_out = clus_to_lba(cl) + s;
                    if (off_out)
                        *off_out = i;
                    return 1;
                }
            }
        }
        cl = fat_get(cl);
    }
    return 0;
}

static int resolve(const char *path, uint8_t *ent, uint32_t *dir_clus_out)
{
    char comp[16];
    const char *p = path;
    uint32_t dir = fs.is_fat32 ? fs.root_clus : 0;
    uint8_t cur[32];
    int found = 0;
    if (kstrcmp(path, "/disk") == 0 || kstrncmp(path, "/disk/", 6) == 0)
        p = path + 5;
    if (dir_clus_out)
        *dir_clus_out = dir;
    while (next_component(&p, comp, sizeof(comp))) {
        const char *peek = p;
        found = find_in_dir(dir, comp, cur, 0, 0);
        if (found <= 0)
            return -1;
        if (cur[11] & 0x10) {
            
            dir = clus_from_ent(cur);
            if (ent)
                kmemcpy(ent, cur, 32);
            while (*peek == '/')
                peek++;
            if (!*peek) {
                
                if (dir_clus_out)
                    *dir_clus_out = dir;
                return 2;
            }
            if (dir_clus_out)
                *dir_clus_out = dir;
            continue;
        }
        while (*peek == '/')
            peek++;
        if (*peek)
            return -1;
        if (dir_clus_out)
            *dir_clus_out = dir;
        if (ent)
            kmemcpy(ent, cur, 32);
        return 1;
    }
    
    if (dir_clus_out)
        *dir_clus_out = dir;
    if (ent) {
        kmemset(ent, 0, 32);
        ent[11] = 0x10;
        clus_to_ent(ent, dir);
    }
    return 2;
}

static uint32_t next_free_clus = 2;

static uint32_t max_clus(void)
{
    uint32_t tot = fs.tot_sec16 ? fs.tot_sec16 : fs.tot_sec32;
    uint32_t data = tot - fs.data_start;
    return 2 + data / fs.sec_per_clus;
}

static uint32_t alloc_cluster(void)
{
    uint32_t c, last = max_clus();
    uint32_t start = next_free_clus;
    if (start < 2 || start >= last)
        start = 2;
    for (c = start; c < last; c++) {
        if (fat_get(c) == 0) {
            if (fat_set(c, fs.is_fat32 ? 0x0FFFFFFF : 0xFFFF) < 0)
                return 0;
            next_free_clus = c + 1;
            return c;
        }
    }
    for (c = 2; c < start; c++) {
        if (fat_get(c) == 0) {
            if (fat_set(c, fs.is_fat32 ? 0x0FFFFFFF : 0xFFFF) < 0)
                return 0;
            next_free_clus = c + 1;
            return c;
        }
    }
    return 0;
}

static int zero_cluster(uint32_t cl)
{
    uint32_t s;
    kmemset(secbuf, 0, FAT_SEC);
    for (s = 0; s < fs.sec_per_clus; s++) {
        if (write_sec(clus_to_lba(cl) + s) < 0)
            return -1;
    }
    return 0;
}

static int find_free_slot(uint32_t dir_clus, uint32_t *sec_out, uint32_t *off_out)
{
    uint32_t cl, sec, i, root_left;
    if (!fs.is_fat32 && dir_clus == 0) {
        root_left = fs.root_secs;
        for (sec = fs.root_start; root_left; sec++, root_left--) {
            if (read_sec(sec) < 0)
                return -1;
            for (i = 0; i < FAT_SEC; i += 32) {
                uint8_t *e = secbuf + i;
                if (e[0] == 0 || e[0] == 0xE5) {
                    *sec_out = sec;
                    *off_out = i;
                    return 0;
                }
            }
        }
        return -1;
    }
    cl = dir_clus ? dir_clus : fs.root_clus;
    while (!is_eoc(cl) && cl >= 2) {
        uint32_t s;
        for (s = 0; s < fs.sec_per_clus; s++) {
            if (read_sec(clus_to_lba(cl) + s) < 0)
                return -1;
            for (i = 0; i < FAT_SEC; i += 32) {
                uint8_t *e = secbuf + i;
                if (e[0] == 0 || e[0] == 0xE5) {
                    *sec_out = clus_to_lba(cl) + s;
                    *off_out = i;
                    return 0;
                }
            }
        }
        {
            uint32_t nxt = fat_get(cl);
            if (is_eoc(nxt) || nxt < 2) {
                uint32_t nc = alloc_cluster();
                if (!nc)
                    return -1;
                if (fat_set(cl, nc) < 0)
                    return -1;
                if (zero_cluster(nc) < 0)
                    return -1;
                cl = nc;
                continue;
            }
            cl = nxt;
        }
    }
    return -1;
}

static int parent_and_name(const char *path, uint32_t *dir_out, char *name, size_t nlen)
{
    char comp[16];
    const char *p = path;
    const char *last = 0;
    uint32_t dir = fs.is_fat32 ? fs.root_clus : 0;
    uint8_t cur[32];
    if (kstrcmp(path, "/disk") == 0 || kstrncmp(path, "/disk/", 6) == 0)
        p = path + 5;
    while (next_component(&p, comp, sizeof(comp))) {
        const char *save = p;
        while (*save == '/')
            save++;
        if (!*save) {
            last = 0;
            kstrncpy(name, comp, nlen);
            *dir_out = dir;
            return 0;
        }
        if (find_in_dir(dir, comp, cur, 0, 0) <= 0)
            return -1;
        if (!(cur[11] & 0x10))
            return -1;
        {
            dir = clus_from_ent(cur);
        }
        (void)last;
    }
    return -1;
}

static int create_entry(uint32_t dir_clus, const char *name, uint8_t attr,
                        uint32_t clus, uint32_t size,
                        uint32_t *sec_out, uint32_t *off_out)
{
    char n83[11];
    uint32_t sec, off;
    name83(name, n83);
    if (find_free_slot(dir_clus, &sec, &off) < 0)
        return -1;
    if (read_sec(sec) < 0)
        return -1;
    kmemset(secbuf + off, 0, 32);
    kmemcpy(secbuf + off, n83, 11);
    secbuf[off + 11] = attr;
    if (fs.is_fat32) {
        secbuf[off + 20] = (uint8_t)((clus >> 16) & 0xFF);
        secbuf[off + 21] = (uint8_t)((clus >> 24) & 0xFF);
    }
    secbuf[off + 26] = (uint8_t)(clus & 0xFF);
    secbuf[off + 27] = (uint8_t)((clus >> 8) & 0xFF);
    secbuf[off + 28] = (uint8_t)(size & 0xFF);
    secbuf[off + 29] = (uint8_t)((size >> 8) & 0xFF);
    secbuf[off + 30] = (uint8_t)((size >> 16) & 0xFF);
    secbuf[off + 31] = (uint8_t)((size >> 24) & 0xFF);
    if (write_sec(sec) < 0)
        return -1;
    if (sec_out)
        *sec_out = sec;
    if (off_out)
        *off_out = off;
    return 0;
}

static int update_dirent(FatFd *f)
{
    if (!f->dir_sec)
        return 0;
    if (read_sec(f->dir_sec) < 0)
        return -1;
    {
        uint8_t *e = secbuf + f->dir_off;
        clus_to_ent(e, f->start);
        e[28] = (uint8_t)(f->size & 0xFF);
        e[29] = (uint8_t)((f->size >> 8) & 0xFF);
        e[30] = (uint8_t)((f->size >> 16) & 0xFF);
        e[31] = (uint8_t)((f->size >> 24) & 0xFF);
    }
    return write_sec(f->dir_sec);
}

static void free_chain(uint32_t cl)
{
    while (cl >= 2 && !is_eoc(cl)) {
        uint32_t n = fat_get(cl);
        fat_set(cl, 0);
        cl = n;
    }
}

int fat_init(void)
{
    uint16_t bps;
    uint8_t spc;
    mounted = 0;
    kmemset(fdt, 0, sizeof(fdt));
    if (ata_init() < 0)
        return -1;
    if (read_sec(0) < 0)
        return -1;
    if (secbuf[510] != 0x55 || secbuf[511] != 0xAA)
        return -1;
    bps = (uint16_t)(secbuf[11] | (secbuf[12] << 8));
    spc = secbuf[13];
    if (bps != 512 || !spc)
        return -1;
    fs.bytes_per_sec = bps;
    fs.sec_per_clus = spc;
    fs.reserved = (uint16_t)(secbuf[14] | (secbuf[15] << 8));
    fs.fats = secbuf[16];
    fs.root_ents = (uint16_t)(secbuf[17] | (secbuf[18] << 8));
    fs.tot_sec16 = (uint16_t)(secbuf[19] | (secbuf[20] << 8));
    fs.fatsz16 = (uint16_t)(secbuf[22] | (secbuf[23] << 8));
    fs.tot_sec32 = (uint32_t)secbuf[32] | ((uint32_t)secbuf[33] << 8) |
                   ((uint32_t)secbuf[34] << 16) | ((uint32_t)secbuf[35] << 24);
    fs.fatsz32 = (uint32_t)secbuf[36] | ((uint32_t)secbuf[37] << 8) |
                 ((uint32_t)secbuf[38] << 16) | ((uint32_t)secbuf[39] << 24);
    fs.root_clus = (uint32_t)secbuf[44] | ((uint32_t)secbuf[45] << 8) |
                   ((uint32_t)secbuf[46] << 16) | ((uint32_t)secbuf[47] << 24);
    fs.is_fat32 = (fs.fatsz16 == 0);
    fs.fat_start = fs.reserved;
    if (fs.is_fat32) {
        fs.root_secs = 0;
        fs.root_start = 0;
        fs.data_start = fs.fat_start + fs.fats * fs.fatsz32;
        if (!fs.root_clus)
            fs.root_clus = 2;
    } else {
        fs.root_secs = ((fs.root_ents * 32) + (bps - 1)) / bps;
        fs.root_start = fs.fat_start + fs.fats * fs.fatsz16;
        fs.data_start = fs.root_start + fs.root_secs;
    }
    mounted = 1;
    tty_writeln(fs.is_fat32 ? "[fat] FAT32 /disk" : "[fat] FAT16 /disk");
    return 0;
}

int fat_mounted(void)
{
    return mounted;
}

int fat_is_path(const char *path)
{
    return mounted && path &&
           (kstrcmp(path, "/disk") == 0 || kstrncmp(path, "/disk/", 6) == 0);
}

static int alloc_fd(void)
{
    int i;
    for (i = 0; i < FAT_MAX_FD; i++)
        if (!fdt[i].used)
            return i;
    return -1;
}

int fat_open(const char *path, int flags)
{
    uint8_t ent[32];
    int r, idx;
    uint32_t start, size;
    uint32_t dir_sec = 0, dir_off = 0, parent = 0;
    char name[16];
    if (!mounted)
        return VFS_ERR_NOENT;
    r = resolve(path, ent, &parent);
    if (r < 0) {
        if (!(flags & VFS_O_CREATE))
            return VFS_ERR_NOENT;
        if (parent_and_name(path, &parent, name, sizeof(name)) < 0)
            return VFS_ERR_NOENT;
        if (create_entry(parent, name, 0x20, 0, 0, &dir_sec, &dir_off) < 0)
            return VFS_ERR_NOSPC;
        r = resolve(path, ent, &parent);
        if (r < 0)
            return VFS_ERR_NOSPC;
    } else if (r == 1) {
        char leaf[16];
        const char *p = path;
        const char *slash = path;
        for (; *p; p++)
            if (*p == '/')
                slash = p + 1;
        kstrncpy(leaf, slash, sizeof(leaf));
        if (leaf[0])
            find_in_dir(parent, leaf, ent, &dir_sec, &dir_off);
    }
    if (r == 2 && !(flags & VFS_O_DIR) && kstrcmp(path, "/disk") != 0)
        return VFS_ERR_ISDIR;
    idx = alloc_fd();
    if (idx < 0)
        return VFS_ERR_NOSPC;
    kmemset(&fdt[idx], 0, sizeof(fdt[idx]));
    start = 0;
    size = 0;
    if (r == 1) {
        start = clus_from_ent(ent);
        size = (uint32_t)ent[28] | ((uint32_t)ent[29] << 8) |
               ((uint32_t)ent[30] << 16) | ((uint32_t)ent[31] << 24);
        if (ent[11] & 0x10) {
            if (!(flags & VFS_O_DIR) && !(flags & VFS_O_READ))
                return VFS_ERR_ISDIR;
            fdt[idx].is_dir = 1;
        }
    } else if (r == 2)
        fdt[idx].is_dir = 1;
    if ((flags & VFS_O_TRUNC) && (flags & VFS_O_WRITE) && r == 1 && !fdt[idx].is_dir) {
        if (start)
            free_chain(start);
        start = 0;
        size = 0;
    }
    fdt[idx].used = 1;
    fdt[idx].flags = flags;
    fdt[idx].start = start;
    fdt[idx].size = size;
    fdt[idx].pos = (flags & VFS_O_APPEND) ? size : 0;
    fdt[idx].dir_sec = dir_sec;
    fdt[idx].dir_off = dir_off;
    return FAT_FD_BASE + idx;
}

int fat_close(int fd)
{
    int i = fd - FAT_FD_BASE;
    if (i < 0 || i >= FAT_MAX_FD || !fdt[i].used)
        return VFS_ERR_BADFD;
    if ((fdt[i].flags & VFS_O_WRITE) && !fdt[i].is_dir)
        update_dirent(&fdt[i]);
    fdt[i].used = 0;
    return 0;
}

int fat_read(int fd, void *buf, size_t n)
{
    FatFd *f;
    uint8_t *out = (uint8_t *)buf;
    size_t got = 0;
    uint32_t cl, skip, want;
    int i = fd - FAT_FD_BASE;
    if (i < 0 || i >= FAT_MAX_FD || !fdt[i].used)
        return VFS_ERR_BADFD;
    f = &fdt[i];
    if (f->is_dir)
        return VFS_ERR_ISDIR;
    if (f->pos >= f->size)
        return 0;
    if (n > f->size - f->pos)
        n = f->size - f->pos;
    cl = f->start;
    skip = f->pos / (fs.sec_per_clus * FAT_SEC);
    while (skip--) {
        cl = fat_get(cl);
        if (is_eoc(cl) || cl < 2)
            return (int)got;
    }
    while (got < n && !is_eoc(cl) && cl >= 2) {
        uint32_t s, off_in_clus = f->pos % (fs.sec_per_clus * FAT_SEC);
        for (s = off_in_clus / FAT_SEC; s < fs.sec_per_clus && got < n; s++) {
            uint32_t off = (s == off_in_clus / FAT_SEC) ? (off_in_clus % FAT_SEC) : 0;
            if (read_sec(clus_to_lba(cl) + s) < 0)
                return (int)got;
            want = FAT_SEC - off;
            if (want > n - got)
                want = (uint32_t)(n - got);
            if (want > f->size - f->pos)
                want = f->size - f->pos;
            kmemcpy(out + got, secbuf + off, want);
            got += want;
            f->pos += want;
            if (f->pos >= f->size)
                return (int)got;
        }
        cl = fat_get(cl);
        off_in_clus = 0;
    }
    return (int)got;
}

int fat_write(int fd, const void *buf, size_t n)
{
    FatFd *f;
    const uint8_t *in = (const uint8_t *)buf;
    size_t put = 0;
    uint32_t cl, skip, clus_bytes;
    int i = fd - FAT_FD_BASE;
    if (i < 0 || i >= FAT_MAX_FD || !fdt[i].used)
        return VFS_ERR_BADFD;
    f = &fdt[i];
    if (f->is_dir)
        return VFS_ERR_ISDIR;
    if (!(f->flags & VFS_O_WRITE))
        return VFS_ERR_INVAL;
    clus_bytes = fs.sec_per_clus * FAT_SEC;
    if (!f->start && n) {
        f->start = alloc_cluster();
        if (!f->start)
            return VFS_ERR_NOSPC;
        if (zero_cluster(f->start) < 0)
            return VFS_ERR_NOSPC;
    }
    cl = f->start;
    skip = f->pos / clus_bytes;
    while (skip--) {
        uint32_t ncl = fat_get(cl);
        if (is_eoc(ncl) || ncl < 2) {
            ncl = alloc_cluster();
            if (!ncl)
                return (int)put ? (int)put : VFS_ERR_NOSPC;
            if (fat_set(cl, ncl) < 0)
                return VFS_ERR_NOSPC;
            if (zero_cluster(ncl) < 0)
                return VFS_ERR_NOSPC;
            cl = ncl;
            break;
        }
        cl = ncl;
    }
    while (put < n) {
        uint32_t off_in_clus = f->pos % clus_bytes;
        uint32_t s = off_in_clus / FAT_SEC;
        uint32_t off = off_in_clus % FAT_SEC;
        uint32_t want = FAT_SEC - off;
        if (want > n - put)
            want = (uint32_t)(n - put);
        if (read_sec(clus_to_lba(cl) + s) < 0)
            return (int)put;
        kmemcpy(secbuf + off, in + put, want);
        if (write_sec(clus_to_lba(cl) + s) < 0)
            return (int)put;
        put += want;
        f->pos += want;
        if (f->pos > f->size)
            f->size = f->pos;
        if (put >= n)
            break;
        if ((f->pos % clus_bytes) == 0) {
            uint32_t ncl = fat_get(cl);
            if (is_eoc(ncl) || ncl < 2) {
                ncl = alloc_cluster();
                if (!ncl)
                    break;
                if (fat_set(cl, ncl) < 0)
                    break;
                if (zero_cluster(ncl) < 0)
                    break;
            }
            cl = ncl;
        }
    }
    update_dirent(f);
    return (int)put;
}

int fat_mkdir(const char *path)
{
    uint32_t parent = 0, cl, sec, off;
    char name[16];
    uint8_t ent[32];
    if (!mounted)
        return VFS_ERR_NOENT;
    if (resolve(path, ent, 0) > 0)
        return VFS_ERR_EXIST;
    if (parent_and_name(path, &parent, name, sizeof(name)) < 0)
        return VFS_ERR_NOENT;
    cl = alloc_cluster();
    if (!cl)
        return VFS_ERR_NOSPC;
    if (zero_cluster(cl) < 0)
        return VFS_ERR_NOSPC;
    
    kmemset(secbuf, 0, FAT_SEC);
    kmemcpy(secbuf, ".          ", 11);
    secbuf[11] = 0x10;
    secbuf[26] = (uint8_t)(cl & 0xFF);
    secbuf[27] = (uint8_t)((cl >> 8) & 0xFF);
    kmemcpy(secbuf + 32, "..         ", 11);
    secbuf[32 + 11] = 0x10;
    if (fs.is_fat32 || parent) {
        secbuf[32 + 26] = (uint8_t)(parent & 0xFF);
        secbuf[32 + 27] = (uint8_t)((parent >> 8) & 0xFF);
        if (fs.is_fat32) {
            secbuf[20] = (uint8_t)((cl >> 16) & 0xFF);
            secbuf[21] = (uint8_t)((cl >> 24) & 0xFF);
            secbuf[32 + 20] = (uint8_t)((parent >> 16) & 0xFF);
            secbuf[32 + 21] = (uint8_t)((parent >> 24) & 0xFF);
        }
    }
    if (write_sec(clus_to_lba(cl)) < 0)
        return VFS_ERR_NOSPC;
    if (create_entry(parent, name, 0x10, cl, 0, &sec, &off) < 0)
        return VFS_ERR_NOSPC;
    return 0;
}

int fat_listdir(const char *path, char *buf, size_t buflen)
{
    uint8_t ent[32];
    uint32_t dir = 0, sec, i, root_left, cl, resolved = 0;
    size_t pos = 0;
    int r;
    if (!mounted)
        return VFS_ERR_NOENT;
    r = resolve(path, ent, &resolved);
    if (r < 0)
        return VFS_ERR_NOENT;
    if (r == 1 && !(ent[11] & 0x10))
        return VFS_ERR_NOTDIR;
    
    if (r == 1 || (r == 2 && (ent[11] & 0x10)))
        dir = clus_from_ent(ent);
    else
        dir = resolved;
    if (dir == 0 && resolved != 0)
        dir = resolved;
    buf[0] = 0;
    if (!fs.is_fat32 && dir == 0) {
        root_left = fs.root_secs;
        for (sec = fs.root_start; root_left; sec++, root_left--) {
            if (read_sec(sec) < 0)
                break;
            for (i = 0; i < FAT_SEC; i += 32) {
                char name[16];
                uint8_t *e = secbuf + i;
                size_t L;
                if (e[0] == 0)
                    goto done;
                if (e[0] == 0xE5 || e[11] == 0x0F || (e[11] & 0x08))
                    continue;
                decode83(e, name, sizeof(name));
                L = kstrlen(name);
                if (pos + L + 2 >= buflen)
                    goto done;
                kmemcpy(buf + pos, name, L);
                pos += L;
                buf[pos++] = '\n';
                buf[pos] = 0;
            }
        }
        goto done;
    }
    cl = dir ? dir : fs.root_clus;
    while (!is_eoc(cl) && cl >= 2) {
        uint32_t s;
        for (s = 0; s < fs.sec_per_clus; s++) {
            if (read_sec(clus_to_lba(cl) + s) < 0)
                goto done;
            for (i = 0; i < FAT_SEC; i += 32) {
                char name[16];
                uint8_t *e = secbuf + i;
                size_t L;
                if (e[0] == 0)
                    goto done;
                if (e[0] == 0xE5 || e[11] == 0x0F || (e[11] & 0x08))
                    continue;
                decode83(e, name, sizeof(name));
                if (!kstrcmp(name, ".") || !kstrcmp(name, ".."))
                    continue;
                L = kstrlen(name);
                if (pos + L + 2 >= buflen)
                    goto done;
                kmemcpy(buf + pos, name, L);
                pos += L;
                buf[pos++] = '\n';
                buf[pos] = 0;
            }
        }
        cl = fat_get(cl);
    }
done:
    return (int)pos;
}

int fat_exists(const char *path)
{
    uint8_t ent[32];
    return resolve(path, ent, 0) > 0;
}

int fat_isdir(const char *path)
{
    uint8_t ent[32];
    int r = resolve(path, ent, 0);
    if (r == 2)
        return 1;
    if (r == 1)
        return (ent[11] & 0x10) != 0;
    return 0;
}

static int dir_is_empty(uint32_t dir_clus)
{
    uint32_t cl, sec, i, root_left;
    char name[16];
    if (!fs.is_fat32 && dir_clus == 0) {
        root_left = fs.root_secs;
        for (sec = fs.root_start; root_left; sec++, root_left--) {
            if (read_sec(sec) < 0)
                return 0;
            for (i = 0; i < FAT_SEC; i += 32) {
                uint8_t *e = secbuf + i;
                if (e[0] == 0)
                    return 1;
                if (e[0] == 0xE5 || e[11] == 0x0F)
                    continue;
                decode83(e, name, sizeof(name));
                if (kstrcmp(name, ".") && kstrcmp(name, ".."))
                    return 0;
            }
        }
        return 1;
    }
    cl = dir_clus ? dir_clus : fs.root_clus;
    while (!is_eoc(cl) && cl >= 2) {
        uint32_t s;
        for (s = 0; s < fs.sec_per_clus; s++) {
            if (read_sec(clus_to_lba(cl) + s) < 0)
                return 0;
            for (i = 0; i < FAT_SEC; i += 32) {
                uint8_t *e = secbuf + i;
                if (e[0] == 0)
                    return 1;
                if (e[0] == 0xE5 || e[11] == 0x0F)
                    continue;
                decode83(e, name, sizeof(name));
                if (kstrcmp(name, ".") && kstrcmp(name, ".."))
                    return 0;
            }
        }
        cl = fat_get(cl);
    }
    return 1;
}

int fat_unlink(const char *path)
{
    uint32_t parent = 0, sec, off, start;
    char name[16];
    uint8_t ent[32];
    int found;

    if (!mounted)
        return VFS_ERR_NOENT;
    if (!path || !path[0] || kstrcmp(path, "/disk") == 0)
        return VFS_ERR_INVAL;
    if (parent_and_name(path, &parent, name, sizeof(name)) < 0)
        return VFS_ERR_NOENT;
    if (!kstrcmp(name, ".") || !kstrcmp(name, ".."))
        return VFS_ERR_INVAL;
    found = find_in_dir(parent, name, ent, &sec, &off);
    if (found <= 0)
        return VFS_ERR_NOENT;
    if (ent[11] & 0x08)
        return VFS_ERR_INVAL;
    start = clus_from_ent(ent);
    if (ent[11] & 0x10) {
        if (!dir_is_empty(start))
            return VFS_ERR_INVAL;
    }
    if (read_sec(sec) < 0)
        return VFS_ERR_NOSPC;
    secbuf[off] = 0xE5;
    if (write_sec(sec) < 0)
        return VFS_ERR_NOSPC;
    if (start >= 2)
        free_chain(start);
    return VFS_OK;
}

uint32_t fat_file_size(const char *path)
{
    uint8_t ent[32];
    int r = resolve(path, ent, 0);
    if (r != 1 || (ent[11] & 0x10))
        return 0;
    return (uint32_t)ent[28] | ((uint32_t)ent[29] << 8) |
           ((uint32_t)ent[30] << 16) | ((uint32_t)ent[31] << 24);
}
