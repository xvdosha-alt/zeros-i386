#include "libgui.h"
#include "user/guic/guic.h"

#define MAX_FILES 64
#define NAME_MAX 56
#define ROW_H 16
#define TOOL_H 28
#define MARGIN 1

static GuiScreen *gp;
static char path[96];
static char listing[2048];

static WWindow win;
static WFrame root;
static WFrame list;
static WButton back;
static WLabel path_lbl;
static WScrollBar sb;
static WButton file_btns[MAX_FILES];
static char file_names[MAX_FILES][NAME_MAX];
static char file_labels[MAX_FILES][NAME_MAX];
static int nfiles;
static int dirty = 1;
static int mx, my, mbtn;

static void go_parent(char *p, int pathmax)
{
    int n;
    if (!p || pathmax < 2)
        return;
    n = strlen_u(p);
    if (n <= 1) {
        strncpy_u(p, "/", pathmax);
        return;
    }
    if (p[n - 1] == '/')
        n--;
    while (n > 1 && p[n - 1] != '/')
        n--;
    if (n <= 1)
        strncpy_u(p, "/", pathmax);
    else
        p[n] = 0;
}

static void join_path(const char *base, const char *name, char *out, int outmax)
{
    int n, k;
    if (!out || outmax < 2)
        return;
    if (!base || !base[0] || (base[0] == '/' && !base[1])) {
        out[0] = '/';
        strncpy_u(out + 1, name ? name : "", outmax - 1);
        return;
    }
    strncpy_u(out, base, outmax);
    n = strlen_u(out);
    if (n > 0 && out[n - 1] != '/' && n + 1 < outmax)
        out[n++] = '/';
    k = 0;
    while (name && name[k] && n + 1 < outmax)
        out[n++] = name[k++];
    out[n] = 0;
}

static int is_dir(const char *base, const char *name)
{
    char full[128];
    char probe[4];
    if (!name || !name[0])
        return 0;
    if (!strcmp_u(name, ".") || !strcmp_u(name, ".."))
        return 1;
    join_path(base, name, full, sizeof(full));
    return sys_listdir(full, probe, sizeof(probe)) >= 0;
}

static int ends_with(const char *s, const char *suf)
{
    int n, m;
    if (!s || !suf)
        return 0;
    n = strlen_u(s);
    m = strlen_u(suf);
    if (n < m)
        return 0;
    return strcmp_u(s + n - m, suf) == 0;
}

static void open_via_wm(const char *exec, const char *argv1)
{
    if (!exec || !exec[0])
        return;
    sys_gui_launch(exec, argv1 ? argv1 : "");
}

static void refresh_listing(void)
{
    if (sys_listdir(path, listing, sizeof(listing)) < 0)
        strncpy_u(listing, "(error)", sizeof(listing));
}

static void style_file_btn(WButton *b)
{
    wbutton_set_colors(b, W95_TEXT, W95_WINDOW, 0x00E8E8E8u);
    b->bg_down = W95_FACE;
    wbutton_set_style(b, WBTN_FLAT, WBTN_SUNKEN);
}

static void layout_chrome(void)
{
    int cx, cy, cw, ch;
    if (!gp)
        return;
    widget_layout_root(wwindow_widget(&win), gp->w, gp->h);
    widget_content_box(wframe_widget(&root), &cx, &cy, &cw, &ch);
    widget_set_pos(wframe_widget(&list),
                   wpos_abs(MARGIN, MARGIN + TOOL_H, WANCHOR_TL));
    widget_set_size(wframe_widget(&list),
                    wsize_abs(cw - MARGIN * 2,
                              ch - (MARGIN + TOOL_H) - MARGIN));
    widget_layout(wwindow_widget(&win));
}

static void on_file(WButton *b, void *userdata);

static int list_vis_rows(void)
{
    int cx, cy, cw, ch;
    widget_content_box(wframe_widget(&list), &cx, &cy, &cw, &ch);
    if (ch < ROW_H)
        return 1;
    return ch / ROW_H;
}

