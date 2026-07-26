#include "libgui.h"
#include "user/guic/guic.h"

static void put2(char *dst, int v)
{
    if (v < 0)
        v = 0;
    if (v > 99)
        v = 99;
    dst[0] = (char)('0' + (v / 10));
    dst[1] = (char)('0' + (v % 10));
}

static void fmt_date(char *dst, const SysTime *t)
{
    int y = t->year;
    dst[0] = (char)('0' + (y / 1000) % 10);
    dst[1] = (char)('0' + (y / 100) % 10);
    dst[2] = (char)('0' + (y / 10) % 10);
    dst[3] = (char)('0' + (y % 10));
    dst[4] = '-';
    put2(dst + 5, t->month);
    dst[7] = '-';
    put2(dst + 8, t->day);
    dst[10] = 0;
}

static void fmt_time(char *dst, const SysTime *t)
{
    put2(dst, t->hour);
    dst[2] = ':';
    put2(dst + 3, t->min);
    dst[5] = ':';
    put2(dst + 6, t->sec);
    dst[8] = 0;
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    SysTime tm;
    WWindow win;
    WLabel date_l;
    WLabel time_l;
    char date[16];
    char time[16];
    int run = 1;
    int dirty = 1;
    int last_sec = -1;
    int hosted;

    if (gui_init_titled(&g, 220, 88, "Clock") != 0) {
        println("clock: display fail");
        return 1;
    }
    hosted = gui_hosted(&g);
    if (hosted)
        sys_gui_set_flags(g.win_id, GUI_FLAG_RESIZABLE);

    /* Hosted: content-only (wm draws chrome). Standalone: own chrome. */
    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Clock", 1, hosted ? 0 : 1);
    wlabel_init(&date_l, wpos_rel(0.5f, 0.28f, WANCHOR_MC), wsize_abs(80, 8),
                date, W95_DIM, 0xFFFFFFFFu);
    wlabel_init(&time_l, wpos_rel(0.5f, 0.62f, WANCHOR_MC), wsize_abs(64, 8),
                time, W95_TITLE, 0xFFFFFFFFu);
    widget_add_child(wwindow_widget(&win), wlabel_widget(&date_l));
    widget_add_child(wwindow_widget(&win), wlabel_widget(&time_l));
    widget_layout_root(wwindow_widget(&win), g.w, g.h);

    date[0] = 0;
    time[0] = 0;

    while (run) {
        if (sys_time(&tm) == 0) {
            if (tm.sec != last_sec) {
                last_sec = tm.sec;
                fmt_date(date, &tm);
                fmt_time(time, &tm);
                wlabel_set_text(&date_l, date);
                wlabel_set_text(&time_l, time);
                dirty = 1;
            }
        } else if (!date[0]) {
            strncpy_u(date, "no time", sizeof(date));
            strncpy_u(time, "--:--:--", sizeof(time));
            wlabel_set_text(&date_l, date);
            wlabel_set_text(&time_l, time);
            dirty = 1;
        }

        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY && ev.key == KEY_ALT_F4)
                run = 0;
            if (ev.type == INP_RESIZE) {
                if (gui_sync_size(&g) == 0) {
                    widget_layout_root(wwindow_widget(&win), g.w, g.h);
                    dirty = 1;
                }
            }
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
