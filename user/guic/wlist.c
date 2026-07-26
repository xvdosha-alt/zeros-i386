#include "wlist.h"

static void wlist_draw(Widget *self, GuiScreen *scr)
{
    WListBox *l = (WListBox *)self;
    int i, y, vis, row;
    int ax = self->ax, ay = self->ay, aw = self->aw, ah = self->ah;

    gui_rect(scr, ax, ay, aw, ah, W95_WINDOW);
    gui_rect(scr, ax, ay, aw, 1, W95_DARKSHADOW);
    gui_rect(scr, ax, ay, 1, ah, W95_DARKSHADOW);
    gui_rect(scr, ax, ay + ah - 1, aw, 1, W95_HIGHLIGHT);
    gui_rect(scr, ax + aw - 1, ay, 1, ah, W95_HIGHLIGHT);

    vis = ah / WLIST_ROW_H;
    if (vis < 1)
        vis = 1;
    for (i = 0; i < vis; i++) {
        row = l->scroll + i;
        if (row >= l->nitems)
            break;
        y = ay + 1 + i * WLIST_ROW_H;
        if (row == l->selected)
            gui_rect(scr, ax + 1, y, aw - 2, WLIST_ROW_H, W95_TITLE);
        gui_text(scr, ax + 4, y + 3, l->items[row],
                 row == l->selected ? W95_HIGHLIGHT : W95_TEXT, 0xFFFFFFFFu);
    }
}

static const WidgetOps wlist_ops = { wlist_draw };

void wlist_init(WListBox *l, WPos pos, WSize size)
{
    if (!l)
        return;
    widget_init(&l->base, &wlist_ops, WKIND_BASE, pos, size);
    l->nitems = 0;
    l->selected = -1;
    l->scroll = 0;
    l->hot = 0;
    l->on_activate = 0;
    l->userdata = 0;
}

void wlist_clear(WListBox *l)
{
    if (!l)
        return;
    l->nitems = 0;
    l->selected = -1;
    l->scroll = 0;
}

int wlist_add(WListBox *l, const char *text)
{
    if (!l || !text || l->nitems >= WLIST_MAX)
        return -1;
    l->items[l->nitems++] = text;
    return l->nitems - 1;
}

void wlist_set_selected(WListBox *l, int idx)
{
    int vis;
    if (!l)
        return;
    if (idx < 0 || idx >= l->nitems) {
        l->selected = -1;
        return;
    }
    l->selected = idx;
    vis = l->base.ah / WLIST_ROW_H;
    if (vis < 1)
        vis = 1;
    if (idx < l->scroll)
        l->scroll = idx;
    if (idx >= l->scroll + vis)
        l->scroll = idx - vis + 1;
}

int wlist_selected(const WListBox *l)
{
    return l ? l->selected : -1;
}

void wlist_set_handler(WListBox *l, WListActivateFn fn, void *userdata)
{
    if (!l)
        return;
    l->on_activate = fn;
    l->userdata = userdata;
}

void wlist_input(WListBox *l, int mx, int my, int buttons)
{
    int row, vis;
    static int prev;
    if (!l || !widget_hit(&l->base, mx, my)) {
        prev = buttons & 1;
        return;
    }
    vis = l->base.ah / WLIST_ROW_H;
    if (vis < 1)
        vis = 1;
    row = l->scroll + (my - l->base.ay - 1) / WLIST_ROW_H;
    if (row >= 0 && row < l->nitems)
        l->selected = row;
    if ((buttons & 1) && !prev && row >= 0 && row < l->nitems && l->on_activate)
        l->on_activate(l, row, l->userdata);
    prev = buttons & 1;
}

void wlist_key(WListBox *l, int key)
{
    if (!l || l->nitems <= 0)
        return;
    if (key == 0x100) { /* up */
        if (l->selected > 0)
            wlist_set_selected(l, l->selected - 1);
        else if (l->selected < 0)
            wlist_set_selected(l, 0);
    } else if (key == 0x101) { /* down */
        if (l->selected + 1 < l->nitems)
            wlist_set_selected(l, l->selected + 1);
        else if (l->selected < 0)
            wlist_set_selected(l, 0);
    } else if ((key == '\r' || key == '\n') && l->selected >= 0 && l->on_activate)
        l->on_activate(l, l->selected, l->userdata);
}