/* Place file rows + scrollbar for current scroll value. */
static void sync_file_btn_geom(void)
{
    int i, list_w, cx, cy, cw, ch;
    int vis, scroll, need_sb, sb_w;
    Widget *lw;

    layout_chrome();
    lw = wframe_widget(&list);
    widget_content_box(lw, &cx, &cy, &cw, &ch);
    vis = list_vis_rows();
    wscroll_set_range(&sb, nfiles, vis);
    scroll = wscroll_value(&sb);
    need_sb = (nfiles > vis);
    sb_w = need_sb ? WSCROLL_W : 0;
    list_w = cw - sb_w;
    if (list_w < 40)
        list_w = 40;

    for (i = 0; i < nfiles; i++) {
        if (i >= scroll && i < scroll + vis) {
            wbutton_widget(&file_btns[i])->visible = 1;
            widget_set_pos(wbutton_widget(&file_btns[i]),
                           wpos_abs(0, (i - scroll) * ROW_H, WANCHOR_TL));
            widget_set_size(wbutton_widget(&file_btns[i]),
                            wsize_abs(list_w, ROW_H));
        } else {
            wbutton_widget(&file_btns[i])->visible = 0;
        }
    }

    if (need_sb) {
        wscroll_widget(&sb)->visible = 1;
        widget_set_pos(wscroll_widget(&sb),
                       wpos_abs(cw - WSCROLL_W, 0, WANCHOR_TL));
        widget_set_size(wscroll_widget(&sb), wsize_abs(WSCROLL_W, ch));
    } else {
        wscroll_widget(&sb)->visible = 0;
    }
    widget_layout(wwindow_widget(&win));
}

static void on_scroll(WScrollBar *s, void *userdata)
{
    (void)s;
    (void)userdata;
    dirty = 1;
}

/* If wm resized our FB, pick up new pointer/size (critical — old FB is freed). */
static void ensure_fb_size(void)
{
    int w, h;
    if (!gp || !gp->hosted || gp->win_id < 0)
        return;
    w = sys_gui_info(gp->win_id, GUI_INFO_W);
    h = sys_gui_info(gp->win_id, GUI_INFO_H);
    if (w < 32 || h < 32)
        return;
    if (w == gp->w && h == gp->h && gp->fb == (uint32_t *)sys_gui_fb(gp->win_id))
        return;
    if (gui_sync_size(gp) != 0)
        return;
    sync_file_btn_geom();
    dirty = 1;
}

static void rebuild_file_buttons(void)
{
    int i = 0, n = 0;

    layout_chrome();
    widget_clear_children(wframe_widget(&list));
    nfiles = 0;
    wscroll_set_value(&sb, 0);

    while (listing[i] && n < MAX_FILES) {
        int j = 0;
        char name[NAME_MAX];
        while (listing[i] && listing[i] != '\n' && j + 1 < NAME_MAX)
            name[j++] = listing[i++];
        name[j] = 0;
        if (listing[i] == '\n')
            i++;
        if (!name[0] || !strcmp_u(name, "."))
            continue;

        strncpy_u(file_names[n], name, NAME_MAX);
        if (!strcmp_u(name, "..")) {
            strncpy_u(file_labels[n], "^", NAME_MAX);
        } else if (is_dir(path, name)) {
            int ln = strlen_u(name);
            strncpy_u(file_labels[n], name, NAME_MAX);
            if (ln + 1 < NAME_MAX) {
                file_labels[n][ln] = '/';
                file_labels[n][ln + 1] = 0;
            }
        } else {
            strncpy_u(file_labels[n], name, NAME_MAX);
        }

        wbutton_init(&file_btns[n],
                     wpos_abs(0, 0, WANCHOR_TL),
                     wsize_abs(40, ROW_H),
                     file_labels[n]);
        style_file_btn(&file_btns[n]);
        wbutton_set_handler(&file_btns[n], on_file, (void *)(unsigned long)n);
        widget_add_child(wframe_widget(&list), wbutton_widget(&file_btns[n]));
        n++;
    }
    nfiles = n;
    /* Scrollbar last so it draws above rows. */
    widget_add_child(wframe_widget(&list), wscroll_widget(&sb));
    sync_file_btn_geom();
    dirty = 1;
}

static void activate_name(const char *name)
{
    char full[128];
    if (!name || !name[0])
        return;
    if (!strcmp_u(name, "..")) {
        go_parent(path, sizeof(path));
        refresh_listing();
        wlabel_set_text(&path_lbl, path);
        rebuild_file_buttons();
        return;
    }
    join_path(path, name, full, sizeof(full));
    if (is_dir(path, name)) {
        strncpy_u(path, full, sizeof(path));
        refresh_listing();
        wlabel_set_text(&path_lbl, path);
        rebuild_file_buttons();
        return;
    }
    if (ends_with(name, ".win")) {
        open_via_wm(full, "");
        return;
    }
    open_via_wm("/sys/gui/notepad.win", full);
}

static void on_back(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    go_parent(path, sizeof(path));
    refresh_listing();
    wlabel_set_text(&path_lbl, path);
    rebuild_file_buttons();
}

