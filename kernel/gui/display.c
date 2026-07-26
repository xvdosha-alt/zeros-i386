#include "display.h"
#include "proc.h"
#include "mm.h"
#include "string.h"
#include "syscall.h"

typedef struct {
    uint32_t type;
    int key;
    int x, y, buttons;
} GuiEvt;

typedef struct {
    int used;
    int id;
    int client_pid;
    int w, h;
    uint32_t *pixels;
    uint32_t pages;
    int damage;
    int mapped;
    int flags;
    char title[GUI_TITLE_MAX];
    GuiEvt ev[GUI_EVT_MAX];
    int ev_r, ev_w;
} GuiWin;

static GuiWin gwins[GUI_WIN_MAX];
static int server_pid;
static int next_id = 1;

#define LAUNCH_Q 4
typedef struct {
    int used;
    char path[GUI_LAUNCH_MAX];
    char argv[GUI_LAUNCH_MAX];
} LaunchReq;
static LaunchReq launch_q[LAUNCH_Q];

void display_init(void)
{
    kmemset(gwins, 0, sizeof(gwins));
    kmemset(launch_q, 0, sizeof(launch_q));
    server_pid = 0;
    next_id = 1;
}

int display_server_set(int on)
{
    Proc *c = proc_current();
    if (!c)
        return -1;
    if (on) {
        if (server_pid && server_pid != c->pid)
            return -1;
        server_pid = c->pid;
        return 0;
    }
    if (server_pid != c->pid)
        return -1;
    server_pid = 0;
    return 0;
}

int display_server_pid(void)
{
    return server_pid;
}

int display_fb_locked(void)
{
    return server_pid != 0;
}

static GuiWin *win_by_id(int id)
{
    int i;
    for (i = 0; i < GUI_WIN_MAX; i++)
        if (gwins[i].used && gwins[i].id == id)
            return &gwins[i];
    return 0;
}

int display_create(int w, int h, const char *title)
{
    Proc *c = proc_current();
    GuiWin *gw = 0;
    size_t bytes, pages;
    int i;
    if (!c || !server_pid || w < 32 || h < 32 || w > 1024 || h > 768)
        return -1;
    for (i = 0; i < GUI_WIN_MAX; i++) {
        if (!gwins[i].used) {
            gw = &gwins[i];
            break;
        }
    }
    if (!gw)
        return -1;
    bytes = (size_t)w * (size_t)h * 4u;
    pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    gw->pixels = (uint32_t *)mm_alloc_pages(pages);
    if (!gw->pixels)
        return -1;
    kmemset(gw->pixels, 0, pages * PAGE_SIZE);
    gw->used = 1;
    gw->id = next_id++;
    gw->client_pid = c->pid;
    gw->w = w;
    gw->h = h;
    gw->pages = (uint32_t)pages;
    gw->damage = 1;
    gw->mapped = 0;
    gw->flags = 0;
    gw->ev_r = gw->ev_w = 0;
    if (title && title[0])
        kstrncpy(gw->title, title, GUI_TITLE_MAX);
    else
        kstrncpy(gw->title, "window", GUI_TITLE_MAX);
    return gw->id;
}

uint32_t display_fb(int id)
{
    GuiWin *gw = win_by_id(id);
    if (!gw)
        return 0;
    return (uint32_t)gw->pixels;
}

int display_damage(int id, uint32_t *frame)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    if (!gw || !c || gw->client_pid != c->pid)
        return -1;
    gw->damage = 1;
    /* Yield so the display server can composite. Do not HLT here:
     * the parent is blocked in wait(), and a HLT before wm's first
     * redraw leaves a blank screen if the client presents early. */
    if (server_pid && frame)
        proc_yield_to_parent(frame);
    return 0;
}

int display_destroy(int id)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    if (!gw)
        return -1;
    if (c && c->pid != gw->client_pid && c->pid != server_pid)
        return -1;
    if (gw->pixels)
        mm_free_pages(gw->pixels, gw->pages);
    kmemset(gw, 0, sizeof(*gw));
    return 0;
}

