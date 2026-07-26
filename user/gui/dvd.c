#include "libgui.h"

#define WIN_W 320
#define WIN_H 200
#define SQ 36
#define SPEED 2

static uint32_t rng;

static uint32_t lcg_next(void)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7fffu;
}

static void seed_rng(void)
{
    SysTime tm;
    rng = 1u;
    if (sys_time(&tm) == 0) {
        rng = (uint32_t)tm.sec
            + (uint32_t)tm.min * 60u
            + (uint32_t)tm.hour * 3600u
            + (uint32_t)tm.day * 86400u
            + (uint32_t)tm.month * 2678400u
            + (uint32_t)tm.year * 32140800u;
        if (!rng)
            rng = 1u;
    }
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1;
    int hosted;
    int ox, oy, cw, ch;
    int x, y, dx, dy;
    int px, py;
    int max_x, max_y;
    int first = 1;

    if (gui_init_titled(&g, WIN_W, WIN_H, "DVD") != 0) {
        println("dvd: display fail");
        return 1;
    }
    hosted = gui_hosted(&g);
    if (hosted) {
        ox = 0;
        oy = 0;
        cw = g.w;
        ch = g.h;
    } else {
        ox = 1;
        oy = 23;
        cw = g.w - 2;
        ch = g.h - 24;
    }
    max_x = cw - SQ;
    max_y = ch - SQ;
    if (max_x < 0)
        max_x = 0;
    if (max_y < 0)
        max_y = 0;

    seed_rng();
    x = (int)(lcg_next() % (uint32_t)(max_x + 1));
    y = (int)(lcg_next() % (uint32_t)(max_y + 1));
    dx = (lcg_next() & 1u) ? SPEED : -SPEED;
    dy = (lcg_next() & 1u) ? SPEED : -SPEED;
    px = x;
    py = y;

    gui_fill(&g, GUI_WIN);
    if (!hosted) {
        gui_fill(&g, GUI_BG);
        gui_rect(&g, 0, 0, g.w, g.h, GUI_WIN);
        gui_border(&g, 0, 0, g.w, g.h, GUI_ACCENT);
        gui_rect(&g, 0, 0, g.w, 22, GUI_TITLE_FOC);
        gui_text(&g, 12, 7, "DVD", GUI_TEXT, 0xFFFFFFFFu);
    }

    while (run) {
        x += dx;
        y += dy;
        if (x <= 0) {
            x = 0;
            dx = SPEED;
        } else if (x >= max_x) {
            x = max_x;
            dx = -SPEED;
        }
        if (y <= 0) {
            y = 0;
            dy = SPEED;
        } else if (y >= max_y) {
            y = max_y;
            dy = -SPEED;
        }

        if (!first)
            gui_rect(&g, ox + px, oy + py, SQ, SQ, GUI_WIN);
        gui_rect(&g, ox + x, oy + y, SQ, SQ, GUI_ACCENT);
        /* present() HLTs one tick then yields to wm (kernel). */
        gui_present(&g);
        first = 0;
        px = x;
        py = y;

        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY && ev.key == KEY_ALT_F4)
                run = 0;
        }
        /* Another tick for wm input / other clients. */
        sys_yield();
    }
    gui_shutdown(&g);
    return 0;
}
