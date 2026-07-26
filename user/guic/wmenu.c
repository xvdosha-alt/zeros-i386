#include "wmenu.h"

static void wmenu_draw(Widget *self, GuiScreen *scr)
{
    WMenu *m = (WMenu *)self;
    int i, y;
    int ax = self->ax, ay = self->ay, aw = self->aw;

    if (!m->open)
        return;
    gui_rect(scr, ax, ay, aw, m->nitems * WMENU_ITEM_H + 4, W95_FACE);
    gui_rect(scr, ax, ay, aw, 1, W95_HIGHLIGHT);
    gui_rect(scr, ax, ay, 1, m->nitems * WMENU_ITEM_H + 4, W95_HIGHLIGHT);
    gui_rect(scr, ax, ay + m->nitems * WMENU_ITEM_H + 3, aw, 1, W95_DARKSHADOW);
    gui_rect(scr, ax + aw - 1, ay, 1, m->nitems * WMENU_ITEM_H + 4, W95_DARKSHADOW);
    for (i = 0; i < m->nitems; i++) {
        y = ay + 2 + i * WMENU_ITEM_H;
        if (i == m->hot)
            gui_rect(scr, ax + 2, y, aw - 4, WMENU_ITEM_H, W95_TITLE);
        gui_text(scr, ax + 8, y + 5, m->items[i],
                 i == m->hot ? W95_HIGHLIGHT : W95_TEXT, 0xFFFFFFFFu);
    }
}

static const WidgetOps wmenu_ops = { wmenu_draw };

void wmenu_init(WMenu *m, WPos pos, WSize size)
{
    if (!m)
        return;
    widget_init(&m->base, &wmenu_ops, WKIND_BASE, pos, size);
    m->nitems = 0;
    m->open = 0;
    m->hot = -1;
    m->on_pick = 0;
    m->userdata = 0;
    m->base.visible = 0;
}

void wmenu_clear(WMenu *m)
{
    if (!m)
        return;
    m->nitems = 0;
    m->hot = -1;
}

int wmenu_add(WMenu *m, const char *text)
{
    if (!m || !text || m->nitems >= WMENU_MAX)
        return -1;
    m->items[m->nitems++] = text;
    return m->nitems - 1;
}

void wmenu_set_open(WMenu *m, int on)
{
    if (!m)
        return;
    m->open = on ? 1 : 0;
    m->base.visible = m->open;
    m->hot = -1;
}

int wmenu_is_open(const WMenu *m)
{
    return m && m->open;
}

void wmenu_set_handler(WMenu *m, WMenuPickFn fn, void *userdata)
{
    if (!m)
        return;
    m->on_pick = fn;
    m->userdata = userdata;
}

void wmenu_input(WMenu *m, int mx, int my, int buttons)
{
    int i;
    static int prev;
    if (!m || !m->open) {
        prev = buttons & 1;
        return;
    }
    m->hot = -1;
    for (i = 0; i < m->nitems; i++) {
        int y = m->base.ay + 2 + i * WMENU_ITEM_H;
        if (gui_hit(mx, my, m->base.ax + 2, y, m->base.aw - 4, WMENU_ITEM_H)) {
            m->hot = i;
            if ((buttons & 1) && !prev && m->on_pick) {
                m->on_pick(m, i, m->userdata);
                wmenu_set_open(m, 0);
            }
            break;
        }
    }
    if ((buttons & 1) && !prev && m->hot < 0)
        wmenu_set_open(m, 0);
    prev = buttons & 1;
}