void display_destroy_pid(int pid)
{
    int i;
    if (pid <= 0)
        return;
    for (i = 0; i < GUI_WIN_MAX; i++) {
        if (!gwins[i].used || gwins[i].client_pid != pid)
            continue;
        if (gwins[i].pixels)
            mm_free_pages(gwins[i].pixels, gwins[i].pages);
        kmemset(&gwins[i], 0, sizeof(gwins[i]));
    }
}

int display_info(int id, int field)
{
    GuiWin *gw = win_by_id(id);
    if (!gw)
        return -1;
    switch (field) {
    case GUI_INFO_W: return gw->w;
    case GUI_INFO_H: return gw->h;
    case GUI_INFO_FB: return (int)(uint32_t)gw->pixels;
    case GUI_INFO_DAMAGE: return gw->damage;
    case GUI_INFO_PID: return gw->client_pid;
    case GUI_INFO_MAPPED: return gw->mapped;
    case GUI_INFO_FLAGS: return gw->flags;
    default: return -1;
    }
}

int display_set_flags(int id, int flags)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    if (!gw || !c || (c->pid != gw->client_pid && c->pid != server_pid))
        return -1;
    gw->flags = flags;
    return 0;
}

int display_resize(int id, int w, int h)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    uint32_t *npix;
    size_t bytes, pages;
    int copy_w, copy_h, y;
    if (!gw || !c || c->pid != server_pid)
        return -1;
    if (w < 32 || h < 32 || w > 1024 || h > 768)
        return -1;
    if (w == gw->w && h == gw->h)
        return 0;
    bytes = (size_t)w * (size_t)h * 4u;
    pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    npix = (uint32_t *)mm_alloc_pages(pages);
    if (!npix)
        return -1;
    kmemset(npix, 0, pages * PAGE_SIZE);
    copy_w = w < gw->w ? w : gw->w;
    copy_h = h < gw->h ? h : gw->h;
    for (y = 0; y < copy_h; y++) {
        int x;
        for (x = 0; x < copy_w; x++)
            npix[y * w + x] = gw->pixels[y * gw->w + x];
    }
    if (gw->pixels)
        mm_free_pages(gw->pixels, gw->pages);
    gw->pixels = npix;
    gw->pages = (uint32_t)pages;
    gw->w = w;
    gw->h = h;
    gw->damage = 1;
    display_post_event(id, INP_RESIZE, 0, w, h, 0);
    return 0;
}

int display_set_title(int id, const char *title)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    if (!gw || !c || (c->pid != gw->client_pid && c->pid != server_pid))
        return -1;
    if (!title)
        return -1;
    kstrncpy(gw->title, title, GUI_TITLE_MAX);
    gw->damage = 1;
    return 0;
}

int display_get_title(int id, char *buf, int n)
{
    GuiWin *gw = win_by_id(id);
    if (!gw || !buf || n <= 0)
        return -1;
    kstrncpy(buf, gw->title, (size_t)n);
    return 0;
}

int display_next_new(void)
{
    int i;
    Proc *c = proc_current();
    if (!c || c->pid != server_pid)
        return -1;
    for (i = 0; i < GUI_WIN_MAX; i++)
        if (gwins[i].used && !gwins[i].mapped)
            return gwins[i].id;
    return -1;
}

int display_ack_map(int id)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    if (!gw || !c || c->pid != server_pid)
        return -1;
    gw->mapped = 1;
    return 0;
}

int display_ack_damage(int id)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    if (!gw || !c || c->pid != server_pid)
        return -1;
    gw->damage = 0;
    return 0;
}

int display_find_pid(int pid)
{
    int i;
    if (pid <= 0)
        return -1;
    for (i = 0; i < GUI_WIN_MAX; i++)
        if (gwins[i].used && gwins[i].client_pid == pid)
            return gwins[i].id;
    return -1;
}

