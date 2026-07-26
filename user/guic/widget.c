#include "widget.h"
#include "wwindow.h"

WPos wpos_abs(int x, int y, unsigned char anchor)
{
    WPos p;
    p.mode = WPOS_ABS;
    p.anchor = anchor;
    p._pad = 0;
    p.x = x;
    p.y = y;
    p.relx = 0.0f;
    p.rely = 0.0f;
    return p;
}

WPos wpos_rel(float relx, float rely, unsigned char anchor)
{
    WPos p;
    p.mode = WPOS_REL;
    p.anchor = anchor;
    p._pad = 0;
    p.x = 0;
    p.y = 0;
    p.relx = relx;
    p.rely = rely;
    return p;
}

WSize wsize_abs(int w, int h)
{
    WSize s;
    s.mode = WSIZE_ABS;
    s._pad[0] = s._pad[1] = s._pad[2] = 0;
    s.w = w;
    s.h = h;
    s.relw = 0.0f;
    s.relh = 0.0f;
    return s;
}

WSize wsize_rel(float relw, float relh)
{
    WSize s;
    s.mode = WSIZE_REL;
    s._pad[0] = s._pad[1] = s._pad[2] = 0;
    s.w = 0;
    s.h = 0;
    s.relw = relw;
    s.relh = relh;
    return s;
}

void wsize_resolve(const WSize *sz, int pw, int ph, int *ow, int *oh)
{
    int w, h;
    if (!sz || !ow || !oh)
        return;
    if (sz->mode == WSIZE_REL) {
        w = (int)(sz->relw * (float)pw);
        h = (int)(sz->relh * (float)ph);
    } else {
        w = sz->w;
        h = sz->h;
    }
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    *ow = w;
    *oh = h;
}

void wpos_resolve_tl(const WPos *pos, int aw, int ah, int pw, int ph,
                     int *ox, int *oy)
{
    int px, py;
    if (!pos || !ox || !oy)
        return;

    if (pos->mode == WPOS_REL) {
        px = (int)(pos->relx * (float)pw);
        py = (int)(pos->rely * (float)ph);
    } else {
        /* ABS: (x,y) are insets from the edges implied by anchor. */
        switch (pos->anchor) {
        case WANCHOR_TC:
        case WANCHOR_MC:
        case WANCHOR_BC:
            px = pw / 2 + pos->x;
            break;
        case WANCHOR_TR:
        case WANCHOR_MR:
        case WANCHOR_BR:
            px = pw - pos->x;
            break;
        default:
            px = pos->x;
            break;
        }
        switch (pos->anchor) {
        case WANCHOR_ML:
        case WANCHOR_MC:
        case WANCHOR_MR:
            py = ph / 2 + pos->y;
            break;
        case WANCHOR_BL:
        case WANCHOR_BC:
        case WANCHOR_BR:
            py = ph - pos->y;
            break;
        default:
            py = pos->y;
            break;
        }
    }

    /* Convert anchored point → top-left. */
    switch (pos->anchor) {
    case WANCHOR_TC:
        *ox = px - aw / 2;
        *oy = py;
        break;
    case WANCHOR_TR:
        *ox = px - aw;
        *oy = py;
        break;
    case WANCHOR_ML:
        *ox = px;
        *oy = py - ah / 2;
        break;
    case WANCHOR_MC:
        *ox = px - aw / 2;
        *oy = py - ah / 2;
        break;
    case WANCHOR_MR:
        *ox = px - aw;
        *oy = py - ah / 2;
        break;
    case WANCHOR_BL:
        *ox = px;
        *oy = py - ah;
        break;
    case WANCHOR_BC:
        *ox = px - aw / 2;
        *oy = py - ah;
        break;
    case WANCHOR_BR:
        *ox = px - aw;
        *oy = py - ah;
        break;
    case WANCHOR_TL:
    default:
        *ox = px;
        *oy = py;
        break;
    }
}

void widget_init(Widget *w, const WidgetOps *ops, int kind, WPos pos, WSize size)
{
    if (!w)
        return;
    w->ops = ops;
    w->kind = kind;
    w->parent = 0;
    w->child = 0;
    w->next = 0;
    w->pos = pos;
    w->size = size;
    w->ax = 0;
    w->ay = 0;
    w->aw = 1;
    w->ah = 1;
    w->visible = 1;
}

void widget_add_child(Widget *parent, Widget *child)
{
    Widget **pp;
    if (!parent || !child)
        return;
    child->parent = parent;
    child->next = 0;
    pp = &parent->child;
    while (*pp)
        pp = &(*pp)->next;
    *pp = child;
}

void widget_abs(const Widget *w, int *ax, int *ay)
{
    if (!w || !ax || !ay)
        return;
    *ax = w->ax;
    *ay = w->ay;
}

int widget_hit(const Widget *w, int mx, int my)
{
    if (!w || !w->visible)
        return 0;
    return gui_hit(mx, my, w->ax, w->ay, w->aw, w->ah);
}

int widget_pressed(const Widget *w, int mx, int my, int buttons)
{
    return widget_hit(w, mx, my) && (buttons & 1);
}

