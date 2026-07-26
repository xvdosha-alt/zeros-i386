#include "libgui.h"
#include "user/guic/guic.h"

#define LP_MAX 16
#define NAME_MAX 40
#define CMD_MAX 96
#define ITEM_H 22
#define STRIPE_W 22
#define MENU_W 160
#define PAD_Y 4
#define O_READ 1

static char names[LP_MAX][NAME_MAX];
static char paths[LP_MAX][96];
static char argvs[LP_MAX][64];
static int nentries;
static int g_quit;
static WButton items[LP_MAX];

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static void trim_inplace(char *s)
{
    int i, n, a, b;
    if (!s)
        return;
    n = strlen_u(s);
    a = 0;
    while (a < n && is_space(s[a]))
        a++;
    b = n;
    while (b > a && is_space(s[b - 1]))
        b--;
    if (a > 0) {
        for (i = 0; i < b - a; i++)
            s[i] = s[a + i];
    }
    s[b - a] = 0;
}

static int has_win_suffix(const char *s)
{
    int n;
    if (!s)
        return 0;
    n = strlen_u(s);
    return n >= 4 && s[n - 4] == '.' && s[n - 3] == 'w' &&
           s[n - 2] == 'i' && s[n - 1] == 'n';
}

/* cmdline: "prog args…" → absolute path + argv tail for sys_gui_launch. */
static int resolve_cmd(const char *cmdline, char *path, int pathmax,
                       char *argv, int argvmax)
{
    char work[CMD_MAX];
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
        if (sys_exists(try)) {
            strncpy_u(path, try, pathmax);
        } else if (!has_win_suffix(tok[0])) {
            n = strlen_u(try);
            if (n + 4 < (int)sizeof(try)) {
                try[n] = '.';
                try[n + 1] = 'w';
                try[n + 2] = 'i';
                try[n + 3] = 'n';
                try[n + 4] = 0;
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

static int load_menu(void)
{
    char buf[2048];
    int fd, n, i, line_start;

    nentries = 0;
    fd = sys_open("/sys/etc/gui_launch.txt", O_READ);
    if (fd < 0)
        return -1;
    n = sys_read(fd, buf, (int)sizeof(buf) - 1);
    sys_close(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;

    line_start = 0;
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
            if (i < n && buf[i] == '\r' && i + 1 < n && buf[i + 1] == '\n')
                line_start = i + 2;
            continue;
        }
        if (len >= (int)sizeof(line))
            len = (int)sizeof(line) - 1;
        memcpy_u(line, buf + line_start, len);
        line[len] = 0;
        line_start = i + 1;
        if (i < n && buf[i] == '\r' && i + 1 <= n && buf[i + 1] == '\n') {
            line_start = i + 2;
            i++;
        }
        trim_inplace(line);
        if (!line[0] || line[0] == '#')
            continue;

        tab = -1;
        for (k = 0; line[k]; k++) {
            if (line[k] == '\t') {
                tab = k;
                break;
            }
        }
        if (tab < 0)
            continue;
        line[tab] = 0;
        trim_inplace(line);
        trim_inplace(line + tab + 1);
        if (!line[0] || !line[tab + 1])
            continue;
        if (nentries >= LP_MAX)
            break;

        strncpy_u(names[nentries], line, NAME_MAX);
        if (resolve_cmd(line + tab + 1, paths[nentries],
                        (int)sizeof(paths[nentries]),
                        argvs[nentries], (int)sizeof(argvs[nentries])) != 0) {
            paths[nentries][0] = 0;
            argvs[nentries][0] = 0;
        }
        nentries++;
    }
    return nentries;
}

static void on_launch(WButton *b, void *userdata)
{
    unsigned idx = (unsigned)(unsigned long)userdata;
    (void)b;
    if (idx >= (unsigned)nentries || !paths[idx][0])
        return;
    sys_gui_launch(paths[idx], argvs[idx]);
    g_quit = 1;
}

static void style_item(WButton *b, int hilite)
{
    if (hilite) {
        b->fg = W95_HIGHLIGHT;
        b->bg = W95_TITLE;
        b->bg_hot = W95_TITLE;
        b->bg_down = W95_TITLE;
    } else {
        b->fg = W95_TEXT;
        b->bg = W95_FACE;
        b->bg_hot = W95_FACE;
        b->bg_down = W95_FACE;
    }
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    WFrame root;
    WFrame stripe;
    WLabel brand;
    int dirty = 1;
    int mx = 0, my = 0, mbtn = 0;
    int i, hosted;
    int win_w, win_h, menu_h;
    int brand_h;

    g_quit = 0;
    load_menu();
    if (nentries < 1) {
        strncpy_u(names[0], "(no menu)", NAME_MAX);
        paths[0][0] = 0;
        nentries = 1;
    }

    menu_h = PAD_Y * 2 + nentries * ITEM_H;
    if (menu_h < 80)
        menu_h = 80;
    win_w = STRIPE_W + MENU_W;
    win_h = menu_h;
    brand_h = 5 * 10;

    if (gui_init_titled(&g, win_w, win_h, "Start") != 0) {
        println("launchpad: display fail");
        return 1;
    }
    hosted = gui_hosted(&g);
    if (hosted && g.win_id >= 0)
        sys_gui_set_flags(g.win_id, GUI_FLAG_POPUP | GUI_FLAG_NO_MINMAX);

    /* Single raised frame — no client window chrome. */
    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, W95_DARKSHADOW, WFRAME_RAISED);
    wframe_set_active(&root, 1);

    wframe_init(&stripe, wpos_abs(0, 0, WANCHOR_TL),
                wsize_abs(STRIPE_W, menu_h), W95_TITLE, 0, WFRAME_FLAT);
    wframe_set_active(&stripe, 1);
    wlabel_init(&brand, wpos_abs(7, menu_h - brand_h - 6, WANCHOR_TL),
                wsize_abs(8, brand_h), "z\ne\nr\nO\nS",
                W95_HIGHLIGHT, 0xFFFFFFFFu);

    widget_add_child(wframe_widget(&root), wframe_widget(&stripe));
    widget_add_child(wframe_widget(&stripe), wlabel_widget(&brand));

    for (i = 0; i < nentries; i++) {
        int y = PAD_Y + i * ITEM_H;
        wbutton_init(&items[i], wpos_abs(STRIPE_W + 2, y, WANCHOR_TL),
                     wsize_abs(MENU_W - 6, ITEM_H - 2), names[i]);
        wbutton_set_style(&items[i], WBTN_FLAT, WBTN_FLAT);
        style_item(&items[i], 0);
        if (paths[i][0])
            wbutton_set_handler(&items[i], on_launch, (void *)(unsigned long)i);
        widget_add_child(wframe_widget(&root), wbutton_widget(&items[i]));
    }
    widget_layout_root(wframe_widget(&root), g.w, g.h);

    while (!g_quit) {
        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY && ev.key == KEY_ALT_F4)
                g_quit = 1;
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            }
        }

        for (i = 0; i < nentries; i++) {
            int st0 = items[i].stage;
            int hot0 = items[i].hot;
            wbutton_input(&items[i], mx, my, mbtn);
            style_item(&items[i], items[i].hot || items[i].stage == WBTN_STAGE_HELD);
            if (wbutton_was_clicked(&items[i])) {
                if (items[i].on_click)
                    items[i].on_click(&items[i], items[i].userdata);
                wbutton_ack_click(&items[i]);
                dirty = 1;
            }
            if (items[i].stage != st0 || items[i].hot != hot0)
                dirty = 1;
        }

        if (dirty) {
            gui_fill(&g, W95_FACE);
            widget_draw(wframe_widget(&root), &g);
            gui_present(&g);
            dirty = 0;
        } else {
            sys_yield();
        }
    }
    gui_shutdown(&g);
    return 0;
}
