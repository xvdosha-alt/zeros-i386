#include "libgui.h"
#include "termrun.h"
#include "user/guic/guic.h"

#define PANEL_H 32
#define TITLE_H 22
/* Thin frame for fixed windows; thicker for resizable (grab edges/corners). */
#define FRAME_X 2
#define FRAME_BOT 2
#define FRAME_X_RESIZE 5
#define FRAME_BOT_RESIZE 5
#define RESIZE_GRIP 5
#define CAP_BTN 14
#define CAP_GAP 2
#define MAX_WIN 8
#define TERM_ROWS 18
#define TERM_LINE 64
#define PANEL_Z 0
#define PANEL_EXIT 1
#define PANEL_BTN_N 2
#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8

enum {
    APP_TERM = 1,
    APP_FILES = 2,
    APP_NET = 3,
    APP_ABOUT = 4,
    APP_EXT = 5 /* external GUI client via display server */
};

enum {
    TERM_PROMPT = 0,
    TERM_RUN = 1,
    TERM_DONE = 2
};

enum {
    CAP_NONE = 0,
    CAP_MIN = 1,
    CAP_MAX = 2,
    CAP_CLOSE = 3
};

typedef struct {
    int open;
    int x, y, w, h;
    int app;
    char title[32];
    char path[96];
    char listing[1024];
    char netbuf[512];
    char lines[TERM_ROWS][TERM_LINE];
    int nlines;
    char input[TERM_LINE];
    int inlen;
    TermSession shell;
    int term_mode; /* gterm-style: prompt → run → done (no new prompt) */
    int shell_pump;
    char live[TERM_LINE];
    /* External display-server client */
    int gui_id;
    int gui_pid;
    uint32_t *gui_fb;
    int gui_cw, gui_ch;
    int resizable;
    int show_minmax; /* 0 = close only (About) */
    int maximized;
    int minimized;
    int popup; /* 1 = no chrome (Start menu etc.) */
    int rest_x, rest_y, rest_w, rest_h;
    int rest_cw, rest_ch;
} Win;

static GuiScreen g;
static Win wins[MAX_WIN];
static int zorder[MAX_WIN];
static int nwin;
static int task_order[MAX_WIN]; /* stable taskbar order (open sequence) */
static int ntaskbar;
static int focus;
static int mx, my, mbtn, prev_btn;
static int dragging;
static int drag_off_x, drag_off_y;
static int resizing;
static int resize_off_x, resize_off_y;
static int resize_base_cw, resize_base_ch;
static int running;
static int hover_btn;
static int anim_focus;
static int dirty = 1;
static int cap_press;     /* CAP_* while mouse held on caption button */
static int cap_press_id;  /* window id for cap_press */

#define DESK_MAX 12
#define ICON_W 64
#define ICON_H 56
#define ICON_GAP_X 12
#define ICON_GAP_Y 8
#define ICON_ORIGIN_X 16
#define ICON_ORIGIN_Y 16
#define O_READ 1

enum { RZ_NONE = 0, RZ_L = 1, RZ_R = 2, RZ_T = 4, RZ_B = 8 };

typedef struct {
    int used;
    char name[40];
    char path[96];
    char argv[64];
    int x, y, w, h;
    int selected;
} DeskIcon;

static DeskIcon desk_icons[DESK_MAX];
static int ndesk;
static int desk_sel;
static int last_icon_click_i;
static int alt_held;
static int alt_tab_active;
static int alt_tab_idx;
static int resize_edges;
static int resize_base_x, resize_base_y, resize_base_w, resize_base_h;
static int sound_on = 1;

static WFrame desk_root;
static WLabel desk_title;
static WLabel desk_sub;
static WFrame panel;
static WButton panel_btns[PANEL_BTN_N];
static WButton task_btns[MAX_WIN];
static char task_labels[MAX_WIN][32];
static int panel_apps_end_x;
static int panel_ready;

static void draw_wallpaper(void);
static void draw_desktop_icons(void);
static void draw_alttab_overlay(void);
static void load_desktop_icons(void);
static void load_sound_pref(void);
static void desk_beep(uint32_t freq, uint32_t ms);
static int desk_icon_at(int x, int y);
static void desk_launch_icon(int i);
static void restore_from_max_for_drag(Win *w, int mx, int my);
static int resize_hit_edges(Win *w, int x, int y);
static void alt_tab_cycle(void);
static void alt_tab_commit(void);

/* Cursor underlay — mouse moves must not full-repaint the desktop. */
#define CUR_W 16
#define CUR_H 16
static uint32_t cur_under[CUR_W * CUR_H];
static int cur_saved;
static int cur_sx = -1, cur_sy = -1;

static void cursor_save_at(int x, int y)
{
    int row, col;
    cur_sx = x;
    cur_sy = y;
    cur_saved = 1;
    for (row = 0; row < CUR_H; row++) {
        int py = y + row;
        for (col = 0; col < CUR_W; col++) {
            int px = x + col;
            uint32_t pix = W95_DESK;
            if (px >= 0 && py >= 0 && px < g.w && py < g.h)
                pix = g.fb[py * g.pitch + px];
            cur_under[row * CUR_W + col] = pix;
        }
    }
}

static void cursor_restore(void)
{
    int row, col;
    if (!cur_saved || cur_sx < 0)
        return;
    for (row = 0; row < CUR_H; row++) {
        int py = cur_sy + row;
        if (py < 0 || py >= g.h)
            continue;
        for (col = 0; col < CUR_W; col++) {
            int px = cur_sx + col;
            if (px < 0 || px >= g.w)
                continue;
            g.fb[py * g.pitch + px] = cur_under[row * CUR_W + col];
        }
    }
    cur_saved = 0;
}

static void cursor_paint(int x, int y)
{
    cursor_restore();
    cursor_save_at(x, y);
    gui_cursor(&g, x, y);
}

/* Panel / auto-started GUI apps: same terminal host as gterm, no tty chrome. */
typedef struct {
    int used;
    TermSession sess;
} LaunchHost;
static LaunchHost launches[MAX_WIN];

static void apply_maximize(int id);
static void apply_minimize(int id);
static void apply_restore(int id);
static void taskbar_sync(void);
static void launch_stop_pid(int pid);
static int launch_app(const char *path);
static int launch_app_argv(const char *path, const char *argv);
static void launches_pump(void);

#define SCREEN_W 1024
#define SCREEN_H 768
#define WIN_W 640
#define WIN_H 420

static void term_push(Win *w, const char *s)
{
    int i;
    if (w->nlines >= TERM_ROWS) {
        for (i = 0; i < TERM_ROWS - 1; i++)
            strncpy_u(w->lines[i], w->lines[i + 1], TERM_LINE);
        w->nlines = TERM_ROWS - 1;
    }
    strncpy_u(w->lines[w->nlines], s, TERM_LINE);
    w->nlines++;
}

static void term_out_cb(void *ctx, const char *line)
{
    term_push((Win *)ctx, line);
}

static int resolve_exec(const char *name, char *out, int outmax)
{
    char bin[128];
    int i, n;
    if (!name || !name[0] || !out || outmax < 2)
        return -1;
    for (i = 0; name[i]; i++) {
        if (name[i] == '/')
            break;
    }
    if (!name[i]) {
        strncpy_u(bin, "/sys/bin/", sizeof(bin));
        n = strlen_u(bin);
        strncpy_u(bin + n, name, (int)sizeof(bin) - n);
        if (sys_exists(bin)) {
            strncpy_u(out, bin, outmax);
            return 0;
        }
        strncpy_u(bin, "/sys/gui/", sizeof(bin));
        n = strlen_u(bin);
        strncpy_u(bin + n, name, (int)sizeof(bin) - n);
        /* Prefer .win for GUI clients; bare name for wm. */
        {
            int L = strlen_u(bin);
            if (L + 4 < (int)sizeof(bin)) {
                bin[L] = '.';
                bin[L + 1] = 'w';
                bin[L + 2] = 'i';
                bin[L + 3] = 'n';
                bin[L + 4] = 0;
                if (sys_exists(bin)) {
                    strncpy_u(out, bin, outmax);
                    return 0;
                }
                bin[L] = 0;
            }
        }
        if (sys_exists(bin)) {
            strncpy_u(out, bin, outmax);
            return 0;
        }
    }
    if (sys_exists(name)) {
        strncpy_u(out, name, outmax);
        return 0;
    }
    return -1;
}

static void write_argv_tail(char *const *argv, int argc)
{
    char args[128];
    int i, pos, afd, L;
    args[0] = 0;
    pos = 0;
    for (i = 1; i < argc; i++) {
        L = strlen_u(argv[i]);
        if (pos + L + 2 >= (int)sizeof(args))
            break;
        if (pos)
            args[pos++] = ' ';
        memcpy_u(args + pos, argv[i], L);
        pos += L;
        args[pos] = 0;
    }
    afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
    if (afd >= 0) {
        sys_write(afd, args, strlen_u(args));
        sys_close(afd);
    }
}

static void term_set_title(Win *w, const char *path)
{
    const char *base = path;
    const char *p;
    int n;
    if (!w)
        return;
    if (path) {
        for (p = path; *p; p++) {
            if (*p == '/')
                base = p + 1;
        }
    }
    strncpy_u(w->title, "gterm - ", sizeof(w->title));
    n = strlen_u(w->title);
    if (base && base[0])
        strncpy_u(w->title + n, base, (int)sizeof(w->title) - n);
    else
        strncpy_u(w->title + n, "?", (int)sizeof(w->title) - n);
}