void widget_draw(Widget *w, GuiScreen *scr)
{
    Widget *c;
    if (!w || !w->visible)
        return;
    if (w->ops && w->ops->draw)
        w->ops->draw(w, scr);
    for (c = w->child; c; c = c->next)
        widget_draw(c, scr);
}

void widget_set_pos(Widget *w, WPos pos)
{
    if (w)
        w->pos = pos;
}

void widget_set_size(Widget *w, WSize size)
{
    if (w)
        w->size = size;
}

void widget_content_box(const Widget *w, int *cx, int *cy, int *cw, int *ch)
{
    int inset;
    if (!w || !cx || !cy || !cw || !ch)
        return;
    if (w->kind == WKIND_WINDOW) {
        wwindow_content_box((const WWindow *)w, cx, cy, cw, ch);
        return;
    }
    *cx = w->ax;
    *cy = w->ay;
    *cw = w->aw;
    *ch = w->ah;
    /* Keep children inside bevel (sunken = 2px, raised = 1px). */
    if (w->kind == WKIND_FRAME) {
        const WFrame *f = (const WFrame *)w;
        if (f->style == WFRAME_FLAT)
            return;
        inset = (f->style == WFRAME_SUNKEN) ? 2 : 1;
        *cx += inset;
        *cy += inset;
        *cw -= inset * 2;
        *ch -= inset * 2;
        if (*cw < 1)
            *cw = 1;
        if (*ch < 1)
            *ch = 1;
    }
}

void widget_layout(Widget *w)
{
    Widget *c;
    int cx, cy, cw, ch, lx, ly;
    if (!w)
        return;
    for (c = w->child; c; c = c->next) {
        widget_content_box(w, &cx, &cy, &cw, &ch);
        wsize_resolve(&c->size, cw, ch, &c->aw, &c->ah);
        wpos_resolve_tl(&c->pos, c->aw, c->ah, cw, ch, &lx, &ly);
        c->ax = cx + lx;
        c->ay = cy + ly;
        widget_layout(c);
    }
}

void widget_layout_root(Widget *root, int scr_w, int scr_h)
{
    if (!root)
        return;
    root->ax = 0;
    root->ay = 0;
    wsize_resolve(&root->size, scr_w, scr_h, &root->aw, &root->ah);
    widget_layout(root);
}

void widget_apply_box(Widget *w, int ax, int ay, int aw, int ah)
{
    if (!w)
        return;
    w->ax = ax;
    w->ay = ay;
    w->aw = aw > 0 ? aw : 1;
    w->ah = ah > 0 ? ah : 1;
    widget_layout(w);
}

void widget_clear_children(Widget *parent)
{
    Widget *c, *n;
    if (!parent)
        return;
    for (c = parent->child; c; c = n) {
        n = c->next;
        c->parent = 0;
        c->next = 0;
    }
    parent->child = 0;
}

static Widget *g_focus;

void widget_focus_set(Widget *w)
{
    g_focus = w;
}

Widget *widget_focus_get(void)
{
    return g_focus;
}

int widget_has_focus(const Widget *w)
{
    return w && g_focus == w;
}

/* --- WFrame -------------------------------------------------------------- */

static void wframe_draw(Widget *self, GuiScreen *scr)
{
    WFrame *f = (WFrame *)self;
    uint32_t hi, lo;
    gui_rect(scr, self->ax, self->ay, self->aw, self->ah, f->bg);
    if (f->style == WFRAME_FLAT)
        return;
    if (f->style == WFRAME_SUNKEN) {
        hi = W95_DARKSHADOW;
        lo = W95_HIGHLIGHT;
    } else {
        hi = W95_HIGHLIGHT;
        lo = W95_DARKSHADOW;
    }
    gui_rect(scr, self->ax, self->ay, self->aw, 1, hi);
    gui_rect(scr, self->ax, self->ay, 1, self->ah, hi);
    gui_rect(scr, self->ax, self->ay + self->ah - 1, self->aw, 1, lo);
    gui_rect(scr, self->ax + self->aw - 1, self->ay, 1, self->ah, lo);
    /* Inner bevel */
    if (f->style == WFRAME_SUNKEN) {
        gui_rect(scr, self->ax + 1, self->ay + 1, self->aw - 2, 1, W95_SHADOW);
        gui_rect(scr, self->ax + 1, self->ay + 1, 1, self->ah - 2, W95_SHADOW);
        gui_rect(scr, self->ax + 1, self->ay + self->ah - 2, self->aw - 2, 1, W95_FACE);
        gui_rect(scr, self->ax + self->aw - 2, self->ay + 1, 1, self->ah - 2, W95_FACE);
    }
    (void)f->border;
}

static const WidgetOps wframe_ops = {
    wframe_draw
};

void wframe_init(WFrame *f, WPos pos, WSize size,
                 uint32_t bg, uint32_t border, int style)
{
    if (!f)
        return;
    widget_init(&f->base, &wframe_ops, WKIND_FRAME, pos, size);
    f->bg = bg;
    f->border = border;
    f->style = style;
    f->active = 1;
}

void wframe_set_style(WFrame *f, int style)
{
    if (f)
        f->style = style;
}

void wframe_set_active(WFrame *f, int on)
{
    if (f)
        f->active = on ? 1 : 0;
}

int wframe_is_active(const WFrame *f)
{
    return f && f->active;
}
