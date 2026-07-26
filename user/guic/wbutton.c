#include "wbutton.h"
#include "wwindow.h"

static int host_active(const Widget *w)
{
    const Widget *p;
    for (p = w; p; p = p->parent) {
        if (p->kind == WKIND_FRAME)
            return ((const WFrame *)p)->active;
        if (p->kind == WKIND_WINDOW)
            return ((const WWindow *)p)->active;
    }
    return 1;
}

static void draw_relief(GuiScreen *scr, int x, int y, int w, int h, int style)
{
    uint32_t hi, lo;
    if (style == WBTN_FLAT || w < 2 || h < 2)
        return;
    if (style == WBTN_SUNKEN) {
        hi = W95_DARKSHADOW;
        lo = W95_HIGHLIGHT;
    } else {
        hi = W95_HIGHLIGHT;
        lo = W95_DARKSHADOW;
    }
    gui_rect(scr, x, y, w, 1, hi);
    gui_rect(scr, x, y, 1, h, hi);
    gui_rect(scr, x, y + h - 1, w, 1, lo);
    gui_rect(scr, x + w - 1, y, 1, h, lo);
    if (style == WBTN_SUNKEN && w > 3 && h > 3) {
        gui_rect(scr, x + 1, y + 1, w - 2, 1, W95_SHADOW);
        gui_rect(scr, x + 1, y + 1, 1, h - 2, W95_SHADOW);
    }
}

static void wbutton_draw(Widget *self, GuiScreen *scr)
{
    WButton *b = (WButton *)self;
    uint32_t fill;
    int tx, ty, tw = 0, sunk, relief;
    const char *p;

    sunk = b->sticky || (b->stage == WBTN_STAGE_HELD);
    if (sunk)
        fill = b->bg_down;
    else if (b->hot)
        fill = b->bg_hot;
    else
        fill = b->bg;
    relief = sunk ? b->style_down : b->style;

    gui_rect(scr, self->ax, self->ay, self->aw, self->ah, fill);
    draw_relief(scr, self->ax, self->ay, self->aw, self->ah, relief);

    if (b->text) {
        for (p = b->text; *p; p++)
            tw += 8;
    }
    /* Keep left-aligned text if the idle style is flat (list rows etc.). */
    if (b->style == WBTN_FLAT)
        tx = self->ax + 4;
    else
        tx = self->ax + (self->aw - tw) / 2;
    ty = self->ay + (self->ah - 8) / 2;
    if (sunk) {
        tx++;
        ty++;
    }
    if (tx < self->ax + 2)
        tx = self->ax + 2;
    if (ty < self->ay)
        ty = self->ay;
    if (b->text)
        gui_text(scr, tx, ty, b->text, b->fg, 0xFFFFFFFFu);
}

static const WidgetOps wbutton_ops = {
    wbutton_draw
};

void wbutton_init(WButton *b, WPos pos, WSize size, const char *text)
{
    if (!b)
        return;
    widget_init(&b->base, &wbutton_ops, WKIND_BUTTON, pos, size);
    b->text = text ? text : "";
    b->fg = W95_TEXT;
    b->bg = W95_FACE;
    b->bg_hot = W95_HOT;
    b->bg_down = W95_DOWN;
    b->border = W95_DARKSHADOW;
    b->border_hot = W95_SHADOW;
    b->border_down = W95_DARKSHADOW;
    b->style = WBTN_RAISED;
    b->style_down = WBTN_SUNKEN;
    b->sticky = 0;
    b->hot = 0;
    b->stage = WBTN_STAGE_IDLE;
    b->on_click = 0;
    b->userdata = 0;
}

void wbutton_set_colors(WButton *b, uint32_t fg, uint32_t bg, uint32_t bg_hot)
{
    if (!b)
        return;
    b->fg = fg;
    b->bg = bg;
    b->bg_hot = bg_hot;
}

void wbutton_set_borders(WButton *b, uint32_t border, uint32_t border_hot,
                         uint32_t border_down)
{
    if (!b)
        return;
    b->border = border;
    b->border_hot = border_hot;
    b->border_down = border_down;
}

void wbutton_set_style(WButton *b, int style, int style_down)
{
    if (!b)
        return;
    b->style = style;
    b->style_down = style_down;
}

void wbutton_set_sticky(WButton *b, int on)
{
    if (b)
        b->sticky = on ? 1 : 0;
}

void wbutton_set_bordered(WButton *b, int on)
{
    if (!b)
        return;
    if (on)
        wbutton_set_style(b, WBTN_RAISED, WBTN_SUNKEN);
    else
        wbutton_set_style(b, WBTN_FLAT, WBTN_FLAT);
}

void wbutton_set_handler(WButton *b, WButtonClickFn fn, void *userdata)
{
    if (!b)
        return;
    b->on_click = fn;
    b->userdata = userdata;
}

void wbutton_set_text(WButton *b, const char *text)
{
    if (!b)
        return;
    b->text = text ? text : "";
}

void wbutton_input(WButton *b, int mx, int my, int buttons)
{
    int hit, down;
    if (!b || !b->base.visible)
        return;

    hit = widget_hit(&b->base, mx, my);
    b->hot = hit;
    down = (buttons & 1) != 0;

    if (!host_active(&b->base)) {
        if (b->stage != WBTN_STAGE_CLICKED)
            b->stage = WBTN_STAGE_IDLE;
        return;
    }

    switch (b->stage) {
    case WBTN_STAGE_IDLE:
        if (hit && down) {
            widget_focus_set(&b->base);
            b->stage = WBTN_STAGE_HELD;
        }
        break;
    case WBTN_STAGE_HELD:
        if (!down) {
            if (hit)
                b->stage = WBTN_STAGE_CLICKED;
            else
                b->stage = WBTN_STAGE_IDLE;
        }
        break;
    case WBTN_STAGE_CLICKED:
        break;
    default:
        b->stage = WBTN_STAGE_IDLE;
        break;
    }
}

int wbutton_is_held(const WButton *b)
{
    return b && b->stage == WBTN_STAGE_HELD;
}

int wbutton_was_clicked(const WButton *b)
{
    return b && b->stage == WBTN_STAGE_CLICKED;
}

void wbutton_ack_click(WButton *b)
{
    if (b && b->stage == WBTN_STAGE_CLICKED)
        b->stage = WBTN_STAGE_IDLE;
}