/* Returns 1 if display should refresh (new lines, live changed, or exited). */
static int term_drain(Win *w, int first_key)
{
    int fed = first_key;
    int spins = 0;
    int changed = 0;
    char prev_live[TERM_LINE];

    if (!w)
        return 0;
    strncpy_u(prev_live, w->live, sizeof(prev_live));
    for (; spins++ < 64;) {
        if (!term_session_pump(&w->shell, fed, term_out_cb, w)) {
            w->live[0] = 0;
            w->shell_pump = 0;
            term_push(w, "[process finished]");
            w->term_mode = TERM_DONE;
            strncpy_u(w->title, "gterm", sizeof(w->title));
            return 1;
        }
        strncpy_u(w->live, term_session_live(&w->shell), sizeof(w->live));
        if (w->shell.painted)
            changed = 1;
        fed = -1;
        if (!w->shell.painted) {
            w->shell_pump = 0;
            break;
        }
        w->shell_pump = 1;
    }
    if (strcmp_u(prev_live, w->live) != 0)
        changed = 1;
    return changed;
}

static void term_exec(Win *w, const char *cmdline)
{
    char work[TERM_LINE];
    char path[128];
    char *argv[8];
    int argc;

    if (!w || !cmdline || !cmdline[0] || w->term_mode != TERM_PROMPT)
        return;
    strncpy_u(work, cmdline, sizeof(work));
    argc = split_args(work, argv, 8);
    if (argc < 1)
        return;
    if (resolve_exec(argv[0], path, sizeof(path)) != 0) {
        term_push(w, "not found");
        return;
    }
    write_argv_tail(argv, argc);
    if (term_session_start(&w->shell, path) != 0) {
        term_push(w, "spawn failed");
        return;
    }
    term_set_title(w, path);
    w->term_mode = TERM_RUN;
    w->shell_pump = 1;
    w->live[0] = 0;
    term_drain(w, -1);
}

static void files_refresh(Win *w)
{
    if (sys_listdir(w->path, w->listing, sizeof(w->listing)) < 0)
        strncpy_u(w->listing, "(empty or error)", sizeof(w->listing));
}

static void net_refresh(Win *w)
{
    if (sys_ifconfig(w->netbuf, sizeof(w->netbuf)) < 0)
        strncpy_u(w->netbuf, "network unavailable", sizeof(w->netbuf));
}

static int win_open(int app, int x, int y)
{
    int i, id = -1;
    for (i = 0; i < MAX_WIN; i++) {
        if (!wins[i].open) {
            id = i;
            break;
        }
    }
    if (id < 0) return -1;
    memset_u(&wins[id], 0, sizeof(wins[id]));
    wins[id].open = 1;
    wins[id].app = app;
    wins[id].x = x;
    wins[id].y = y;
    wins[id].w = WIN_W;
    wins[id].h = WIN_H;
    if (app == APP_TERM) {
        strncpy_u(wins[id].title, "gterm", sizeof(wins[id].title));
        wins[id].term_mode = TERM_PROMPT;
        term_push(&wins[id], "gterm — type an executable (bin name or path)");
    } else if (app == APP_FILES) {
        strncpy_u(wins[id].title, "Files", sizeof(wins[id].title));
        strncpy_u(wins[id].path, "/sys", sizeof(wins[id].path));
        files_refresh(&wins[id]);
    } else if (app == APP_NET) {
        strncpy_u(wins[id].title, "Network", sizeof(wins[id].title));
        wins[id].w = 520;
        wins[id].h = 320;
        net_refresh(&wins[id]);
    } else if (app == APP_ABOUT) {
        strncpy_u(wins[id].title, "About zerOS", sizeof(wins[id].title));
        wins[id].w = 420;
        wins[id].h = 240;
        wins[id].show_minmax = 0;
    }
    if (app != APP_ABOUT)
        wins[id].show_minmax = 1;
    zorder[nwin++] = id;
    if (ntaskbar < MAX_WIN)
        task_order[ntaskbar++] = id;
    focus = id;
    anim_focus = 8;
    dirty = 1;
    return id;
}

static void win_raise(int id)
{
    int i, j;
    if (id < 0 || !wins[id].open) return;
    wins[id].minimized = 0;
    for (i = 0; i < nwin; i++) {
        if (zorder[i] == id) {
            for (j = i; j < nwin - 1; j++) zorder[j] = zorder[j + 1];
            zorder[nwin - 1] = id;
            break;
        }
    }
    if (focus != id) anim_focus = 8;
    focus = id;
    dirty = 1;
}

static void win_close(int id);

/* Soft close: if client set CLOSE_HOOK, post INP_CLOSE; else destroy. */
static void request_win_close(int id)
{
    Win *w;
    int flags;
    if (id < 0 || !wins[id].open)
        return;
    w = &wins[id];
    if (w->app == APP_EXT && w->gui_id >= 0) {
        flags = sys_gui_info(w->gui_id, GUI_INFO_FLAGS);
        if (flags > 0 && (flags & GUI_FLAG_CLOSE_HOOK)) {
            sys_gui_post(w->gui_id, INP_CLOSE, 0);
            return;
        }
    }
    win_close(id);
}

static void win_close(int id)
{
    int i, j;
    int child = -1;
    if (id < 0 || !wins[id].open) return;
    if (wins[id].app == APP_TERM && wins[id].term_mode == TERM_RUN)
        child = wins[id].shell.pid;
    if (wins[id].term_mode == TERM_RUN) {
        /* Kills the hosted process (gterm is only a shim). */
        term_session_stop(&wins[id].shell);
        wins[id].term_mode = TERM_DONE;
        wins[id].live[0] = 0;
    }
    if (wins[id].app == APP_EXT) {
        int cpid = wins[id].gui_pid;
        if (wins[id].gui_id >= 0) {
            sys_gui_destroy(wins[id].gui_id);
            wins[id].gui_id = -1;
        }
        if (cpid >= 0)
            sys_kill(cpid);
        launch_stop_pid(cpid);
        /* Also stop a gterm-hosted session if this was typed from a term. */
        {
            int t;
            for (t = 0; t < MAX_WIN; t++) {
                if (!wins[t].open || wins[t].app != APP_TERM)
                    continue;
                if (wins[t].term_mode == TERM_RUN &&
                    wins[t].shell.pid == cpid) {
                    term_session_stop(&wins[t].shell);
                    wins[t].shell_pump = 0;
                    wins[t].live[0] = 0;
                    term_push(&wins[t], "[process finished]");
                    wins[t].term_mode = TERM_DONE;
                    strncpy_u(wins[t].title, "gterm", sizeof(wins[t].title));
                }
            }
        }
    }
    wins[id].open = 0;
    for (i = 0; i < nwin; i++) {
        if (zorder[i] == id) {
            for (j = i; j < nwin - 1; j++) zorder[j] = zorder[j + 1];
            nwin--;
            break;
        }
    }
    for (i = 0; i < ntaskbar; i++) {
        if (task_order[i] == id) {
            for (j = i; j < ntaskbar - 1; j++)
                task_order[j] = task_order[j + 1];
            ntaskbar--;
            break;
        }
    }
    focus = nwin > 0 ? zorder[nwin - 1] : -1;
    /* Drop client window that belonged to the killed gterm child. */
    if (child >= 0) {
        for (i = 0; i < MAX_WIN; i++) {
            if (wins[i].open && wins[i].app == APP_EXT && wins[i].gui_pid == child) {
                wins[i].gui_id = -1;
                win_close(i);
            }
        }
    }
    dirty = 1;
}

/* Client content origin inside wm chrome. */
static int win_frame_x(const Win *w)
{
    if (!w || w->maximized || w->popup)
        return 0;
    return w->resizable ? FRAME_X_RESIZE : FRAME_X;
}

static int win_frame_top(const Win *w)
{
    if (!w || w->maximized || w->popup)
        return 0;
    return w->resizable ? FRAME_X_RESIZE : FRAME_X;
}

static int win_frame_bot(const Win *w)
{
    if (!w || w->maximized || w->popup)
        return 0;
    return w->resizable ? FRAME_BOT_RESIZE : FRAME_BOT;
}

static int client_ox(const Win *w)
{
    return w->x + win_frame_x(w);
}

static int client_oy(const Win *w)
{
    if (w->popup)
        return w->y;
    return w->y + win_frame_top(w) + TITLE_H;
}

/* Caption buttons right→left: close, then max, then min. */
static void caption_btn_xy(const Win *w, int which, int *bx, int *by)
{
    int fx = win_frame_x(w);
    int idx = 0; /* 0=close (rightmost) */
    if (which == CAP_MAX)
        idx = 1;
    else if (which == CAP_MIN)
        idx = 2;
    *bx = w->x + w->w - fx - 2 - CAP_BTN - idx * (CAP_BTN + CAP_GAP);
    *by = w->y + win_frame_top(w) + (TITLE_H - CAP_BTN) / 2;
}

static int caption_hit(const Win *w, int x, int y)
{
    int bx, by;
    if (!w || w->popup)
        return CAP_NONE;
    caption_btn_xy(w, CAP_CLOSE, &bx, &by);
    if (gui_hit(x, y, bx, by, CAP_BTN, CAP_BTN))
        return CAP_CLOSE;
    if (w->show_minmax) {
        caption_btn_xy(w, CAP_MAX, &bx, &by);
        if (gui_hit(x, y, bx, by, CAP_BTN, CAP_BTN))
            return CAP_MAX;
        caption_btn_xy(w, CAP_MIN, &bx, &by);
        if (gui_hit(x, y, bx, by, CAP_BTN, CAP_BTN))
            return CAP_MIN;
    }
    return CAP_NONE;
}

