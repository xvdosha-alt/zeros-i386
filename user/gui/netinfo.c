#include "libgui.h"
#include "user/guic/guic.h"

#define NET_MAX 512
#define MARGIN 4

static GuiScreen *gp;
static char netbuf[NET_MAX];

static WWindow win;
static WFrame root;
static WEntry entry;
static WScrollBar sb;
static int dirty = 1;
static int mx, my, mbtn;

static void sync_scroll(void)
{
    int vis = wentry_vis_rows(&entry);
    int lines = wentry_line_count(&entry);
    int need = lines > vis;
    int cx, cy, cw, ch;

    widget_content_box(wframe_widget(&root), &cx, &cy, &cw, &ch);
    if (need) {
        wscroll_widget(&sb)->visible = 1;
        widget_set_pos(wentry_widget(&entry),
                       wpos_abs(MARGIN, MARGIN, WANCHOR_TL));
        widget_set_size(wentry_widget(&entry),
                        wsize_abs(cw - MARGIN * 2 - WSCROLL_W, ch - MARGIN * 2));
        widget_set_pos(wscroll_widget(&sb),
                       wpos_abs(cw - MARGIN - WSCROLL_W, MARGIN, WANCHOR_TL));
        widget_set_size(wscroll_widget(&sb),
                        wsize_abs(WSCROLL_W, ch - MARGIN * 2));
    } else {
        wscroll_widget(&sb)->visible = 0;
        widget_set_pos(wentry_widget(&entry),
                       wpos_abs(MARGIN, MARGIN, WANCHOR_TL));
        widget_set_size(wentry_widget(&entry),
                        wsize_abs(cw - MARGIN * 2, ch - MARGIN * 2));
    }
    widget_layout(wwindow_widget(&win));
    wscroll_set_range(&sb, lines, wentry_vis_rows(&entry));
    wscroll_set_value(&sb, wentry_scroll(&entry));
}

static void layout_chrome(void)
{
    if (!gp)
        return;
    widget_layout_root(wwindow_widget(&win), gp->w, gp->h);
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

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1;
    int hosted;
    int prev_scroll;

    if (gui_init_titled(&g, 520, 340, "Network") != 0) {
        println("netinfo: display fail");
        return 1;
    }
    gp = &g;
    hosted = gui_hosted(&g);
    if (hosted)
        sys_gui_set_flags(g.win_id, GUI_FLAG_RESIZABLE);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Network", 1, hosted ? 0 : 1);
    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, 0, WFRAME_FLAT);
    wframe_set_active(&root, 1);

    wentry_init(&entry, wpos_abs(MARGIN, MARGIN, WANCHOR_TL),
                wsize_abs(100, 100), netbuf, NET_MAX, WENTRY_MULTI);
    wentry_set_readonly(&entry, 1);
    wentry_set_colors(&entry, W95_TEXT, W95_FACE, W95_TITLE);

    if (sys_ifconfig(netbuf, sizeof(netbuf)) < 0)
        strncpy_u(netbuf, "network unavailable", sizeof(netbuf));
    {
        /* set_text from same buffer: length sync without destructive clear */
        int n = strlen_u(netbuf);
        entry.len = n;
        entry.cursor = n;
        entry.scroll = 0;
    }

    wscroll_init(&sb, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(WSCROLL_W, 100));
    wscroll_set_handler(&sb, on_scroll, 0);
    wscroll_widget(&sb)->visible = 0;

    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wentry_widget(&entry));
    widget_add_child(wframe_widget(&root), wscroll_widget(&sb));
    layout_chrome();

    while (run) {
        ensure_fb_size();

        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY && ev.key == KEY_ALT_F4)
                run = 0;
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            }
            if (ev.type == INP_RESIZE) {
                if (gui_sync_size(&g) == 0) {
                    layout_chrome();
                    dirty = 1;
                }
            }
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

        if (dirty) {
            layout_chrome();
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
