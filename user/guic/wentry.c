#include "wentry.h"

static int clampi(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void mark_view(WEntry *e)
{
    e->view_dirty = 1;
}

static void mark_changed(WEntry *e)
{
    e->changed = 1;
    mark_view(e);
}

static int line_start(const WEntry *e, int line)
{
    int i = 0, cur = 0;
    if (!e || line <= 0)
        return 0;
    while (i < e->len) {
        if (e->buf[i++] == '\n') {
            cur++;
            if (cur == line)
                return i;
        }
    }
    return e->len;
}

static void cursor_rowcol(const WEntry *e, int *row, int *col)
{
    int i, r = 0, c = 0;
    if (!e) {
        if (row)
            *row = 0;
        if (col)
            *col = 0;
        return;
    }
    for (i = 0; i < e->cursor && i < e->len; i++) {
        if (e->buf[i] == '\n') {
            r++;
            c = 0;
        } else {
            c++;
        }
    }
    if (row)
        *row = r;
    if (col)
        *col = c;
}

int wentry_line_count(const WEntry *e)
{
    int i, n = 1;
    if (!e || e->len <= 0)
        return 1;
    for (i = 0; i < e->len; i++)
        if (e->buf[i] == '\n')
            n++;
    return n;
}

int wentry_vis_rows(const WEntry *e)
{
    int h, rows;
    if (!e)
        return 1;
    h = e->base.ah - WENTRY_PAD * 2;
    if (h < WENTRY_LINE_H)
        return 1;
    rows = h / WENTRY_LINE_H;
    return rows < 1 ? 1 : rows;
}

int wentry_vis_cols(const WEntry *e)
{
    int w, cols;
    if (!e)
        return 8;
    w = e->base.aw - WENTRY_PAD * 2;
    cols = w / WENTRY_CHAR_W;
    return cols < 1 ? 1 : cols;
}

void wentry_ensure_cursor_visible(WEntry *e)
{
    int row, col, vis, maxs;
    if (!e)
        return;
    cursor_rowcol(e, &row, &col);
    if (e->multiline) {
        vis = wentry_vis_rows(e);
        if (row < e->scroll)
            e->scroll = row;
        if (row >= e->scroll + vis)
            e->scroll = row - vis + 1;
        maxs = wentry_line_count(e) - vis;
        if (maxs < 0)
            maxs = 0;
        e->scroll = clampi(e->scroll, 0, maxs);
    } else {
        vis = wentry_vis_cols(e);
        if (col < e->scroll)
            e->scroll = col;
        if (col >= e->scroll + vis)
            e->scroll = col - vis + 1;
        if (e->scroll < 0)
            e->scroll = 0;
    }
}

static void insert_char(WEntry *e, char ch)
{
    int i;
    if (!e || !e->buf || e->len + 1 >= e->cap)
        return;
    if (!e->multiline && ch == '\n')
        return;
    for (i = e->len; i > e->cursor; i--)
        e->buf[i] = e->buf[i - 1];
    e->buf[e->cursor] = ch;
    e->cursor++;
    e->len++;
    e->buf[e->len] = 0;
    mark_changed(e);
    wentry_ensure_cursor_visible(e);
}

static void delete_back(WEntry *e)
{
    int i;
    if (!e || e->cursor <= 0)
        return;
    e->cursor--;
    for (i = e->cursor; i < e->len; i++)
        e->buf[i] = e->buf[i + 1];
    e->len--;
    e->buf[e->len] = 0;
    mark_changed(e);
    wentry_ensure_cursor_visible(e);
}

static void move_left(WEntry *e)
{
    if (e->cursor > 0)
        e->cursor--;
    mark_view(e);
    wentry_ensure_cursor_visible(e);
}

static void move_right(WEntry *e)
{
    if (e->cursor < e->len)
        e->cursor++;
    mark_view(e);
    wentry_ensure_cursor_visible(e);
}

static void move_up(WEntry *e)
{
    int row, col, start, i, c;
    if (!e->multiline)
        return;
    cursor_rowcol(e, &row, &col);
    if (row <= 0)
        return;
    start = line_start(e, row - 1);
    c = 0;
    i = start;
    while (i < e->len && e->buf[i] != '\n' && c < col) {
        i++;
        c++;
    }
    e->cursor = i;
    mark_view(e);
    wentry_ensure_cursor_visible(e);
}

static void move_down(WEntry *e)
{
    int row, col, start, i, c, rows;
    if (!e->multiline)
        return;
    cursor_rowcol(e, &row, &col);
    rows = wentry_line_count(e);
    if (row + 1 >= rows)
        return;
    start = line_start(e, row + 1);
    c = 0;
    i = start;
    while (i < e->len && e->buf[i] != '\n' && c < col) {
        i++;
        c++;
    }
    e->cursor = i;
    mark_view(e);
    wentry_ensure_cursor_visible(e);
}

static void content_box(const WEntry *e, int *cx, int *cy, int *cw, int *ch)
{
    *cx = e->base.ax + WENTRY_PAD;
    *cy = e->base.ay + WENTRY_PAD;
    *cw = e->base.aw - WENTRY_PAD * 2;
    *ch = e->base.ah - WENTRY_PAD * 2;
    if (*cw < 1)
        *cw = 1;
    if (*ch < 1)
        *ch = 1;
}

static void wentry_draw(Widget *self, GuiScreen *scr)
{
    WEntry *e = (WEntry *)self;
    int cx, cy, cw, ch, cols, vis, y, i, row, col;

    gui_rect(scr, self->ax, self->ay, self->aw, self->ah, e->bg);
    if (!e->flat) {
        gui_rect(scr, self->ax, self->ay, self->aw, 1, W95_DARKSHADOW);
        gui_rect(scr, self->ax, self->ay, 1, self->ah, W95_DARKSHADOW);
        gui_rect(scr, self->ax, self->ay + self->ah - 1, self->aw, 1, W95_HIGHLIGHT);
        gui_rect(scr, self->ax + self->aw - 1, self->ay, 1, self->ah, W95_HIGHLIGHT);
        gui_rect(scr, self->ax + 1, self->ay + 1, self->aw - 2, 1, W95_SHADOW);
        gui_rect(scr, self->ax + 1, self->ay + 1, 1, self->ah - 2, W95_SHADOW);
    }

    content_box(e, &cx, &cy, &cw, &ch);
    cols = wentry_vis_cols(e);

    if (e->multiline) {
        vis = wentry_vis_rows(e);
        y = cy;
        i = line_start(e, e->scroll);
        while (y + WENTRY_LINE_H <= cy + ch && i <= e->len) {
            char rowbuf[128];
            int j = 0;
            while (i < e->len && e->buf[i] != '\n' && j < cols &&
                   j + 1 < (int)sizeof(rowbuf))
                rowbuf[j++] = e->buf[i++];
            rowbuf[j] = 0;
            gui_text(scr, cx, y, rowbuf, e->fg, 0xFFFFFFFFu);
            if (i < e->len && e->buf[i] == '\n')
                i++;
            else if (i >= e->len)
                break;
            y += WENTRY_LINE_H;
        }
        if (widget_has_focus(&e->base) && !e->readonly) {
            cursor_rowcol(e, &row, &col);
            if (row >= e->scroll && row < e->scroll + vis) {
                int curx = cx + col * WENTRY_CHAR_W;
                int cury = cy + (row - e->scroll) * WENTRY_LINE_H;
                if (curx >= cx && curx + WENTRY_CHAR_W <= cx + cw)
                    gui_rect(scr, curx, cury + WENTRY_LINE_H - 2,
                             WENTRY_CHAR_W - 1, 2, e->cursor_c);
            }
        }
    } else {
        char rowbuf[128];
        int j = 0, start = e->scroll;
        while (start + j < e->len && j < cols && j + 1 < (int)sizeof(rowbuf)) {
            if (e->buf[start + j] == '\n')
                break;
            rowbuf[j] = e->buf[start + j];
            j++;
        }
        rowbuf[j] = 0;
        gui_text(scr, cx, cy + (ch - 8) / 2, rowbuf, e->fg, 0xFFFFFFFFu);
        if (widget_has_focus(&e->base) && !e->readonly) {
            cursor_rowcol(e, &row, &col);
            (void)row;
            {
                int curx = cx + (col - e->scroll) * WENTRY_CHAR_W;
                int cury = cy + (ch - WENTRY_LINE_H) / 2;
                if (cury < cy)
                    cury = cy;
                if (curx >= cx && curx + WENTRY_CHAR_W <= cx + cw)
                    gui_rect(scr, curx, cury + WENTRY_LINE_H - 2,
                             WENTRY_CHAR_W - 1, 2, e->cursor_c);
            }
        }
    }
}

static const WidgetOps wentry_ops = {
    wentry_draw
};

void wentry_init(WEntry *e, WPos pos, WSize size, char *buf, int cap,
                 int multiline)
{
    if (!e)
        return;
    widget_init(&e->base, &wentry_ops, WKIND_BASE, pos, size);
    e->buf = buf;
    e->cap = cap > 1 ? cap : 1;
    e->len = 0;
    e->cursor = 0;
    e->scroll = 0;
    e->multiline = multiline ? 1 : 0;
    e->flat = 0;
    e->readonly = 0;
    e->focused = 0;
    e->changed = 0;
    e->view_dirty = 1;
    e->fg = W95_TEXT;
    e->bg = W95_WINDOW;
    e->cursor_c = W95_TITLE;
    if (e->buf && e->cap > 0) {
        e->buf[0] = 0;
        e->len = 0;
    }
}

void wentry_set_text(WEntry *e, const char *s)
{
    int i = 0;
    if (!e || !e->buf || e->cap < 2)
        return;
    if (!s)
        s = "";
    while (s[i] && i + 1 < e->cap) {
        e->buf[i] = s[i];
        i++;
    }
    e->buf[i] = 0;
    e->len = i;
    e->cursor = i;
    e->scroll = 0;
    e->changed = 0;
    mark_view(e);
}

void wentry_set_cursor(WEntry *e, int pos)
{
    if (!e)
        return;
    if (pos < 0)
        pos = 0;
    if (pos > e->len)
        pos = e->len;
    e->cursor = pos;
    wentry_ensure_cursor_visible(e);
    mark_view(e);
}

const char *wentry_text(const WEntry *e)
{
    return (e && e->buf) ? e->buf : "";
}

int wentry_len(const WEntry *e)
{
    return e ? e->len : 0;
}

void wentry_set_scroll(WEntry *e, int scroll)
{
    int maxs, vis;
    if (!e)
        return;
    if (e->multiline) {
        vis = wentry_vis_rows(e);
        maxs = wentry_line_count(e) - vis;
        if (maxs < 0)
            maxs = 0;
        e->scroll = clampi(scroll, 0, maxs);
    } else {
        e->scroll = scroll < 0 ? 0 : scroll;
    }
    mark_view(e);
}

int wentry_scroll(const WEntry *e)
{
    return e ? e->scroll : 0;
}

void wentry_scroll_to_end(WEntry *e)
{
    int maxs, vis;
    if (!e || !e->multiline)
        return;
    vis = wentry_vis_rows(e);
    maxs = wentry_line_count(e) - vis;
    if (maxs < 0)
        maxs = 0;
    e->scroll = maxs;
    mark_view(e);
}

void wentry_set_focused(WEntry *e, int on)
{
    if (!e)
        return;
    if (on)
        widget_focus_set(&e->base);
    else if (widget_has_focus(&e->base))
        widget_focus_set(0);
    e->focused = on ? 1 : 0;
    mark_view(e);
}

void wentry_set_flat(WEntry *e, int on)
{
    if (!e)
        return;
    e->flat = on ? 1 : 0;
    mark_view(e);
}

void wentry_set_readonly(WEntry *e, int on)
{
    if (!e)
        return;
    e->readonly = on ? 1 : 0;
    if (e->readonly) {
        e->bg = W95_FACE;
        e->fg = W95_TEXT;
        if (widget_has_focus(&e->base))
            widget_focus_set(0);
        e->focused = 0;
    } else {
        e->bg = W95_WINDOW;
        e->fg = W95_TEXT;
    }
    mark_view(e);
}

void wentry_set_colors(WEntry *e, uint32_t fg, uint32_t bg, uint32_t cursor_c)
{
    if (!e)
        return;
    e->fg = fg;
    e->bg = bg;
    e->cursor_c = cursor_c;
    mark_view(e);
}

void wentry_key(WEntry *e, int key)
{
    if (!e || e->readonly || !widget_has_focus(&e->base))
        return;
    e->focused = 1;
    if (key == 8 || key == 127) {
        delete_back(e);
    } else if (key == 0x100) {
        move_up(e);
    } else if (key == 0x101) {
        move_down(e);
    } else if (key == 0x102) {
        move_left(e);
    } else if (key == 0x103) {
        move_right(e);
    } else if (key == '\r' || key == '\n') {
        if (e->multiline)
            insert_char(e, '\n');
    } else if (key == '\t' || key == 9) {
        /* Soft tab: 4 spaces — keeps column math simple. */
        insert_char(e, ' ');
        insert_char(e, ' ');
        insert_char(e, ' ');
        insert_char(e, ' ');
    } else if (key >= 32 && key < 127) {
        insert_char(e, (char)key);
    }
}

void wentry_input(WEntry *e, int mx, int my, int buttons)
{
    int cx, cy, cw, ch, cols, vis, row, col, start, i, c;
    if (!e || !e->base.visible)
        return;
    if (!(buttons & 1) || !widget_hit(&e->base, mx, my))
        return;

    /* Readonly: allow click-to-scroll focus only for viewing, no caret edits. */
    if (!e->readonly) {
        widget_focus_set(&e->base);
        e->focused = 1;
    }
    content_box(e, &cx, &cy, &cw, &ch);
    cols = wentry_vis_cols(e);

    if (e->multiline) {
        vis = wentry_vis_rows(e);
        row = (my - cy) / WENTRY_LINE_H;
        if (row < 0)
            row = 0;
        if (row >= vis)
            row = vis - 1;
        row += e->scroll;
        col = (mx - cx) / WENTRY_CHAR_W;
        if (col < 0)
            col = 0;
        start = line_start(e, row);
        c = 0;
        i = start;
        while (i < e->len && e->buf[i] != '\n' && c < col) {
            i++;
            c++;
        }
        if (!e->readonly)
            e->cursor = i;
    } else {
        col = e->scroll + (mx - cx) / WENTRY_CHAR_W;
        if (col < 0)
            col = 0;
        if (col > e->len)
            col = e->len;
        if (!e->readonly)
            e->cursor = col;
    }
    mark_view(e);
    if (!e->readonly)
        wentry_ensure_cursor_visible(e);
}

int wentry_was_changed(const WEntry *e)
{
    return e && e->changed;
}

void wentry_ack_changed(WEntry *e)
{
    if (e)
        e->changed = 0;
}

int wentry_view_dirty(const WEntry *e)
{
    return e && e->view_dirty;
}

void wentry_ack_view(WEntry *e)
{
    if (e)
        e->view_dirty = 0;
}
