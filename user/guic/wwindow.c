#include "wwindow.h"

#define CAP_BTN 14
#define CAP_GAP 2

void wwindow_content_box(const WWindow *win, int *cx, int *cy, int *cw, int *ch)
{
    const Widget *w;
    int fx, ft, fb;
    if (!win || !cx || !cy || !cw || !ch)
        return;
    w = &win->base;
    if (!win->draw_chrome) {
        *cx = w->ax;
        *cy = w->ay;
        *cw = w->aw;
        *ch = w->ah;
        return;
    }
    fx = win->resizable ? WWIN_FRAME_RESIZE : WWIN_FRAME;
    ft = fx;
    fb = fx;
    *cx = w->ax + fx;
    *cy = w->ay + ft + WWIN_TITLE_H;
    *cw = w->aw - fx * 2;
    *ch = w->ah - (ft + WWIN_TITLE_H + fb);
    if (*cw < 1)
        *cw = 1;
    if (*ch < 1)
        *ch = 1;
}

static void draw_cap_btn(GuiScreen *scr, int bx, int by, const char *glyph,
                         int enabled, int pressed)
{
    uint32_t fill = pressed ? W95_DOWN : W95_FACE;
    uint32_t hi, lo, fg;
    int tx = bx + 3, ty = by + 3;

    gui_rect(scr, bx, by, CAP_BTN, CAP_BTN, fill);
    if (pressed) {
        hi = W95_DARKSHADOW;
        lo = W95_HIGHLIGHT;
        tx++;
        ty++;
    } else {
        hi = W95_HIGHLIGHT;
        lo = W95_DARKSHADOW;
    }
    gui_rect(scr, bx, by, CAP_BTN, 1, hi);
    gui_rect(scr, bx, by, 1, CAP_BTN, hi);
    gui_rect(scr, bx, by + CAP_BTN - 1, CAP_BTN, 1, lo);
    gui_rect(scr, bx + CAP_BTN - 1, by, 1, CAP_BTN, lo);
    fg = enabled ? W95_TEXT : W95_DIM;
    gui_text(scr, tx, ty, glyph, fg, 0xFFFFFFFFu);
}

static void wwindow_draw(Widget *self, GuiScreen *scr)
{
    WWindow *win = (WWindow *)self;
    int cx, cy, cw, ch;
    uint32_t tc, tt;
    int tb_y, bx, by, fx, ft;
    int nbtn, i;

    if (!win->draw_chrome) {
        gui_rect(scr, self->ax, self->ay, self->aw, self->ah, win->face);
        return;
    }

    fx = win->resizable ? WWIN_FRAME_RESIZE : WWIN_FRAME;
    ft = fx;
    gui_rect(scr, self->ax, self->ay, self->aw, self->ah, win->face);
    gui_rect(scr, self->ax, self->ay, self->aw, 1, W95_HIGHLIGHT);
    gui_rect(scr, self->ax, self->ay, 1, self->ah, W95_HIGHLIGHT);
    gui_rect(scr, self->ax, self->ay + self->ah - 1, self->aw, 1, W95_DARKSHADOW);
    gui_rect(scr, self->ax + self->aw - 1, self->ay, 1, self->ah, W95_DARKSHADOW);
    if (win->resizable) {
        gui_rect(scr, self->ax + 1, self->ay + 1, self->aw - 2, 1, W95_HIGHLIGHT);
        gui_rect(scr, self->ax + 1, self->ay + 1, 1, self->ah - 2, W95_HIGHLIGHT);
        gui_rect(scr, self->ax + 1, self->ay + self->ah - 2, self->aw - 2, 1, W95_SHADOW);
        gui_rect(scr, self->ax + self->aw - 2, self->ay + 1, 1, self->ah - 2, W95_SHADOW);
    }

    tb_y = self->ay + ft;
    tc = win->active ? win->title_foc : win->title_lost;
    tt = win->active ? W95_HIGHLIGHT : W95_TEXT;
    gui_rect(scr, self->ax + fx, tb_y, self->aw - fx * 2, WWIN_TITLE_H, tc);
    if (win->title)
        gui_text(scr, self->ax + fx + 4, tb_y + (WWIN_TITLE_H - 8) / 2,
                 win->title, tt, 0xFFFFFFFFu);

    nbtn = win->show_minmax ? 3 : 1;
    by = tb_y + (WWIN_TITLE_H - CAP_BTN) / 2;
    /* Right to left: close, max, min */
    for (i = 0; i < nbtn; i++) {
        const char *glyph;
        int enabled = 1;
        bx = self->ax + self->aw - fx - 2 - CAP_BTN - i * (CAP_BTN + CAP_GAP);
        if (i == 0)
            glyph = "x";
        else if (i == 1) {
            glyph = "o";
            enabled = win->resizable;
        } else
            glyph = "_";
        draw_cap_btn(scr, bx, by, glyph, enabled, 0);
    }

    wwindow_content_box(win, &cx, &cy, &cw, &ch);
    gui_rect(scr, cx, cy, cw, ch, W95_WINDOW);
}

static const WidgetOps wwindow_ops = {
    wwindow_draw
};

void wwindow_init(WWindow *win, WPos pos, WSize size, const char *title,
                  int resizable, int draw_chrome)
{
    if (!win)
        return;
    widget_init(&win->base, &wwindow_ops, WKIND_WINDOW, pos, size);
    win->title = title ? title : "";
    win->resizable = resizable ? 1 : 0;
    win->draw_chrome = draw_chrome ? 1 : 0;
    win->show_minmax = 1;
    win->active = 1;
    win->face = W95_FACE;
    win->title_foc = W95_TITLE;
    win->title_lost = W95_TITLE_LOST;
}

void wwindow_set_title(WWindow *win, const char *title)
{
    if (win)
        win->title = title ? title : "";
}

void wwindow_set_active(WWindow *win, int on)
{
    if (win)
        win->active = on ? 1 : 0;
}

void wwindow_set_resizable(WWindow *win, int on)
{
    if (win)
        win->resizable = on ? 1 : 0;
}

void wwindow_set_minmax(WWindow *win, int on)
{
    if (win)
        win->show_minmax = on ? 1 : 0;
}
