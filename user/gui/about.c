#include "libgui.h"
#include "user/guic/guic.h"

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    WWindow win;
    WLabel title;
    WLabel line1;
    WLabel line2;
    WButton ok;
    int run = 1;
    int dirty = 1;
    int mx = 0, my = 0, mbtn = 0;
    int prev_hot = 0;
    int prev_stage = 0;
    int hosted;

    if (gui_init_titled(&g, 360, 160, "About") != 0) {
        println("about: display fail");
        return 1;
    }
    hosted = gui_hosted(&g);
    if (hosted && g.win_id >= 0)
        sys_gui_set_flags(g.win_id, GUI_FLAG_NO_MINMAX);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "About", 0, hosted ? 0 : 1);
    wwindow_set_minmax(&win, 0);
    wlabel_init(&title, wpos_abs(16, 20, WANCHOR_TL), wsize_abs(0, 8),
                "zerOS", W95_TEXT, 0xFFFFFFFFu);
    wlabel_init(&line1, wpos_abs(16, 44, WANCHOR_TL), wsize_abs(0, 8),
                "i386 multiboot desktop", W95_TEXT, 0xFFFFFFFFu);
    wlabel_init(&line2, wpos_abs(16, 64, WANCHOR_TL), wsize_abs(0, 8),
                "windowed via display server", W95_DIM, 0xFFFFFFFFu);
    wbutton_init(&ok, wpos_abs(16, 16, WANCHOR_BR), wsize_abs(72, 28), "OK");

    widget_add_child(wwindow_widget(&win), wlabel_widget(&title));
    widget_add_child(wwindow_widget(&win), wlabel_widget(&line1));
    widget_add_child(wwindow_widget(&win), wlabel_widget(&line2));
    widget_add_child(wwindow_widget(&win), wbutton_widget(&ok));
    widget_layout_root(wwindow_widget(&win), g.w, g.h);

    while (run) {
        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY && (ev.key == KEY_ALT_F4 || ev.key == '\r' || ev.key == '\n'))
                run = 0;
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            }
        }

        prev_hot = ok.hot;
        prev_stage = ok.stage;
        wbutton_input(&ok, mx, my, mbtn);
        if (wbutton_was_clicked(&ok)) {
            wbutton_ack_click(&ok);
            run = 0;
        }
        if (ok.stage != prev_stage || ok.hot != prev_hot)
            dirty = 1;

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
