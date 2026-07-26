#include "libgui.h"
#include "user/guic/guic.h"

#define BUF_MAX 64

static void on_close_transfer(WEntry *a, WEntry *b)
{
    char tmp[BUF_MAX];
    const char *src;
    int i;

    /* For gterm-hosted test: dump A before replacing. */
    println(wentry_text(a));

    /* Clear A, copy B → A, clear B. */
    src = wentry_text(b);
    i = 0;
    while (src[i] && i + 1 < BUF_MAX) {
        tmp[i] = src[i];
        i++;
    }
    tmp[i] = 0;
    wentry_set_text(a, "");
    wentry_set_text(a, tmp);
    wentry_set_text(b, "");
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    WWindow win;
    WLabel lab_a;
    WLabel lab_b;
    WLabel hint;
    WEntry entry_a;
    WEntry entry_b;
    char buf_a[BUF_MAX];
    char buf_b[BUF_MAX];
    int run = 1;
    int dirty = 1;
    int mx = 0, my = 0, mbtn = 0;
    int hosted;
    Widget *prev_focus = 0;

    if (gui_init_titled(&g, 320, 180, "w_test") != 0) {
        println("w_test: display fail");
        return 1;
    }
    hosted = gui_hosted(&g);
    if (hosted)
        sys_gui_set_flags(g.win_id, GUI_FLAG_CLOSE_HOOK);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "w_test", 0, hosted ? 0 : 1);
    wwindow_set_active(&win, 1);

    wlabel_init(&lab_a, wpos_abs(16, 12, WANCHOR_TL), wsize_abs(0, 8),
                "A", W95_DIM, 0xFFFFFFFFu);
    wentry_init(&entry_a, wpos_abs(16, 28, WANCHOR_TL), wsize_abs(280, 24),
                buf_a, BUF_MAX, WENTRY_SINGLE);

    wlabel_init(&lab_b, wpos_abs(16, 64, WANCHOR_TL), wsize_abs(0, 8),
                "B", W95_DIM, 0xFFFFFFFFu);
    wentry_init(&entry_b, wpos_abs(16, 80, WANCHOR_TL), wsize_abs(280, 24),
                buf_b, BUF_MAX, WENTRY_SINGLE);

    wlabel_init(&hint, wpos_abs(16, 120, WANCHOR_TL), wsize_abs(0, 8),
                "focus one; close copies B->A", W95_DIM, 0xFFFFFFFFu);

    wentry_set_text(&entry_a, "first");
    wentry_set_text(&entry_b, "second");
    wentry_set_focused(&entry_a, 1);

    widget_add_child(wwindow_widget(&win), wlabel_widget(&lab_a));
    widget_add_child(wwindow_widget(&win), wentry_widget(&entry_a));
    widget_add_child(wwindow_widget(&win), wlabel_widget(&lab_b));
    widget_add_child(wwindow_widget(&win), wentry_widget(&entry_b));
    widget_add_child(wwindow_widget(&win), wlabel_widget(&hint));
    widget_layout_root(wwindow_widget(&win), g.w, g.h);

    prev_focus = widget_focus_get();

    while (run) {
        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_CLOSE ||
                (ev.type == INP_KEY && ev.key == KEY_ALT_F4)) {
                /* Close only when B is empty; otherwise copy B→A and clear B. */
                if (wentry_len(&entry_b) > 0) {
                    on_close_transfer(&entry_a, &entry_b);
                    dirty = 1;
                } else {
                    run = 0;
                }
            } else if (ev.type == INP_KEY) {
                /* Only the focused entry receives keys (incl. Enter ignored). */
                wentry_key(&entry_a, ev.key);
                wentry_key(&entry_b, ev.key);
                dirty = 1;
            }
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            }
        }

        wentry_input(&entry_a, mx, my, mbtn);
        wentry_input(&entry_b, mx, my, mbtn);

        if (widget_focus_get() != prev_focus) {
            prev_focus = widget_focus_get();
            /* Losing focus must drop the caret on the other field. */
            entry_a.focused = widget_has_focus(wentry_widget(&entry_a));
            entry_b.focused = widget_has_focus(wentry_widget(&entry_b));
            dirty = 1;
        }
        if (wentry_view_dirty(&entry_a) || wentry_view_dirty(&entry_b)) {
            wentry_ack_view(&entry_a);
            wentry_ack_view(&entry_b);
            dirty = 1;
        }

        if (dirty) {
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