static void draw_cap_btn(int bx, int by, const char *glyph, int enabled, int pressed)
{
    uint32_t fill = pressed ? W95_DOWN : W95_FACE;
    uint32_t hi, lo, fg;
    int tx = bx + 3, ty = by + 3;

    gui_rect(&g, bx, by, CAP_BTN, CAP_BTN, fill);
    if (pressed) {
        hi = W95_DARKSHADOW;
        lo = W95_HIGHLIGHT;
        tx++;
        ty++;
    } else {
        hi = W95_HIGHLIGHT;
        lo = W95_DARKSHADOW;
    }
    gui_rect(&g, bx, by, CAP_BTN, 1, hi);
    gui_rect(&g, bx, by, 1, CAP_BTN, hi);
    gui_rect(&g, bx, by + CAP_BTN - 1, CAP_BTN, 1, lo);
    gui_rect(&g, bx + CAP_BTN - 1, by, 1, CAP_BTN, lo);
    fg = enabled ? W95_TEXT : W95_DIM;
    gui_text(&g, tx, ty, glyph, fg, 0xFFFFFFFFu);
}

static void apply_maximize(int id)
{
    Win *w;
    int cw, ch, fx, ft, fb;
    if (id < 0 || !wins[id].open)
        return;
    w = &wins[id];
    if (!w->resizable && !w->maximized)
        return;
    if (w->maximized) {
        w->maximized = 0;
        w->x = w->rest_x;
        w->y = w->rest_y;
        w->w = w->rest_w;
        w->h = w->rest_h;
        if (w->app == APP_EXT && w->gui_id >= 0) {
            if (sys_gui_resize(w->gui_id, w->rest_cw, w->rest_ch) == 0) {
                w->gui_fb = (uint32_t *)sys_gui_fb(w->gui_id);
                w->gui_cw = w->rest_cw;
                w->gui_ch = w->rest_ch;
            }
        }
    } else {
        w->rest_x = w->x;
        w->rest_y = w->y;
        w->rest_w = w->w;
        w->rest_h = w->h;
        w->rest_cw = w->gui_cw;
        w->rest_ch = w->gui_ch;
        w->maximized = 1; /* frame helpers return 0 once set */
        w->minimized = 0;
        w->x = 0;
        w->y = 0;
        w->w = g.w;
        w->h = g.h - PANEL_H;
        if (w->app == APP_EXT && w->gui_id >= 0) {
            fx = win_frame_x(w);
            ft = win_frame_top(w);
            fb = win_frame_bot(w);
            cw = w->w - fx * 2;
            ch = w->h - (ft + TITLE_H + fb);
            if (cw < 32)
                cw = 32;
            if (ch < 32)
                ch = 32;
            if (sys_gui_resize(w->gui_id, cw, ch) == 0) {
                w->gui_fb = (uint32_t *)sys_gui_fb(w->gui_id);
                w->gui_cw = cw;
                w->gui_ch = ch;
            }
        }
    }
    dirty = 1;
}

static void apply_minimize(int id)
{
    int i;
    if (id < 0 || !wins[id].open)
        return;
    wins[id].minimized = 1;
    if (focus == id) {
        focus = -1;
        for (i = nwin - 1; i >= 0; i--) {
            int wid = zorder[i];
            if (wins[wid].open && !wins[wid].minimized) {
                focus = wid;
                break;
            }
        }
    }
    dirty = 1;
}

static void apply_restore(int id)
{
    if (id < 0 || !wins[id].open)
        return;
    wins[id].minimized = 0;
    win_raise(id);
}

static void draw_win_chrome(Win *w, int focused)
{
    uint32_t title_c = focused ? W95_TITLE : W95_TITLE_LOST;
    uint32_t title_t = focused ? W95_HIGHLIGHT : W95_TEXT;
    int fx = win_frame_x(w);
    int ft = win_frame_top(w);
    int tb_y = w->y + ft;
    int bx, by;
    int pressed;

    if (w->popup)
        return;

    if (anim_focus > 0 && focused)
        title_c = 0x000000A8u;
    gui_rect(&g, w->x, w->y, w->w, w->h, W95_FACE);
    if (!w->maximized) {
        /* Outer raised edge */
        gui_rect(&g, w->x, w->y, w->w, 1, W95_HIGHLIGHT);
        gui_rect(&g, w->x, w->y, 1, w->h, W95_HIGHLIGHT);
        gui_rect(&g, w->x, w->y + w->h - 1, w->w, 1, W95_DARKSHADOW);
        gui_rect(&g, w->x + w->w - 1, w->y, 1, w->h, W95_DARKSHADOW);
        if (w->resizable) {
            /* Second bevel so top/bottom frame matches side thickness. */
            gui_rect(&g, w->x + 1, w->y + 1, w->w - 2, 1, W95_HIGHLIGHT);
            gui_rect(&g, w->x + 1, w->y + 1, 1, w->h - 2, W95_HIGHLIGHT);
            gui_rect(&g, w->x + 1, w->y + w->h - 2, w->w - 2, 1, W95_SHADOW);
            gui_rect(&g, w->x + w->w - 2, w->y + 1, 1, w->h - 2, W95_SHADOW);
        }
    }
    /* Title sits fully inside the frame (not over the bevel). */
    gui_rect(&g, w->x + fx, tb_y, w->w - fx * 2, TITLE_H, title_c);
    gui_text(&g, w->x + fx + 4, tb_y + (TITLE_H - 8) / 2, w->title, title_t, 0xFFFFFFFFu);

    caption_btn_xy(w, CAP_CLOSE, &bx, &by);
    pressed = (cap_press == CAP_CLOSE && cap_press_id == (int)(w - wins));
    draw_cap_btn(bx, by, "x", 1, pressed);

    if (w->show_minmax) {
        caption_btn_xy(w, CAP_MAX, &bx, &by);
        pressed = (cap_press == CAP_MAX && cap_press_id == (int)(w - wins));
        /* Gray when not resizable (unless already maximized — restore stays active). */
        draw_cap_btn(bx, by, "o", w->resizable || w->maximized, pressed);

        caption_btn_xy(w, CAP_MIN, &bx, &by);
        pressed = (cap_press == CAP_MIN && cap_press_id == (int)(w - wins));
        draw_cap_btn(bx, by, "_", 1, pressed);
    }
}

static void draw_term(Win *w)
{
    int i, y;
    char prompt[96];
    draw_win_chrome(w, focus == (int)(w - wins));
    gui_rect(&g, client_ox(w), client_oy(w), w->w - win_frame_x(w) * 2,
             w->h - (client_oy(w) - w->y) - win_frame_bot(w), W95_WINDOW);
    y = client_oy(w) + 4;
    for (i = 0; i < w->nlines; i++) {
        gui_text(&g, client_ox(w) + 4, y, w->lines[i], W95_TEXT, 0xFFFFFFFFu);
        y += 10;
    }
    if (w->term_mode == TERM_RUN) {
        if (w->live[0]) {
            int caret = term_session_caret(&w->shell);
            int L = strlen_u(w->live);
            gui_text(&g, client_ox(w) + 4, y, w->live, W95_TITLE, 0xFFFFFFFFu);
            if (caret < 0 || caret > L)
                caret = L;
            gui_rect(&g, client_ox(w) + 4 + caret * 8, y + 8, 7, 2, W95_TITLE);
        }
        return;
    }
    if (w->term_mode == TERM_DONE)
        return;
    prompt[0] = '}';
    prompt[1] = ' ';
    prompt[2] = 0;
    strncpy_u(prompt + 2, w->input, (int)sizeof(prompt) - 2);
    gui_text(&g, client_ox(w) + 4, y, prompt, W95_TITLE, 0xFFFFFFFFu);
}

static void draw_files(Win *w)
{
    int i = 0, y;
    char line[64];
    draw_win_chrome(w, focus == (int)(w - wins));
    gui_rect(&g, client_ox(w), client_oy(w), w->w - win_frame_x(w) * 2,
             w->h - (client_oy(w) - w->y) - win_frame_bot(w), W95_WINDOW);
    gui_text(&g, client_ox(w) + 4, client_oy(w) + 4, w->path, W95_DIM, 0xFFFFFFFFu);
    y = client_oy(w) + 18;
    while (w->listing[i] && y + 10 < w->y + w->h - win_frame_bot(w)) {
        int j = 0;
        while (w->listing[i] && w->listing[i] != '\n' && j + 1 < 48)
            line[j++] = w->listing[i++];
        line[j] = 0;
        if (w->listing[i] == '\n') i++;
        if (line[0]) {
            gui_text(&g, client_ox(w) + 8, y, line, W95_TEXT, 0xFFFFFFFFu);
            y += 10;
        }
    }
}

static void draw_net(Win *w)
{
    int i = 0, y;
    char line[64];
    draw_win_chrome(w, focus == (int)(w - wins));
    gui_rect(&g, client_ox(w), client_oy(w), w->w - win_frame_x(w) * 2,
             w->h - (client_oy(w) - w->y) - win_frame_bot(w), W95_WINDOW);
    y = client_oy(w) + 6;
    while (w->netbuf[i] && y + 10 < w->y + w->h - win_frame_bot(w)) {
        int j = 0;
        while (w->netbuf[i] && w->netbuf[i] != '\n' && j + 1 < 48)
            line[j++] = w->netbuf[i++];
        line[j] = 0;
        if (w->netbuf[i] == '\n') i++;
        if (line[0]) {
            gui_text(&g, client_ox(w) + 6, y, line, W95_TEXT, 0xFFFFFFFFu);
            y += 12;
        }
    }
}

static void draw_about(Win *w)
{
    draw_win_chrome(w, focus == (int)(w - wins));
    gui_rect(&g, client_ox(w), client_oy(w), w->w - win_frame_x(w) * 2,
             w->h - (client_oy(w) - w->y) - win_frame_bot(w), W95_WINDOW);
    gui_text(&g, client_ox(w) + 12, client_oy(w) + 16, "zerOS desktop", W95_TITLE, 0xFFFFFFFFu);
    gui_text(&g, client_ox(w) + 12, client_oy(w) + 36, "phase 1 GUI", W95_TEXT, 0xFFFFFFFFu);
    gui_text(&g, client_ox(w) + 12, client_oy(w) + 52, "VESA + PS/2 mouse", W95_DIM, 0xFFFFFFFFu);
    gui_text(&g, client_ox(w) + 12, client_oy(w) + 68, "FAT /disk · virtio-net", W95_DIM, 0xFFFFFFFFu);
}