int display_request_launch(const char *path, const char *argv)
{
    int i;
    if (!server_pid || !path || !path[0])
        return -1;
    for (i = 0; i < LAUNCH_Q; i++) {
        if (launch_q[i].used)
            continue;
        launch_q[i].used = 1;
        kstrncpy(launch_q[i].path, path, GUI_LAUNCH_MAX);
        if (argv && argv[0])
            kstrncpy(launch_q[i].argv, argv, GUI_LAUNCH_MAX);
        else
            launch_q[i].argv[0] = 0;
        return 0;
    }
    return -1;
}

int display_take_launch(char *buf, int n)
{
    int i;
    size_t lp, la;
    Proc *c = proc_current();
    if (!c || c->pid != server_pid || !buf || n < 4)
        return 0;
    for (i = 0; i < LAUNCH_Q; i++) {
        if (!launch_q[i].used)
            continue;
        lp = kstrlen(launch_q[i].path);
        la = kstrlen(launch_q[i].argv);
        if ((int)(lp + la + 3) > n)
            return 0;
        kmemcpy(buf, launch_q[i].path, lp + 1);
        kmemcpy(buf + lp + 1, launch_q[i].argv, la + 1);
        launch_q[i].used = 0;
        return 1;
    }
    return 0;
}

int display_post_event(int id, uint32_t type, int key, int x, int y, int buttons)
{
    GuiWin *gw = win_by_id(id);
    int next, i, last;
    Proc *c = proc_current();
    if (!gw || !c)
        return -1;
    /* Server or DOS-box host (gterm) may inject input. */
    if (c->pid != server_pid && !c->cons_host)
        return -1;
    /* Coalesce resize: during drag many events would overflow the queue. */
    if (type == INP_RESIZE) {
        for (i = gw->ev_r; i != gw->ev_w; i = (i + 1) % GUI_EVT_MAX) {
            if (gw->ev[i].type == INP_RESIZE) {
                gw->ev[i].x = x;
                gw->ev[i].y = y;
                return 0;
            }
        }
    }
    /* Coalesce mouse: keep latest pointer/buttons so release is never dropped. */
    if (type == INP_MOUSE && gw->ev_r != gw->ev_w) {
        last = (gw->ev_w + GUI_EVT_MAX - 1) % GUI_EVT_MAX;
        if (gw->ev[last].type == INP_MOUSE) {
            gw->ev[last].x = x;
            gw->ev[last].y = y;
            gw->ev[last].buttons = buttons;
            return 0;
        }
    }
    next = (gw->ev_w + 1) % GUI_EVT_MAX;
    if (next == gw->ev_r) {
        /* Queue full: still deliver mouse release / latest mouse if possible. */
        if (type == INP_MOUSE) {
            for (i = gw->ev_r; i != gw->ev_w; i = (i + 1) % GUI_EVT_MAX) {
                if (gw->ev[i].type == INP_MOUSE) {
                    gw->ev[i].x = x;
                    gw->ev[i].y = y;
                    gw->ev[i].buttons = buttons;
                    return 0;
                }
            }
        }
        return -1;
    }
    gw->ev[gw->ev_w].type = type;
    gw->ev[gw->ev_w].key = key;
    gw->ev[gw->ev_w].x = x;
    gw->ev[gw->ev_w].y = y;
    gw->ev[gw->ev_w].buttons = buttons;
    gw->ev_w = next;
    return 0;
}

int display_poll_event(int id, uint32_t *type, int *key, int *x, int *y, int *buttons)
{
    GuiWin *gw = win_by_id(id);
    Proc *c = proc_current();
    GuiEvt e;
    if (!gw || !c || c->pid != gw->client_pid)
        return 0;
    if (gw->ev_r == gw->ev_w)
        return 0;
    e = gw->ev[gw->ev_r];
    gw->ev_r = (gw->ev_r + 1) % GUI_EVT_MAX;
    if (type) *type = e.type;
    if (key) *key = e.key;
    if (x) *x = e.x;
    if (y) *y = e.y;
    if (buttons) *buttons = e.buttons;
    return 1;
}
