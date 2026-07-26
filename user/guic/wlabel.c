#include "wlabel.h"

static void wlabel_draw(Widget *self, GuiScreen *scr)
{
    WLabel *l = (WLabel *)self;
    if (!l->text)
        return;
    gui_text(scr, self->ax, self->ay, l->text, l->fg, l->bg);
}

static const WidgetOps wlabel_ops = {
    wlabel_draw
};

static int text_width(const char *text)
{
    int tw = 0;
    if (!text)
        return 8;
    while (*text) {
        if (*text != '\n')
            tw += 8;
        text++;
    }
    return tw > 0 ? tw : 8;
}

void wlabel_init(WLabel *l, WPos pos, WSize size, const char *text,
                 uint32_t fg, uint32_t bg)
{
    if (!l)
        return;
    if (size.mode == WSIZE_ABS && size.w <= 0)
        size = wsize_abs(text_width(text), 8);
    widget_init(&l->base, &wlabel_ops, WKIND_LABEL, pos, size);
    l->text = text ? text : "";
    l->fg = fg;
    l->bg = bg;
}

void wlabel_set_text(WLabel *l, const char *text)
{
    if (!l)
        return;
    l->text = text ? text : "";
    if (l->base.size.mode == WSIZE_ABS)
        l->base.size = wsize_abs(text_width(l->text), 8);
}
