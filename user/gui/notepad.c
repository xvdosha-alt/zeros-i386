#include "libgui.h"
#include "user/guic/guic.h"

#define O_READ 1
#define O_WRITE 2
#define O_TRUNC 8
#define O_CREATE 16

#define TEXT_MAX 4096
#define TOOL_H 28
#define MARGIN 4

static GuiScreen *gp;
static char text[TEXT_MAX];
static char path[128];
static int file_dirty;

static WWindow win;
static WFrame root;
static WButton save_btn;
static WButton open_btn;
static WFileDialog open_dlg;
static WFileDialog save_dlg;
static WLabel path_lbl;
static WLabel star_lbl;
static WFrame editor;
static WEntry entry;
static WScrollBar sb;
static int dirty = 1;
static int mx, my, mbtn;

static void load(void)
{
    int fd = sys_open(path, O_READ);
    int n = 0;
    file_dirty = 0;
    if (fd < 0) {
        wentry_set_text(&entry, "");
        return;
    }
    n = sys_read(fd, text, sizeof(text) - 1);
    if (n < 0)
        n = 0;
    text[n] = 0;
    sys_close(fd);
    wentry_set_text(&entry, text);
    file_dirty = 0;
}

static int do_save(void)
{
    const char *body = wentry_text(&entry);
    int n = wentry_len(&entry);
    int fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
    if (fd < 0)
        return -1;
    sys_write(fd, body, n);
    sys_close(fd);
    file_dirty = 0;
    wentry_ack_changed(&entry);
    dirty = 1;
    return 0;
}

static void update_star(void)
{
    wlabel_set_text(&star_lbl, file_dirty ? "*" : "");
}

static void on_save(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    wfiledlg_show(&save_dlg, path);
    dirty = 1;
}

static void on_open_ok(WFileDialog *d, const char *chosen, void *userdata)
{
    (void)d;
    (void)userdata;
    if (!chosen || !chosen[0])
        return;
    strncpy_u(path, chosen, sizeof(path));
    wlabel_set_text(&path_lbl, path);
    load();
    update_star();
    dirty = 1;
}

static void on_saveas_ok(WFileDialog *d, const char *chosen, void *userdata)
{
    (void)d;
    (void)userdata;
    if (!chosen || !chosen[0])
        return;
    strncpy_u(path, chosen, sizeof(path));
    wlabel_set_text(&path_lbl, path);
    do_save();
    update_star();
    dirty = 1;
}

static void on_open(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    wfiledlg_show(&open_dlg, path);
    dirty = 1;
}

static void sync_scroll(void)
{
    int vis = wentry_vis_rows(&entry);
    int lines = wentry_line_count(&entry);
    int need = lines > vis;
    int cx, cy, cw, ch;

    widget_content_box(wframe_widget(&editor), &cx, &cy, &cw, &ch);
    if (need) {
        wscroll_widget(&sb)->visible = 1;
        widget_set_pos(wentry_widget(&entry), wpos_abs(0, 0, WANCHOR_TL));
        widget_set_size(wentry_widget(&entry),
                        wsize_abs(cw - WSCROLL_W, ch));
        widget_set_pos(wscroll_widget(&sb),
                       wpos_abs(cw - WSCROLL_W, 0, WANCHOR_TL));
        widget_set_size(wscroll_widget(&sb), wsize_abs(WSCROLL_W, ch));
    } else {
        wscroll_widget(&sb)->visible = 0;
        widget_set_pos(wentry_widget(&entry), wpos_abs(0, 0, WANCHOR_TL));
        widget_set_size(wentry_widget(&entry), wsize_abs(cw, ch));
    }
    widget_layout(wwindow_widget(&win));
    wscroll_set_range(&sb, lines, wentry_vis_rows(&entry));
    wscroll_set_value(&sb, wentry_scroll(&entry));
}

static void layout_chrome(void)
{
    int cx, cy, cw, ch;
    if (!gp)
        return;
    widget_layout_root(wwindow_widget(&win), gp->w, gp->h);
    widget_content_box(wframe_widget(&root), &cx, &cy, &cw, &ch);
    widget_set_pos(wframe_widget(&editor),
                   wpos_abs(MARGIN, TOOL_H, WANCHOR_TL));
    widget_set_size(wframe_widget(&editor),
                    wsize_abs(cw - MARGIN * 2, ch - TOOL_H - MARGIN));
    /* Path label width: leave room for Save + star */
    widget_set_pos(wlabel_widget(&path_lbl),
                   wpos_abs(112, 10, WANCHOR_TL));
    widget_set_pos(wlabel_widget(&star_lbl),
                   wpos_abs(cw - 16, 10, WANCHOR_TL));
    widget_layout(wwindow_widget(&win));
    sync_scroll();
}