static void on_file(WButton *b, void *userdata)
{
    unsigned idx = (unsigned)(unsigned long)userdata;
    (void)b;
    if (idx < (unsigned)nfiles)
        activate_name(file_names[idx]);
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1;
    int hosted;
    int i;
    int prev_back_hot = 0, prev_back_stage = 0;
    int prev_scroll;

    strncpy_u(path, "/sys", sizeof(path));
    if (gui_init_titled(&g, 560, 400, "Files") != 0) {
        println("files: display fail");
        return 1;
    }
    gp = &g;
    hosted = gui_hosted(&g);
    if (hosted)
        sys_gui_set_flags(g.win_id, GUI_FLAG_RESIZABLE);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Files", 1, hosted ? 0 : 1);

    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, W95_DARKSHADOW, WFRAME_FLAT);
    wframe_set_active(&root, 1);

    wbutton_init(&back, wpos_abs(MARGIN + 4, MARGIN + 4, WANCHOR_TL),
                 wsize_abs(18, TOOL_H - 8), "^");
    wbutton_set_handler(&back, on_back, 0);

    wlabel_init(&path_lbl, wpos_abs(MARGIN + 28, MARGIN + 10, WANCHOR_TL),
                wsize_abs(0, 8), path, W95_TEXT, 0xFFFFFFFFu);

    wframe_init(&list, wpos_abs(MARGIN, MARGIN + TOOL_H, WANCHOR_TL),
                wsize_abs(100, 100), W95_WINDOW, W95_DARKSHADOW, WFRAME_SUNKEN);
    wframe_set_active(&list, 1);

    wscroll_init(&sb, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(WSCROLL_W, 100));
    wscroll_set_handler(&sb, on_scroll, 0);
    wscroll_widget(&sb)->visible = 0;

    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wbutton_widget(&back));
    widget_add_child(wframe_widget(&root), wlabel_widget(&path_lbl));
    widget_add_child(wframe_widget(&root), wframe_widget(&list));

    refresh_listing();
    rebuild_file_buttons();

    while (run) {
        ensure_fb_size();

        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY) {
                if (ev.key == KEY_ALT_F4)
                    run = 0;
                else if (ev.key == 3) /* Ctrl+C copy current path */
                    sys_clip_set(path, strlen_u(path));
                else if (ev.key == 8 || ev.key == 127)
                    on_back(0, 0);
            }
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            }
            if (ev.type == INP_RESIZE) {
                if (gui_sync_size(&g) == 0) {
                    sync_file_btn_geom();
                    dirty = 1;
                }
            }
        }

        prev_back_hot = back.hot;
        prev_back_stage = back.stage;
        wbutton_input(&back, mx, my, mbtn);
        if (wbutton_was_clicked(&back)) {
            if (back.on_click)
                back.on_click(&back, back.userdata);
            wbutton_ack_click(&back);
        }
        if (back.hot != prev_back_hot || back.stage != prev_back_stage)
            dirty = 1;

        prev_scroll = wscroll_value(&sb);
        {
            int was_press = sb.dragging || sb.arrow || sb.track_dir;
            wscroll_input(&sb, mx, my, mbtn);
            if (wscroll_was_changed(&sb)) {
                wscroll_ack(&sb);
                sync_file_btn_geom();
                dirty = 1;
            } else if (wscroll_value(&sb) != prev_scroll) {
                sync_file_btn_geom();
                dirty = 1;
            }
            if (was_press != (sb.dragging || sb.arrow || sb.track_dir))
                dirty = 1;
            if (sb.dragging || sb.arrow || sb.track_dir)
                dirty = 1;
        }

        for (i = 0; i < nfiles; i++) {
            int ph, ps;
            if (!wbutton_widget(&file_btns[i])->visible)
                continue;
            ph = file_btns[i].hot;
            ps = file_btns[i].stage;
            wbutton_input(&file_btns[i], mx, my, mbtn);
            if (wbutton_was_clicked(&file_btns[i])) {
                on_file(&file_btns[i], file_btns[i].userdata);
                wbutton_ack_click(&file_btns[i]);
                break;
            }
            if (file_btns[i].hot != ph || file_btns[i].stage != ps)
                dirty = 1;
        }

        if (dirty) {
            sync_file_btn_geom();
            gui_fill(&g, W95_FACE);
            widget_draw(wwindow_widget(&win), &g);
            gui_present(&g);
            dirty = 0;
        }
        sys_yield();
    }
    gui_shutdown(&g);
    return 0;
}