static int win_open_ext(int gui_id)
{
    int id, cw, ch, pid;
    uint32_t fb;
    cw = sys_gui_info(gui_id, 0);
    ch = sys_gui_info(gui_id, 1);
    fb = (uint32_t)sys_gui_info(gui_id, 2);
    pid = sys_gui_info(gui_id, 4);
    if (cw < 32 || ch < 32 || !fb || pid < 0)
        return -1;
    id = win_open(APP_EXT, 48 + nwin * 12, 48 + nwin * 10);
    if (id < 0)
        return -1;
    wins[id].gui_id = gui_id;
    wins[id].gui_pid = pid;
    wins[id].gui_fb = (uint32_t *)fb;
    wins[id].gui_cw = cw;
    wins[id].gui_ch = ch;
    wins[id].resizable =
        (sys_gui_info(gui_id, GUI_INFO_FLAGS) & GUI_FLAG_RESIZABLE) != 0;
    wins[id].popup =
        (sys_gui_info(gui_id, GUI_INFO_FLAGS) & GUI_FLAG_POPUP) != 0;
    wins[id].show_minmax =
        !wins[id].popup &&
        (sys_gui_info(gui_id, GUI_INFO_FLAGS) & GUI_FLAG_NO_MINMAX) == 0;
    if (wins[id].popup) {
        int i, j;
        wins[id].w = cw;
        wins[id].h = ch;
        wins[id].x = 0;
        wins[id].y = g.h - PANEL_H - ch;
        if (wins[id].y < 0)
            wins[id].y = 0;
        /* Popups stay off the taskbar. */
        for (i = 0; i < ntaskbar; i++) {
            if (task_order[i] == id) {
                for (j = i; j < ntaskbar - 1; j++)
                    task_order[j] = task_order[j + 1];
                ntaskbar--;
                break;
            }
        }
    } else {
        wins[id].w = cw + win_frame_x(&wins[id]) * 2;
        wins[id].h = ch + win_frame_top(&wins[id]) + TITLE_H + win_frame_bot(&wins[id]);
    }
    strncpy_u(wins[id].title, "app", sizeof(wins[id].title));
    sys_gui_get_title(gui_id, wins[id].title, (int)sizeof(wins[id].title));
    sys_gui_ack(gui_id, 0);
    return id;
}

static void draw_ext(Win *w)
{
    int x, y, cw, ch;
    uint32_t *src;
    draw_win_chrome(w, focus == (int)(w - wins));
    if (!w->gui_fb)
        return;
    cw = w->gui_cw;
    ch = w->gui_ch;
    src = w->gui_fb;
    for (y = 0; y < ch; y++) {
        int dy = client_oy(w) + y;
        if (dy < 0 || dy >= g.h - PANEL_H)
            continue;
        for (x = 0; x < cw; x++) {
            int dx = client_ox(w) + x;
            if (dx < 0 || dx >= g.w)
                continue;
            g.fb[dy * g.pitch + dx] = src[y * cw + x];
        }
    }
}

/* True if a higher window (or the panel) covers this desktop pixel. */
static int win_obscured_at(int wid, int x, int y)
{
    int i, zi = -1;
    if (y >= g.h - PANEL_H)
        return 1;
    for (i = 0; i < nwin; i++) {
        if (zorder[i] == wid) {
            zi = i;
            break;
        }
    }
    if (zi < 0)
        return 1;
    for (i = zi + 1; i < nwin; i++) {
        Win *o = &wins[zorder[i]];
        if (!o->open || o->minimized)
            continue;
        if (gui_hit(x, y, o->x, o->y, o->w, o->h))
            return 1;
    }
    return 0;
}

/* Blit client pixels only — no full-desktop clear (avoids flicker).
 * Skips pixels covered by windows above this one in z-order. */
static void blit_ext(int wid)
{
    int x, y, cw, ch;
    uint32_t *src;
    Win *w;
    if (wid < 0 || wid >= MAX_WIN)
        return;
    w = &wins[wid];
    if (!w->open || w->minimized || !w->gui_fb)
        return;
    cw = w->gui_cw;
    ch = w->gui_ch;
    src = w->gui_fb;
    for (y = 0; y < ch; y++) {
        int dy = client_oy(w) + y;
        if (dy < 0 || dy >= g.h - PANEL_H)
            continue;
        for (x = 0; x < cw; x++) {
            int dx = client_ox(w) + x;
            if (dx < 0 || dx >= g.w)
                continue;
            if (win_obscured_at(wid, dx, dy))
                continue;
            g.fb[dy * g.pitch + dx] = src[y * cw + x];
        }
    }
}

static int launch_app_argv(const char *path, const char *argv)
{
    int i;
    int afd;
    if (!path || !sys_exists(path))
        return -1;
    for (i = 0; i < MAX_WIN; i++) {
        if (launches[i].used)
            continue;
        afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
        if (afd >= 0) {
            if (argv && argv[0])
                sys_write(afd, argv, strlen_u(argv));
            sys_close(afd);
        }
        if (term_session_start(&launches[i].sess, path) != 0)
            return -1;
        launches[i].used = 1;
        return 0;
    }
    return -1;
}

static int launch_app(const char *path)
{
    return launch_app_argv(path, "");
}

static void launch_stop_pid(int pid)
{
    int i;
    if (pid < 0)
        return;
    for (i = 0; i < MAX_WIN; i++) {
        if (launches[i].used && launches[i].sess.pid == pid) {
            term_session_stop(&launches[i].sess);
            launches[i].used = 0;
            return;
        }
    }
}

static void launches_pump(void)
{
    int i;
    for (i = 0; i < MAX_WIN; i++) {
        if (!launches[i].used)
            continue;
        if (!term_session_pump(&launches[i].sess, -1, 0, 0))
            launches[i].used = 0;
    }
}

static void desk_beep(uint32_t freq, uint32_t ms)
{
    if (sound_on)
        sys_beep((int)freq, (int)ms);
}