static void on_scroll(WScrollBar *s, void *userdata)
{
    (void)userdata;
    wentry_set_scroll(&entry, wscroll_value(s));
    dirty = 1;
}

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
    layout_chrome();
    dirty = 1;
}

static void read_path_arg(void)
{
    char line[160];
    char *argv[4];
    int argc, n;
    strncpy_u(path, "/sys/tmp/note.txt", sizeof(path));
    n = read_argv(line, sizeof(line));
    if (n <= 0)
        return;
    argc = split_args(line, argv, 4);
    if (argc >= 1 && argv[0][0])
        strncpy_u(path, argv[0], sizeof(path));
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1;
    int hosted;
    int prev_save_hot = 0, prev_save_stage = 0;
    int prev_scroll;

    read_path_arg();
    if (gui_init_titled(&g, 640, 420, "Notepad") != 0) {
        println("notepad: display fail");
        return 1;
    }
    gp = &g;
    hosted = gui_hosted(&g);
    if (hosted)
        sys_gui_set_flags(g.win_id, GUI_FLAG_RESIZABLE);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Notepad", 1, hosted ? 0 : 1);

    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, W95_DARKSHADOW, WFRAME_FLAT);
    wframe_set_active(&root, 1);

    wbutton_init(&open_btn, wpos_abs(MARGIN, 4, WANCHOR_TL),
                 wsize_abs(48, TOOL_H - 8), "Open");
    wbutton_set_handler(&open_btn, on_open, 0);
    wbutton_init(&save_btn, wpos_abs(MARGIN + 52, 4, WANCHOR_TL),
                 wsize_abs(48, TOOL_H - 8), "Save");
    wbutton_set_handler(&save_btn, on_save, 0);
    wfiledlg_init(&open_dlg, 0);
    wfiledlg_set_handler(&open_dlg, on_open_ok, 0);
    wfiledlg_init(&save_dlg, 1);
    wfiledlg_set_handler(&save_dlg, on_saveas_ok, 0);

    wlabel_init(&path_lbl, wpos_abs(112, 10, WANCHOR_TL), wsize_abs(0, 8),
                path, W95_TEXT, 0xFFFFFFFFu);
    wlabel_init(&star_lbl, wpos_abs(100, 10, WANCHOR_TL), wsize_abs(8, 8),
                "", W95_TITLE, 0xFFFFFFFFu);

    wframe_init(&editor, wpos_abs(MARGIN, TOOL_H, WANCHOR_TL),
                wsize_abs(100, 100), W95_FACE, 0, WFRAME_FLAT);

    wentry_init(&entry, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(100, 100),
                text, TEXT_MAX, WENTRY_MULTI);
    wentry_set_focused(&entry, 1);
    wscroll_init(&sb, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(WSCROLL_W, 100));
    wscroll_set_handler(&sb, on_scroll, 0);
    wscroll_widget(&sb)->visible = 0;

    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wbutton_widget(&open_btn));
    widget_add_child(wframe_widget(&root), wbutton_widget(&save_btn));
    widget_add_child(wframe_widget(&root), wlabel_widget(&path_lbl));
    widget_add_child(wframe_widget(&root), wlabel_widget(&star_lbl));
    widget_add_child(wframe_widget(&root), wframe_widget(&editor));
    widget_add_child(wframe_widget(&editor), wentry_widget(&entry));
    widget_add_child(wframe_widget(&editor), wscroll_widget(&sb));

    load();
    update_star();
    layout_chrome();

    while (run) {
        ensure_fb_size();

        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY) {
                int k = ev.key;
                if (wfiledlg_visible(&open_dlg)) {
                    wfiledlg_key(&open_dlg, k);
                    dirty = 1;
                    continue;
                }
                if (wfiledlg_visible(&save_dlg)) {
                    wfiledlg_key(&save_dlg, k);
                    dirty = 1;
                    continue;
                }
                if (k == KEY_ALT_F4) {
                    run = 0;
                } else if (k == 19) { /* Ctrl+S */
                    on_save(0, 0);
                } else if (k == 15) { /* Ctrl+O */
                    on_open(0, 0);
                } else if (k == 3) { /* Ctrl+C copy all */
                    const char *body = wentry_text(&entry);
                    sys_clip_set(body, wentry_len(&entry));
                } else if (k == 22) { /* Ctrl+V paste */
                    char clip[TEXT_MAX];
                    int n = sys_clip_get(clip, TEXT_MAX - 1);
                    if (n > 0) {
                        clip[n] = 0;
                        wentry_set_text(&entry, clip);
                        file_dirty = 1;
                        update_star();
                    }
                } else {
                    wentry_key(&entry, k);
                    if (wentry_was_changed(&entry)) {
                        file_dirty = 1;
                        update_star();
                        wentry_ack_changed(&entry);
                    }
                }
                dirty = 1;
            }
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
                if ((mbtn & (MOUSE_WHEEL_UP | MOUSE_WHEEL_DOWN)) &&
                    (widget_hit(wentry_widget(&entry), mx, my) ||
                     widget_hit(wscroll_widget(&sb), mx, my) ||
                     widget_hit(wframe_widget(&editor), mx, my))) {
                    int delta = 0;
                    if (mbtn & MOUSE_WHEEL_UP)
                        delta -= 3;
                    if (mbtn & MOUSE_WHEEL_DOWN)
                        delta += 3;
                    if (delta) {
                        wscroll_set_range(&sb, wentry_line_count(&entry),
                                          wentry_vis_rows(&entry));
                        wscroll_set_value(&sb, wscroll_value(&sb) + delta);
                        wentry_set_scroll(&entry, wscroll_value(&sb));
                        dirty = 1;
                    }
                }
            }
            if (ev.type == INP_RESIZE) {
                if (gui_sync_size(&g) == 0) {
                    layout_chrome();
                    dirty = 1;
                }
            }
        }

        if (wfiledlg_visible(&open_dlg) || wfiledlg_visible(&save_dlg)) {
            if (wfiledlg_visible(&open_dlg)) {
                wfiledlg_layout(&open_dlg, 0, 0, g.w, g.h);
                wfiledlg_input(&open_dlg, mx, my, mbtn);
            }
            if (wfiledlg_visible(&save_dlg)) {
                wfiledlg_layout(&save_dlg, 0, 0, g.w, g.h);
                wfiledlg_input(&save_dlg, mx, my, mbtn);
            }
            dirty = 1;
        } else {
        prev_save_hot = save_btn.hot;
        prev_save_stage = save_btn.stage;
        wbutton_input(&open_btn, mx, my, mbtn);
        if (wbutton_was_clicked(&open_btn)) {
            on_open(&open_btn, 0);
            wbutton_ack_click(&open_btn);
        }
        wbutton_input(&save_btn, mx, my, mbtn);
        if (wbutton_was_clicked(&save_btn)) {
            on_save(&save_btn, 0);
            wbutton_ack_click(&save_btn);
        }
        if (save_btn.hot != prev_save_hot || save_btn.stage != prev_save_stage)
            dirty = 1;
        if (open_btn.hot || open_btn.stage)
            dirty = 1;
        }

        prev_scroll = wscroll_value(&sb);
        {
            int was_press = sb.dragging || sb.arrow || sb.track_dir;
            wscroll_input(&sb, mx, my, mbtn);
            if (wscroll_was_changed(&sb)) {
                wscroll_ack(&sb);
                wentry_set_scroll(&entry, wscroll_value(&sb));
                dirty = 1;
            } else if (wscroll_value(&sb) != prev_scroll) {
                wentry_set_scroll(&entry, wscroll_value(&sb));
                dirty = 1;
            }
            if (was_press != (sb.dragging || sb.arrow || sb.track_dir))
                dirty = 1;
            if (sb.dragging || sb.arrow || sb.track_dir)
                dirty = 1;
        }

        wentry_input(&entry, mx, my, mbtn);
        if (wentry_view_dirty(&entry)) {
            wentry_ack_view(&entry);
            /* Keep scrollbar thumb in sync after typing / caret moves. */
            wscroll_set_range(&sb, wentry_line_count(&entry),
                              wentry_vis_rows(&entry));
            wscroll_set_value(&sb, wentry_scroll(&entry));
            dirty = 1;
        }
        if (wentry_was_changed(&entry)) {
            file_dirty = 1;
            update_star();
            wentry_ack_changed(&entry);
            dirty = 1;
        }

        if (dirty) {
            layout_chrome();
            gui_fill(&g, W95_FACE);
            widget_draw(wwindow_widget(&win), &g);
            if (wfiledlg_visible(&open_dlg)) {
                wfiledlg_layout(&open_dlg, 0, 0, g.w, g.h);
                wfiledlg_draw(&open_dlg, &g);
            }
            if (wfiledlg_visible(&save_dlg)) {
                wfiledlg_layout(&save_dlg, 0, 0, g.w, g.h);
                wfiledlg_draw(&save_dlg, &g);
            }
            gui_present(&g);
            dirty = 0;
        }
        sys_yield();
    }
    gui_shutdown(&g);
    return 0;
}
