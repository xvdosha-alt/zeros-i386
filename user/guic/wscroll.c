#include "wscroll.h"

static int clampi(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

int wscroll_max(const WScrollBar *s)
{
    int m;
    if (!s)
        return 0;
    m = s->range - s->page;
    return m > 0 ? m : 0;
}

static void notify(WScrollBar *s)
{
    s->changed = 1;
    if (s->on_change)
        s->on_change(s, s->userdata);
}

static void set_value(WScrollBar *s, int v)
{
    int m = wscroll_max(s);
    v = clampi(v, 0, m);
    if (v == s->value)
        return;
    s->value = v;
    notify(s);
}

/* Track = area between arrow buttons. */
static void track_box(const WScrollBar *s, int *tx, int *ty, int *tw, int *th)
{
    const Widget *w = &s->base;
    *tx = w->ax;
    *ty = w->ay + WSCROLL_BTN;
    *tw = w->aw;
    *th = w->ah - WSCROLL_BTN * 2;
    if (*th < 1)
        *th = 1;
}

static void thumb_metrics(const WScrollBar *s, int *ty, int *th)
{
    int tx, tty, tw, tth, maxv, travel;
    track_box(s, &tx, &tty, &tw, &tth);
    (void)tx;
    (void)tw;
    if (s->range <= s->page || s->range <= 0) {
        *ty = tty;
        *th = tth;
        return;
    }
    *th = (int)((long)s->page * tth / s->range);
    if (*th < WSCROLL_THUMB_MIN)
        *th = WSCROLL_THUMB_MIN;
    if (*th > tth)
        *th = tth;
    maxv = wscroll_max(s);
    travel = tth - *th;
    if (travel < 0)
        travel = 0;
    *ty = tty + (maxv > 0 ? (int)((long)s->value * travel / maxv) : 0);
}

static void draw_raised(GuiScreen *scr, int x, int y, int w, int h, int pressed)
{
    uint32_t fill = pressed ? W95_DOWN : W95_FACE;
    uint32_t hi, lo;
    gui_rect(scr, x, y, w, h, fill);
    if (pressed) {
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
}

static void wscroll_draw(Widget *self, GuiScreen *scr)
{
    WScrollBar *s = (WScrollBar *)self;
    int tx, ty, tw, th, thy, thh;
    int ax = self->ax, ay = self->ay, aw = self->aw, ah = self->ah;
    int up_p, dn_p;
    int ox, oy;

    if (s->range <= s->page)
        return;

    track_box(s, &tx, &ty, &tw, &th);
    gui_rect(scr, tx, ty, tw, th, W95_WINDOW);
    /* Subtle sunken track edge */
    gui_rect(scr, tx, ty, tw, 1, W95_SHADOW);
    gui_rect(scr, tx, ty, 1, th, W95_SHADOW);

    thumb_metrics(s, &thy, &thh);
    draw_raised(scr, tx, thy, tw, thh, s->dragging);

    up_p = (s->arrow < 0);
    dn_p = (s->arrow > 0);
    draw_raised(scr, ax, ay, aw, WSCROLL_BTN, up_p);
    draw_raised(scr, ax, ay + ah - WSCROLL_BTN, aw, WSCROLL_BTN, dn_p);

    ox = ax + (aw - 8) / 2;
    oy = ay + (WSCROLL_BTN - 8) / 2;
    if (up_p) {
        ox++;
        oy++;
    }
    gui_text(scr, ox, oy, "^", W95_TEXT, 0xFFFFFFFFu);

    ox = ax + (aw - 8) / 2;
    oy = ay + ah - WSCROLL_BTN + (WSCROLL_BTN - 8) / 2;
    if (dn_p) {
        ox++;
        oy++;
    }
    gui_text(scr, ox, oy, "v", W95_TEXT, 0xFFFFFFFFu);
}

static const WidgetOps wscroll_ops = {
    wscroll_draw
};

void wscroll_init(WScrollBar *s, WPos pos, WSize size)
{
    if (!s)
        return;
    widget_init(&s->base, &wscroll_ops, WKIND_BASE, pos, size);
    s->value = 0;
    s->page = 1;
    s->range = 1;
    s->dragging = 0;
    s->drag_off = 0;
    s->arrow = 0;
    s->hold_ticks = 0;
    s->track_dir = 0;
    s->changed = 0;
    s->on_change = 0;
    s->userdata = 0;
}

void wscroll_set_range(WScrollBar *s, int range, int page)
{
    if (!s)
        return;
    if (range < 0)
        range = 0;
    if (page < 1)
        page = 1;
    s->range = range;
    s->page = page;
    s->base.visible = (range > page) ? 1 : 0;
    if (s->value > wscroll_max(s))
        s->value = wscroll_max(s);
}

void wscroll_set_value(WScrollBar *s, int value)
{
    if (!s)
        return;
    s->value = clampi(value, 0, wscroll_max(s));
}

int wscroll_value(const WScrollBar *s)
{
    return s ? s->value : 0;
}

void wscroll_set_handler(WScrollBar *s, WScrollChangeFn fn, void *userdata)
{
    if (!s)
        return;
    s->on_change = fn;
    s->userdata = userdata;
}

void wscroll_input(WScrollBar *s, int mx, int my, int buttons)
{
    int down, hit;
    int tx, ty, tw, th, thy, thh;
    int maxv, travel;
    int ax, ay, aw, ah;
    int was_armed;

    if (!s)
        return;

    was_armed = s->dragging || s->arrow || s->track_dir;
    down = (buttons & 1) != 0;

    /* Always accept release even if hidden mid-drag. */
    if (!down) {
        if (was_armed) {
            s->dragging = 0;
            s->arrow = 0;
            s->track_dir = 0;
            s->hold_ticks = 0;
            s->changed = 1; /* signal visual refresh */
        }
        return;
    }

    if (!s->base.visible && !was_armed)
        return;

    ax = s->base.ax;
    ay = s->base.ay;
    aw = s->base.aw;
    ah = s->base.ah;
    hit = widget_hit(&s->base, mx, my);
    track_box(s, &tx, &ty, &tw, &th);
    thumb_metrics(s, &thy, &thh);

    /* Thumb drag */
    if (s->dragging) {
        maxv = wscroll_max(s);
        travel = th - thh;
        if (travel < 1)
            travel = 1;
        set_value(s, (int)((long)(my - s->drag_off - ty) * maxv / travel));
        return;
    }

    /* Arrow hold with initial delay + repeat */
    if (s->arrow) {
        s->hold_ticks++;
        if (s->hold_ticks > 10 && (s->hold_ticks % 3) == 0)
            set_value(s, s->value + s->arrow);
        return;
    }

    /* Track page hold */
    if (s->track_dir) {
        s->hold_ticks++;
        if (s->hold_ticks > 10 && (s->hold_ticks % 4) == 0)
            set_value(s, s->value + s->track_dir * s->page);
        return;
    }

    if (!hit)
        return;

    /* Up arrow */
    if (gui_hit(mx, my, ax, ay, aw, WSCROLL_BTN)) {
        s->arrow = -1;
        s->hold_ticks = 1;
        set_value(s, s->value - 1);
        return;
    }
    /* Down arrow */
    if (gui_hit(mx, my, ax, ay + ah - WSCROLL_BTN, aw, WSCROLL_BTN)) {
        s->arrow = 1;
        s->hold_ticks = 1;
        set_value(s, s->value + 1);
        return;
    }
    /* Thumb */
    if (gui_hit(mx, my, tx, thy, tw, thh)) {
        s->dragging = 1;
        s->drag_off = my - thy;
        s->changed = 1;
        return;
    }
    /* Track above / below thumb */
    if (gui_hit(mx, my, tx, ty, tw, th)) {
        s->track_dir = (my < thy) ? -1 : 1;
        s->hold_ticks = 1;
        set_value(s, s->value + s->track_dir * s->page);
    }
}

int wscroll_was_changed(const WScrollBar *s)
{
    return s && s->changed;
}

void wscroll_ack(WScrollBar *s)
{
    if (s)
        s->changed = 0;
}