static void load_sound_pref(void)
{
    char buf[256];
    int fd, n, i, ls;
    sound_on = 1;
    fd = sys_open("/sys/etc/gui_settings.txt", O_READ);
    if (fd < 0)
        return;
    n = sys_read(fd, buf, (int)sizeof(buf) - 1);
    sys_close(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    ls = 0;
    for (i = 0; i <= n; i++) {
        char line[64];
        int len;
        if (i < n && buf[i] != '\n' && buf[i] != '\r')
            continue;
        len = i - ls;
        if (len > 0 && buf[ls + len - 1] == '\r')
            len--;
        if (len <= 0) {
            ls = i + 1;
            continue;
        }
        if (len >= (int)sizeof(line))
            len = (int)sizeof(line) - 1;
        memcpy_u(line, buf + ls, len);
        line[len] = 0;
        ls = i + 1;
        if (line[0] == '#')
            continue;
        if (line[0] == 'b' && line[1] == 'e' && line[2] == 'e' && line[3] == 'p' &&
            line[4] == '=')
            sound_on = (line[5] != '0');
    }
}

static int desk_resolve_cmd(const char *cmdline, char *path, int pathmax,
                            char *argv, int argvmax)
{
    char work[96];
    char try[128];
    char *tok[8];
    int argc, n, i, pos, L;

    if (!cmdline || !cmdline[0] || !path || pathmax < 2)
        return -1;
    strncpy_u(work, cmdline, sizeof(work));
    argc = split_args(work, tok, 8);
    if (argc < 1)
        return -1;
    path[0] = 0;
    if (tok[0][0] == '/') {
        if (sys_exists(tok[0]))
            strncpy_u(path, tok[0], pathmax);
    } else {
        strncpy_u(try, "/sys/gui/", sizeof(try));
        n = strlen_u(try);
        strncpy_u(try + n, tok[0], (int)sizeof(try) - n);
        if (sys_exists(try))
            strncpy_u(path, try, pathmax);
        else {
            n = strlen_u(try);
            if (n + 4 < (int)sizeof(try)) {
                try[n] = '.'; try[n+1] = 'w'; try[n+2] = 'i'; try[n+3] = 'n'; try[n+4] = 0;
                if (sys_exists(try))
                    strncpy_u(path, try, pathmax);
            }
        }
        if (!path[0]) {
            strncpy_u(try, "/sys/bin/", sizeof(try));
            n = strlen_u(try);
            strncpy_u(try + n, tok[0], (int)sizeof(try) - n);
            if (sys_exists(try))
                strncpy_u(path, try, pathmax);
        }
    }
    if (!path[0])
        return -1;
    argv[0] = 0;
    if (argvmax > 0 && argc > 1) {
        pos = 0;
        for (i = 1; i < argc; i++) {
            L = strlen_u(tok[i]);
            if (pos + L + 2 >= argvmax)
                break;
            if (pos)
                argv[pos++] = ' ';
            memcpy_u(argv + pos, tok[i], L);
            pos += L;
            argv[pos] = 0;
        }
    }
    return 0;
}

static void load_desktop_icons(void)
{
    char buf[2048];
    int fd, n, i, line_start, col, row;

    ndesk = 0;
    desk_sel = -1;
    fd = sys_open("/sys/etc/gui_desktop.txt", O_READ);
    if (fd < 0)
        return;
    n = sys_read(fd, buf, (int)sizeof(buf) - 1);
    sys_close(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    line_start = 0;
    col = 0;
    row = 0;
    for (i = 0; i <= n; i++) {
        char line[160];
        int len, k, tab;
        if (i < n && buf[i] != '\n' && buf[i] != '\r')
            continue;
        len = i - line_start;
        if (len > 0 && buf[line_start + len - 1] == '\r')
            len--;
        if (len <= 0) {
            line_start = i + 1;
            continue;
        }
        if (len >= (int)sizeof(line))
            len = (int)sizeof(line) - 1;
        memcpy_u(line, buf + line_start, len);
        line[len] = 0;
        line_start = i + 1;
        if (!line[0] || line[0] == '#')
            continue;
        tab = -1;
        for (k = 0; line[k]; k++) {
            if (line[k] == '\t') { tab = k; break; }
        }
        if (tab < 0)
            continue;
        line[tab] = 0;
        if (ndesk >= DESK_MAX)
            break;
        strncpy_u(desk_icons[ndesk].name, line, 40);
        if (desk_resolve_cmd(line + tab + 1, desk_icons[ndesk].path,
                             (int)sizeof(desk_icons[ndesk].path),
                             desk_icons[ndesk].argv,
                             (int)sizeof(desk_icons[ndesk].argv)) != 0) {
            desk_icons[ndesk].path[0] = 0;
            desk_icons[ndesk].argv[0] = 0;
        }
        desk_icons[ndesk].used = 1;
        desk_icons[ndesk].w = ICON_W;
        desk_icons[ndesk].h = ICON_H;
        desk_icons[ndesk].x = ICON_ORIGIN_X + col * (ICON_W + ICON_GAP_X);
        desk_icons[ndesk].y = ICON_ORIGIN_Y + row * (ICON_H + ICON_GAP_Y);
        desk_icons[ndesk].selected = 0;
        ndesk++;
        row++;
        if (row >= 8) {
            row = 0;
            col++;
        }
    }
}

static void draw_wallpaper(void)
{
    int x, y;
    /* Teal desk with subtle diagonal hash (Win95-ish). */
    for (y = 0; y < g.h - PANEL_H; y++) {
        for (x = 0; x < g.w; x++) {
            uint32_t c = W95_DESK;
            if (((x + y) & 7) == 0)
                c = 0x00007070u;
            g.fb[y * g.pitch + x] = c;
        }
    }
}

static void draw_icon_glyph(int x, int y, int sel)
{
    uint32_t face = sel ? W95_TITLE : 0x00C0C000u;
    uint32_t hi = W95_HIGHLIGHT;
    uint32_t lo = W95_DARKSHADOW;
    gui_rect(&g, x + 16, y + 4, 32, 28, face);
    gui_rect(&g, x + 16, y + 4, 32, 1, hi);
    gui_rect(&g, x + 16, y + 4, 1, 28, hi);
    gui_rect(&g, x + 16, y + 31, 32, 1, lo);
    gui_rect(&g, x + 47, y + 4, 1, 28, lo);
    gui_rect(&g, x + 20, y + 10, 24, 2, W95_WINDOW);
    gui_rect(&g, x + 20, y + 16, 18, 2, W95_WINDOW);
}

static void draw_desktop_icons(void)
{
    int i;
    for (i = 0; i < ndesk; i++) {
        DeskIcon *ic = &desk_icons[i];
        int tw;
        if (!ic->used)
            continue;
        if (ic->selected)
            gui_rect(&g, ic->x, ic->y, ic->w, ic->h, W95_TITLE);
        draw_icon_glyph(ic->x, ic->y, ic->selected);
        tw = (int)strlen_u(ic->name) * 8;
        if (tw > ic->w - 4)
            tw = ic->w - 4;
        gui_text(&g, ic->x + (ic->w - tw) / 2, ic->y + 36, ic->name,
                 W95_HIGHLIGHT, 0xFFFFFFFFu);
    }
}

static int desk_icon_at(int x, int y)
{
    int i;
    for (i = 0; i < ndesk; i++) {
        if (!desk_icons[i].used)
            continue;
        if (gui_hit(x, y, desk_icons[i].x, desk_icons[i].y,
                    desk_icons[i].w, desk_icons[i].h))
            return i;
    }
    return -1;
}

static void desk_launch_icon(int i)
{
    if (i < 0 || i >= ndesk || !desk_icons[i].path[0])
        return;
    desk_beep(880, 40);
    launch_app_argv(desk_icons[i].path, desk_icons[i].argv);
}

static void restore_from_max_for_drag(Win *w, int mx, int my)
{
    int fx, ft, fb;
    if (!w || !w->maximized)
        return;
    w->maximized = 0;
    w->x = w->rest_x;
    w->y = w->rest_y;
    w->w = w->rest_w;
    w->h = w->rest_h;
    if (w->app == APP_EXT && w->gui_id >= 0) {
        if (sys_gui_resize(w->gui_id, w->rest_cw, w->rest_ch) == 0) {
            w->gui_fb = (uint32_t *)sys_gui_fb(w->gui_id);
            w->gui_cw = w->rest_cw;
            w->gui_ch = w->rest_ch;
        }
    }
    /* Keep cursor over title bar ratio. */
    if (w->w > 0)
        drag_off_x = (mx - w->x);
    if (drag_off_x < 0)
        drag_off_x = 0;
    if (drag_off_x > w->w)
        drag_off_x = w->w / 2;
    drag_off_y = TITLE_H / 2;
    (void)fx; (void)ft; (void)fb; (void)my;
}

static int resize_hit_edges(Win *w, int x, int y)
{
    int e = RZ_NONE;
    int fx, ft, fb;
    if (!w || !w->resizable || w->popup || w->maximized)
        return RZ_NONE;
    fx = win_frame_x(w);
    ft = win_frame_top(w);
    fb = win_frame_bot(w);
    if (gui_hit(x, y, w->x, w->y, fx + 2, w->h))
        e |= RZ_L;
    if (gui_hit(x, y, w->x + w->w - fx - 2, w->y, fx + 2, w->h))
        e |= RZ_R;
    if (gui_hit(x, y, w->x, w->y, w->w, ft + 2))
        e |= RZ_T;
    if (gui_hit(x, y, w->x, w->y + w->h - fb - 2, w->w, fb + 2))
        e |= RZ_B;
    /* Prefer corners via BR grip. */
    if (gui_hit(x, y, w->x + w->w - RESIZE_GRIP, w->y + w->h - RESIZE_GRIP,
                RESIZE_GRIP, RESIZE_GRIP))
        e = RZ_R | RZ_B;
    return e;
}



/* Taskbar chrome: [Z] opens Launchpad, X exits the desktop. */
static void panel_on_launchpad(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    desk_beep(520, 30);
    launch_app("/sys/gui/launchpad.win");
}

static void panel_on_exit(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    running = 0;
}

static void panel_on_task(WButton *b, void *userdata)
{
    int id = (int)(unsigned long)userdata;
    (void)b;
    if (id < 0 || id >= MAX_WIN || !wins[id].open)
        return;
    if (wins[id].minimized)
        apply_restore(id);
    else if (focus == id)
        apply_minimize(id);
    else
        win_raise(id);
    dirty = 1;
}

static void taskbar_sync(void)
{
    int i, nshown = 0, x, avail, bw, gap = 4;
    int exit_w = (int)strlen_u("X") * 8 + 16;
    int right = g.w - 8 - exit_w - 8;
    int ids[MAX_WIN];

    if (!panel_ready)
        return;
    for (i = 0; i < ntaskbar; i++) {
        int wid = task_order[i];
        if (wid >= 0 && wid < MAX_WIN && wins[wid].open && !wins[wid].popup)
            ids[nshown++] = wid;
    }
    x = panel_apps_end_x + 8;
    avail = right - x;
    if (avail < 0)
        avail = 0;
    if (nshown > 0) {
        bw = (avail - gap * (nshown - 1)) / nshown;
        if (bw > 140)
            bw = 140;
        if (bw < 48)
            bw = 48;
    } else {
        bw = 48;
    }
    for (i = 0; i < MAX_WIN; i++) {
        Widget *ww = wbutton_widget(&task_btns[i]);
        if (i < nshown) {
            int wid = ids[i];
            int active = (focus == wid && !wins[wid].minimized);
            strncpy_u(task_labels[i], wins[wid].title, sizeof(task_labels[i]));
            if (!task_labels[i][0])
                strncpy_u(task_labels[i], "window", sizeof(task_labels[i]));
            wbutton_set_text(&task_btns[i], task_labels[i]);
            wbutton_set_handler(&task_btns[i], panel_on_task,
                                (void *)(unsigned long)wid);
            wbutton_set_sticky(&task_btns[i], active);
            widget_set_pos(ww, wpos_abs(x, 5, WANCHOR_TL));
            widget_set_size(ww, wsize_abs(bw, 22));
            ww->visible = 1;
            x += bw + gap;
        } else {
            ww->visible = 0;
            wbutton_set_sticky(&task_btns[i], 0);
        }
    }
    widget_apply_box(wframe_widget(&panel), 0, g.h - PANEL_H, g.w, PANEL_H);
    widget_layout(wframe_widget(&panel));
}

static void panel_init_widgets(void)
{
    int i;
    wframe_init(&panel, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(g.w, PANEL_H),
                W95_FACE, W95_DARKSHADOW, WFRAME_RAISED);
    wframe_set_active(&panel, 1);

    wbutton_init(&panel_btns[PANEL_Z], wpos_abs(8, 5, WANCHOR_TL),
                 wsize_abs((int)strlen_u("[Z]") * 8 + 12, 22), "[Z]");
    wbutton_set_style(&panel_btns[PANEL_Z], WBTN_RAISED, WBTN_SUNKEN);
    wbutton_set_handler(&panel_btns[PANEL_Z], panel_on_launchpad, 0);
    widget_add_child(wframe_widget(&panel), wbutton_widget(&panel_btns[PANEL_Z]));
    panel_apps_end_x = 8 + (int)strlen_u("[Z]") * 8 + 12;

    for (i = 0; i < MAX_WIN; i++) {
        task_labels[i][0] = 0;
        wbutton_init(&task_btns[i], wpos_abs(0, 5, WANCHOR_TL),
                     wsize_abs(80, 22), task_labels[i]);
        wbutton_set_style(&task_btns[i], WBTN_RAISED, WBTN_SUNKEN);
        wbutton_widget(&task_btns[i])->visible = 0;
        widget_add_child(wframe_widget(&panel), wbutton_widget(&task_btns[i]));
    }

    wbutton_init(&panel_btns[PANEL_EXIT], wpos_abs(8, 5, WANCHOR_BR),
                 wsize_abs((int)strlen_u("X") * 8 + 16, 22), "X");
    wbutton_set_style(&panel_btns[PANEL_EXIT], WBTN_RAISED, WBTN_SUNKEN);
    wbutton_set_handler(&panel_btns[PANEL_EXIT], panel_on_exit, 0);
    widget_add_child(wframe_widget(&panel), wbutton_widget(&panel_btns[PANEL_EXIT]));

    widget_apply_box(wframe_widget(&panel), 0, g.h - PANEL_H, g.w, PANEL_H);
    panel_ready = 1;
    taskbar_sync();
}

static void draw_panel(void)
{
    if (!panel_ready)
        return;
    taskbar_sync();
    widget_draw(wframe_widget(&panel), &g);
}

static int panel_hit(int x, int y)
{
    int i;
    if (!panel_ready || y < g.h - PANEL_H)
        return -1;
    for (i = 0; i < PANEL_BTN_N; i++) {
        if (widget_hit(wbutton_widget(&panel_btns[i]), x, y))
            return i;
    }
    for (i = 0; i < MAX_WIN; i++) {
        if (wbutton_widget(&task_btns[i])->visible &&
            widget_hit(wbutton_widget(&task_btns[i]), x, y))
            return PANEL_BTN_N + i;
    }
    return -1;
}

static void panel_feed_pointer(int mx, int my)
{
    int i, prev_hot;
    if (!panel_ready)
        return;
    taskbar_sync();
    prev_hot = hover_btn;
    hover_btn = -1;
    for (i = 0; i < PANEL_BTN_N; i++) {
        int st0 = panel_btns[i].stage;
        wbutton_input(&panel_btns[i], mx, my, mbtn);
        if (panel_btns[i].hot)
            hover_btn = i;
        if (wbutton_was_clicked(&panel_btns[i])) {
            if (panel_btns[i].on_click)
                panel_btns[i].on_click(&panel_btns[i], panel_btns[i].userdata);
            wbutton_ack_click(&panel_btns[i]);
            dirty = 1;
        }
        if (panel_btns[i].stage != st0)
            dirty = 1;
    }
    for (i = 0; i < MAX_WIN; i++) {
        int st0 = task_btns[i].stage;
        wbutton_input(&task_btns[i], mx, my, mbtn);
        if (task_btns[i].hot)
            hover_btn = PANEL_BTN_N + i;
        if (wbutton_was_clicked(&task_btns[i])) {
            if (task_btns[i].on_click)
                task_btns[i].on_click(&task_btns[i], task_btns[i].userdata);
            wbutton_ack_click(&task_btns[i]);
            dirty = 1;
        }
        if (task_btns[i].stage != st0)
            dirty = 1;
    }
    if (hover_btn != prev_hot)
        dirty = 1;
}

static void redraw(void)
{
    int i;
    cur_saved = 0;
    draw_wallpaper();
    draw_desktop_icons();
    /* Keep desk_root layout for hit geometry; skip solid fill draw. */
    for (i = 0; i < nwin; i++) {
        Win *w = &wins[zorder[i]];
        if (!w->open || w->minimized) continue;
        if (w->app == APP_TERM) draw_term(w);
        else if (w->app == APP_FILES) draw_files(w);
        else if (w->app == APP_NET) draw_net(w);
        else if (w->app == APP_ABOUT) draw_about(w);
        else if (w->app == APP_EXT) draw_ext(w);
    }
    draw_panel();
    draw_alttab_overlay();
    cursor_paint(mx, my);
    if (anim_focus > 0) {
        anim_focus--;
        dirty = 1;
    }
    gui_present(&g);
}

static int top_win_at(int x, int y)
{
    int i;
    for (i = nwin - 1; i >= 0; i--) {
        Win *w = &wins[zorder[i]];
        if (!w->open || w->minimized) continue;
        if (gui_hit(x, y, w->x, w->y, w->w, w->h)) return zorder[i];
    }
    return -1;
}

static void files_activate_line(Win *w, int line_idx)
{
    int i = 0, cur = 0, j, n, p, k;
    char name[64];
    char next[96];
    if (!w || line_idx < 0) return;
    while (w->listing[i]) {
        j = 0;
        while (w->listing[i] && w->listing[i] != '\n' && j + 1 < (int)sizeof(name))
            name[j++] = w->listing[i++];
        name[j] = 0;
        if (w->listing[i] == '\n') i++;
        if (!name[0]) continue;
        if (cur == line_idx) {
            if (!strcmp_u(name, ".")) return;
            if (!strcmp_u(name, "..")) {
                n = strlen_u(w->path);
                if (n <= 1) {
                    strncpy_u(w->path, "/", sizeof(w->path));
                } else {
                    if (w->path[n - 1] == '/') n--;
                    while (n > 1 && w->path[n - 1] != '/') n--;
                    if (n <= 1) strncpy_u(w->path, "/", sizeof(w->path));
                    else w->path[n] = 0;
                }
                files_refresh(w);
                return;
            }
            p = 0;
            n = strlen_u(w->path);
            while (p < n && p + 1 < (int)sizeof(next)) {
                next[p] = w->path[p];
                p++;
            }
            if (p > 0 && next[p - 1] != '/' && p + 1 < (int)sizeof(next))
                next[p++] = '/';
            k = 0;
            while (name[k] && p + 1 < (int)sizeof(next)) next[p++] = name[k++];
            next[p] = 0;
            if (sys_listdir(next, w->listing, sizeof(w->listing)) >= 0)
                strncpy_u(w->path, next, sizeof(w->path));
            else
                files_refresh(w);
            return;
        }
        cur++;
    }
}

static void dismiss_popups(int except_id)
{
    int i;
    for (i = 0; i < MAX_WIN; i++) {
        if (!wins[i].open || !wins[i].popup || i == except_id)
            continue;
        request_win_close(i);
    }
}

static void on_click(int x, int y)
{
    int id;
    int cap;
    if (alt_tab_active)
        alt_tab_commit();
    id = top_win_at(x, y);
    /* Outside a popup (desktop, panel, or another window) dismisses Start. */
    if (panel_hit(x, y) >= 0) {
        dismiss_popups(-1);
        return;
    }
    if (id < 0 || !wins[id].popup)
        dismiss_popups(-1);
    if (id < 0) {
        int ii = desk_icon_at(x, y);
        int j;
        for (j = 0; j < ndesk; j++)
            desk_icons[j].selected = 0;
        if (ii >= 0) {
            /* Second click on already-selected icon launches (dbl-click). */
            if (desk_sel == ii && last_icon_click_i == ii) {
                desk_icons[ii].selected = 1;
                desk_launch_icon(ii);
                last_icon_click_i = -1;
            } else {
                desk_icons[ii].selected = 1;
                desk_sel = ii;
                last_icon_click_i = ii;
            }
            dirty = 1;
        } else {
            desk_sel = -1;
            last_icon_click_i = -1;
            dirty = 1;
        }
        return;
    }
    win_raise(id);
    {
        Win *w = &wins[id];
        int edges;
        cap = caption_hit(w, x, y);
        if (cap != CAP_NONE) {
            /* Disabled maximize: gray, no press. */
            if (cap == CAP_MAX && !w->resizable && !w->maximized)
                return;
            cap_press = cap;
            cap_press_id = id;
            dirty = 1;
            return;
        }
        edges = resize_hit_edges(w, x, y);
        if (edges) {
            resizing = 1;
            resize_edges = edges;
            resize_off_x = x;
            resize_off_y = y;
            resize_base_cw = w->gui_cw;
            resize_base_ch = w->gui_ch;
            resize_base_x = w->x;
            resize_base_y = w->y;
            resize_base_w = w->w;
            resize_base_h = w->h;
            if (w->maximized)
                restore_from_max_for_drag(w, x, y);
            return;
        }
        if (!w->popup &&
            gui_hit(x, y, w->x, w->y, w->w, win_frame_top(w) + TITLE_H)) {
            dragging = 1;
            drag_off_x = x - w->x;
            drag_off_y = y - w->y;
            if (w->maximized)
                restore_from_max_for_drag(w, x, y);
            return;
        }
        /* Forward clicks into client content (client-local coords).
         * Button state itself is streamed in forward_client_pointer(). */
        if (w->app == APP_EXT && w->gui_id >= 0) {
            int cx = x - client_ox(w);
            int cy = y - client_oy(w);
            if (cx >= 0 && cy >= 0 && cx < w->gui_cw && cy < w->gui_ch)
                return;
        }
        if (w->app == APP_FILES &&
            gui_hit(x, y, client_ox(w), client_oy(w) + 18,
                    w->w - win_frame_x(w) * 2,
                    w->h - (client_oy(w) - w->y) - win_frame_bot(w) - 18)) {
            int line = (y - (client_oy(w) + 18)) / 10;
            files_activate_line(w, line);
        }
    }
}

static void caption_release(int x, int y)
{
    int id = cap_press_id;
    int which = cap_press;
    int hit;
    cap_press = CAP_NONE;
    cap_press_id = -1;
    dirty = 1;
    if (which == CAP_NONE || id < 0 || !wins[id].open)
        return;
    hit = caption_hit(&wins[id], x, y);
    if (hit != which)
        return;
    if (which == CAP_CLOSE)
        request_win_close(id);
    else if (which == CAP_MAX)
        apply_maximize(id);
    else if (which == CAP_MIN)
        apply_minimize(id);
}

static void alt_tab_cycle(void)
{
    int candidates[MAX_WIN];
    int nc = 0;
    int i, cur = 0;
    for (i = 0; i < nwin; i++) {
        int id = zorder[i];
        if (!wins[id].open || wins[id].popup)
            continue;
        candidates[nc++] = id;
    }
    if (nc < 1)
        return;
    if (!alt_tab_active) {
        alt_tab_active = 1;
        alt_tab_idx = nc - 1;
        for (i = 0; i < nc; i++) {
            if (candidates[i] == focus) {
                alt_tab_idx = i;
                break;
            }
        }
    }
    alt_tab_idx = (alt_tab_idx + 1) % nc;
    cur = candidates[alt_tab_idx];
    if (wins[cur].minimized)
        wins[cur].minimized = 0;
    win_raise(cur);
    dirty = 1;
}

static void alt_tab_commit(void)
{
    alt_tab_active = 0;
}

static void draw_alttab_overlay(void)
{
    int i, nc = 0;
    int bx, by, bw, bh;
    char line[40];
    if (!alt_tab_active)
        return;
    for (i = 0; i < nwin; i++) {
        if (wins[zorder[i]].open && !wins[zorder[i]].popup)
            nc++;
    }
    if (nc < 1)
        return;
    bw = 220;
    bh = 16 + nc * 14;
    bx = (g.w - bw) / 2;
    by = (g.h - PANEL_H - bh) / 2;
    gui_rect(&g, bx, by, bw, bh, W95_FACE);
    gui_rect(&g, bx, by, bw, 1, W95_HIGHLIGHT);
    gui_rect(&g, bx, by, 1, bh, W95_HIGHLIGHT);
    gui_rect(&g, bx, by + bh - 1, bw, 1, W95_DARKSHADOW);
    gui_rect(&g, bx + bw - 1, by, 1, bh, W95_DARKSHADOW);
    nc = 0;
    for (i = 0; i < nwin; i++) {
        int id = zorder[i];
        uint32_t fg;
        if (!wins[id].open || wins[id].popup)
            continue;
        fg = (id == focus) ? W95_TITLE : W95_TEXT;
        strncpy_u(line, wins[id].title, sizeof(line));
        gui_text(&g, bx + 8, by + 8 + nc * 14, line, fg, 0xFFFFFFFFu);
        nc++;
    }
}

/* Stream pointer + button bits into the focused GUI client.
 * Always forward while button is held so drags/releases are not lost
 * when the cursor leaves the client rect. */
static void forward_client_pointer(void)
{
    Win *w;
    int cx, cy, inside;
    static int client_btn;
    if (focus < 0 || !wins[focus].open || wins[focus].app != APP_EXT)
        return;
    w = &wins[focus];
    if (w->gui_id < 0)
        return;
    cx = mx - client_ox(w);
    cy = my - client_oy(w);
    inside = (cx >= 0 && cy >= 0 && cx < w->gui_cw && cy < w->gui_ch);
    if (inside) {
        if (cx < 0)
            cx = 0;
        if (cy < 0)
            cy = 0;
        if (cx >= w->gui_cw)
            cx = w->gui_cw - 1;
        if (cy >= w->gui_ch)
            cy = w->gui_ch - 1;
        sys_gui_post_mouse(w->gui_id, cx, cy, mbtn & MOUSE_BTN_MASK);
        client_btn = mbtn & MOUSE_BTN_LEFT;
    } else if (client_btn || (mbtn & MOUSE_BTN_LEFT)) {
        /* Clamp to edge while dragging; always deliver button-up. */
        if (cx < 0)
            cx = 0;
        if (cy < 0)
            cy = 0;
        if (cx >= w->gui_cw)
            cx = w->gui_cw - 1;
        if (cy >= w->gui_ch)
            cy = w->gui_ch - 1;
        sys_gui_post_mouse(w->gui_id, cx, cy, mbtn & MOUSE_BTN_MASK);
        client_btn = mbtn & MOUSE_BTN_LEFT;
    }
}

static void apply_resize_drag(void)
{
    Win *w;
    int nw, nh, nx, ny;
    int fx, ft, fb;
    uint32_t fbp;
    int dx, dy;
    if (!resizing || focus < 0 || !wins[focus].open)
        return;
    w = &wins[focus];
    if (!w->resizable || w->gui_id < 0)
        return;
    fx = win_frame_x(w);
    ft = win_frame_top(w);
    fb = win_frame_bot(w);
    dx = mx - resize_off_x;
    dy = my - resize_off_y;
    nx = resize_base_x;
    ny = resize_base_y;
    nw = resize_base_cw;
    nh = resize_base_ch;
    if (resize_edges & RZ_R)
        nw = resize_base_cw + dx;
    if (resize_edges & RZ_B)
        nh = resize_base_ch + dy;
    if (resize_edges & RZ_L) {
        nw = resize_base_cw - dx;
        nx = resize_base_x + dx;
    }
    if (resize_edges & RZ_T) {
        nh = resize_base_ch - dy;
        ny = resize_base_y + dy;
    }
    if (nw < 64)
        nw = 64;
    if (nh < 40)
        nh = 40;
    if (nw > g.w - fx * 2)
        nw = g.w - fx * 2;
    if (nh > g.h - PANEL_H - (ft + TITLE_H + fb))
        nh = g.h - PANEL_H - (ft + TITLE_H + fb);
    if (nx < 0)
        nx = 0;
    if (ny < 0)
        ny = 0;
    if (nw == w->gui_cw && nh == w->gui_ch && nx == w->x && ny == w->y)
        return;
    if (sys_gui_resize(w->gui_id, nw, nh) != 0)
        return;
    fbp = sys_gui_fb(w->gui_id);
    w->gui_fb = (uint32_t *)fbp;
    w->gui_cw = nw;
    w->gui_ch = nh;
    w->x = nx;
    w->y = ny;
    w->w = nw + fx * 2;
    w->h = nh + ft + TITLE_H + fb;
    dirty = 1;
}

static void term_submit(Win *w)
{
    char cmd[TERM_LINE];
    if (w->term_mode != TERM_PROMPT || !w->input[0])
        return;
    strncpy_u(cmd, w->input, sizeof(cmd));
    term_push(w, w->input);
    w->inlen = 0;
    w->input[0] = 0;
    term_exec(w, cmd);
}

static void on_key(int key)
{
    Win *w;
    if (key == KEY_ALT_TAB) {
        alt_held = 1;
        alt_tab_cycle();
        return;
    }
    /* Bare Tab goes to focused apps; only cycle when desktop has focus. */
    if (key == 9) {
        int to_app = 0;
        if (focus >= 0 && wins[focus].open &&
            (wins[focus].app == APP_EXT || wins[focus].app == APP_TERM))
            to_app = 1;
        if (!to_app) {
            alt_tab_cycle();
            return;
        }
    }

    if (focus >= 0 && wins[focus].open && wins[focus].app == APP_EXT) {
        w = &wins[focus];
        if (key == KEY_ALT_F4) {
            request_win_close(focus);
            dirty = 1;
            return;
        }
        if (w->gui_id >= 0)
            sys_gui_post(w->gui_id, INP_KEY, key);
        return;
    }

    if (focus >= 0 && wins[focus].open && wins[focus].app == APP_TERM) {
        w = &wins[focus];
        if (w->term_mode == TERM_RUN) {
            term_drain(w, (key == '\r') ? '\n' : key);
            dirty = 1;
            return;
        }
        if (w->term_mode == TERM_DONE)
            return;
        if (key == '\r' || key == '\n') {
            term_submit(w);
            dirty = 1;
            return;
        }
        if (key == 8 || key == 127) {
            if (w->inlen > 0) {
                w->inlen--;
                w->input[w->inlen] = 0;
                dirty = 1;
            }
            return;
        }
        if (key >= 32 && key < 127 && w->inlen + 1 < TERM_LINE) {
            w->input[w->inlen++] = (char)key;
            w->input[w->inlen] = 0;
            dirty = 1;
        }
        return;
    }

    /* Files: Enter opens selected line under cursor. */
    if (focus >= 0 && wins[focus].open && wins[focus].app == APP_FILES) {
        w = &wins[focus];
        if (key == '\r' || key == '\n' || key == ' ') {
            if (gui_hit(mx, my, client_ox(w), client_oy(w) + 18,
                        w->w - win_frame_x(w) * 2,
                        w->h - (client_oy(w) - w->y) - win_frame_bot(w) - 18)) {
                int line = (my - (client_oy(w) + 18)) / 10;
                files_activate_line(w, line);
            } else {
                on_click(mx, my);
            }
            return;
        }
    }

    if (key == ' ')
        on_click(mx, my);
    /* Esc never quits the desktop — only panel Exit. */
}

int main(void)
{
    SysInputEvent ev;
    int i;

    /* Launched from msh→desktop we inherit cons_enable; every println
     * would yield back to desktop. Detach so the compositor can run. */
    sys_cons_detach();

    if (gui_init(&g, SCREEN_W, SCREEN_H) != 0) {
        println("wm: framebuffer init failed");
        return 1;
    }
    wframe_init(&desk_root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_DESK, 0, WFRAME_FLAT);
    wlabel_init(&desk_title, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(0, 8),
                "", W95_HIGHLIGHT, 0xFFFFFFFFu);
    wlabel_init(&desk_sub, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(0, 8),
                "", W95_WINDOW, 0xFFFFFFFFu);
    widget_layout_root(wframe_widget(&desk_root), SCREEN_W, SCREEN_H);
    load_sound_pref();
    load_desktop_icons();
    panel_init_widgets();
    if (sys_gui_server(1) != 0) {
        println("wm: display server register failed");
        gui_shutdown(&g);
        return 1;
    }
    mx = SCREEN_W / 2;
    my = SCREEN_H / 2;
    nwin = 0;
    ntaskbar = 0;
    focus = -1;
    running = 1;
    dragging = 0;
    resizing = 0;
    dirty = 1;
    for (i = 0; i < MAX_WIN; i++)
        launches[i].used = 0;
    dirty = 1;
    redraw(); /* paint desktop before blocking beeps */
    desk_beep(660, 60);
    desk_beep(880, 80);
    for (i = 0; i < 48; i++) {
        int gid;
        launches_pump();
        while ((gid = sys_gui_next()) >= 0)
            win_open_ext(gid);
    }
    dirty = 1;
    redraw();
    while (running) {
        int n = 0;
        int gid;
        int had_damage = 0;

        /* Client-requested launches (Files opens notepad / .win via wm). */
        {
            char lbuf[256];
            while (sys_gui_take_launch(lbuf, (int)sizeof(lbuf)) > 0) {
                char *lpath = lbuf;
                char *largv = lbuf + strlen_u(lbuf) + 1;
                launch_app_argv(lpath, largv);
            }
        }

        /* Adopt windows created by terminal-hosted GUI clients. */
        while ((gid = sys_gui_next()) >= 0) {
            win_open_ext(gid);
            dirty = 1;
        }

        while (n++ < 32 && sys_input_poll(&ev) > 0) {
            if (ev.type == INP_KEY) {
                on_key(ev.key);
                dirty = 1;
            } else if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
                if ((mbtn & 1) && !(prev_btn & 1)) {
                    on_click(mx, my);
                    dirty = 1;
                }
                if (!(mbtn & 1) && (prev_btn & 1)) {
                    if (cap_press != CAP_NONE)
                        caption_release(mx, my);
                    dragging = 0;
                    resizing = 0;
                    resize_edges = RZ_NONE;
                } else if (!(mbtn & 1)) {
                    dragging = 0;
                    resizing = 0;
                    resize_edges = RZ_NONE;
                }
                if (dragging && focus >= 0 && wins[focus].open) {
                    wins[focus].x = mx - drag_off_x;
                    wins[focus].y = my - drag_off_y;
                    if (wins[focus].x < 0) wins[focus].x = 0;
                    if (wins[focus].y < 0) wins[focus].y = 0;
                    if (wins[focus].y > g.h - PANEL_H - TITLE_H)
                        wins[focus].y = g.h - PANEL_H - TITLE_H;
                    dirty = 1;
                }
                if (resizing)
                    apply_resize_drag();
                forward_client_pointer();
                prev_btn = mbtn;
            }
        }
        if (!running) break;
        /* Sync pointer; do not drop keyboard events. */
        if (sys_input_poll(&ev) > 0) {
            if (ev.type == INP_KEY) {
                on_key(ev.key);
                dirty = 1;
            } else if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
                if ((mbtn & 1) && !(prev_btn & 1)) {
                    on_click(mx, my);
                    dirty = 1;
                }
                if (!(mbtn & 1) && (prev_btn & 1)) {
                    if (cap_press != CAP_NONE)
                        caption_release(mx, my);
                    dragging = 0;
                    resizing = 0;
                    resize_edges = RZ_NONE;
                } else if (!(mbtn & 1)) {
                    dragging = 0;
                    resizing = 0;
                }
                if (dragging && focus >= 0 && wins[focus].open) {
                    wins[focus].x = mx - drag_off_x;
                    wins[focus].y = my - drag_off_y;
                    dirty = 1;
                }
                if (resizing)
                    apply_resize_drag();
                forward_client_pointer();
                prev_btn = mbtn;
            }
        } else {
            mx = ev.x;
            my = ev.y;
            mbtn = ev.buttons;
        }
        panel_feed_pointer(mx, my);
        if (!(mbtn & 1) && (prev_btn & 1) && cap_press != CAP_NONE)
            caption_release(mx, my);
        if (!(mbtn & 1)) {
            dragging = 0;
            resizing = 0;
        }
        if (dragging && focus >= 0 && wins[focus].open) {
            wins[focus].x = mx - drag_off_x;
            wins[focus].y = my - drag_off_y;
            dirty = 1;
        }
        if (resizing)
            apply_resize_drag();

        /* Give CPU to panel-launched and gterm-hosted children. */
        launches_pump();
        for (i = 0; i < MAX_WIN; i++) {
            if (!wins[i].open || wins[i].term_mode != TERM_RUN)
                continue;
            if (term_drain(&wins[i], -1))
                dirty = 1;
        }

        /* Adopt windows created during this pump (same frame). */
        while ((gid = sys_gui_next()) >= 0) {
            win_open_ext(gid);
            dirty = 1;
        }

        /* Reap dead clients; note damage after they have painted. */
        had_damage = 0;
        for (i = 0; i < MAX_WIN; i++) {
            if (!wins[i].open || wins[i].app != APP_EXT || wins[i].gui_id < 0)
                continue;
            if (sys_gui_info(wins[i].gui_id, 0) < 0) {
                wins[i].gui_id = -1;
                win_close(i);
                dirty = 1;
                continue;
            }
            {
                char t[32];
                sys_gui_get_title(wins[i].gui_id, t, (int)sizeof(t));
                if (strcmp_u(t, wins[i].title) != 0) {
                    strncpy_u(wins[i].title, t, sizeof(wins[i].title));
                    dirty = 1;
                }
            }
            if (sys_gui_info(wins[i].gui_id, 3) > 0)
                had_damage = 1;
            {
                int flags = sys_gui_info(wins[i].gui_id, GUI_INFO_FLAGS);
                int r = (flags & GUI_FLAG_RESIZABLE) != 0;
                int mm = (flags & GUI_FLAG_NO_MINMAX) == 0;
                if (r != wins[i].resizable) {
                    wins[i].resizable = r;
                    wins[i].w = wins[i].gui_cw + win_frame_x(&wins[i]) * 2;
                    wins[i].h = wins[i].gui_ch + win_frame_top(&wins[i]) +
                                TITLE_H + win_frame_bot(&wins[i]);
                    dirty = 1;
                }
                if (mm != wins[i].show_minmax) {
                    wins[i].show_minmax = mm;
                    dirty = 1;
                }
            }
        }

        prev_btn = mbtn;

        if (dirty) {
            dirty = 0;
            redraw();
            for (i = 0; i < MAX_WIN; i++) {
                if (wins[i].open && wins[i].app == APP_EXT && wins[i].gui_id >= 0 &&
                    sys_gui_info(wins[i].gui_id, 3) > 0)
                    sys_gui_ack(wins[i].gui_id, 1);
            }
        } else if (had_damage) {
            int dx0 = g.w, dy0 = g.h, dx1 = 0, dy1 = 0;
            int any = 0;
            cursor_restore();
            /* Bottom→top so overlapping damaged clients stack correctly. */
            for (i = 0; i < nwin; i++) {
                int wid = zorder[i];
                Win *w;
                if (!wins[wid].open || wins[wid].app != APP_EXT ||
                    wins[wid].gui_id < 0)
                    continue;
                if (sys_gui_info(wins[wid].gui_id, 3) <= 0)
                    continue;
                w = &wins[wid];
                blit_ext(wid);
                sys_gui_ack(wins[wid].gui_id, 1);
                if (w->x < dx0) dx0 = w->x;
                if (w->y < dy0) dy0 = w->y;
                if (w->x + w->w > dx1) dx1 = w->x + w->w;
                if (w->y + w->h > dy1) dy1 = w->y + w->h;
                any = 1;
            }
            cursor_paint(mx, my);
            if (any) {
                if (mx < dx0) dx0 = mx;
                if (my < dy0) dy0 = my;
                if (mx + CUR_W > dx1) dx1 = mx + CUR_W;
                if (my + CUR_H > dy1) dy1 = my + CUR_H;
                if (dx0 < 0) dx0 = 0;
                if (dy0 < 0) dy0 = 0;
                if (dx1 > g.w) dx1 = g.w;
                if (dy1 > g.h) dy1 = g.h;
                if (dx1 > dx0 && dy1 > dy0)
                    gui_present_rect(&g, dx0, dy0, dx1 - dx0, dy1 - dy0);
            }
            /* Animating clients must not starve input: breathe once per frame. */
            sys_yield();
        } else if (mx != cur_sx || my != cur_sy) {
            int ox = cur_sx, oy = cur_sy;
            int x0, y0, x1, y1;
            cursor_paint(mx, my);
            /* Push only the old+new cursor footprints to the LFB. */
            x0 = ox < mx ? ox : mx;
            y0 = oy < my ? oy : my;
            x1 = (ox > mx ? ox : mx) + CUR_W;
            y1 = (oy > my ? oy : my) + CUR_H;
            if (ox < 0) {
                x0 = mx;
                y0 = my;
                x1 = mx + CUR_W;
                y1 = my + CUR_H;
            }
            gui_present_rect(&g, x0, y0, x1 - x0, y1 - y0);
        } else {
            /* Nothing to paint: HLT until the next timer IRQ, then pump
             * hosted apps again. Busy-spinning here freezes the desktop
             * when a client (e.g. clock) is alive but idle. */
            sys_yield();
        }
    }
    /* Tear down clients, leave graphics, then drop server role. */
    for (i = 0; i < MAX_WIN; i++) {
        if (launches[i].used) {
            term_session_stop(&launches[i].sess);
            launches[i].used = 0;
        }
        if (wins[i].open)
            win_close(i);
    }
    gui_shutdown(&g);
    sys_gui_server(0);
    sys_fb_mode(0, 0, 0);
    return 0;
}
