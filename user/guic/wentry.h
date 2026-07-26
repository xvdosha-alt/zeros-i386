#ifndef USER_WENTRY_H
#define USER_WENTRY_H

#include "widget.h"

#define WENTRY_LINE_H 12
#define WENTRY_PAD 4
#define WENTRY_CHAR_W 8

enum {
    WENTRY_SINGLE = 0,
    WENTRY_MULTI = 1
};

typedef struct WEntry {
    Widget base;
    char *buf;
    int cap;
    int len;
    int cursor;
    int scroll; /* first visible line (multi) or first char col (single) */
    int multiline;
    int flat; /* 1 = no sunken bevel */
    int readonly; /* 1 = no edit; gray face by default */
    int focused;
    int changed; /* content edited */
    int view_dirty;
    uint32_t fg;
    uint32_t bg;
    uint32_t cursor_c;
} WEntry;

/* buf must remain valid; cap includes space for trailing NUL. */
void wentry_init(WEntry *e, WPos pos, WSize size, char *buf, int cap,
                 int multiline);
void wentry_set_text(WEntry *e, const char *s);
void wentry_set_cursor(WEntry *e, int pos);
const char *wentry_text(const WEntry *e);
int wentry_len(const WEntry *e);
int wentry_line_count(const WEntry *e);
int wentry_vis_rows(const WEntry *e);
int wentry_vis_cols(const WEntry *e);
void wentry_set_scroll(WEntry *e, int scroll);
int wentry_scroll(const WEntry *e);
void wentry_scroll_to_end(WEntry *e);
void wentry_ensure_cursor_visible(WEntry *e);
void wentry_set_focused(WEntry *e, int on);
void wentry_set_flat(WEntry *e, int on);
void wentry_set_readonly(WEntry *e, int on);
void wentry_set_colors(WEntry *e, uint32_t fg, uint32_t bg, uint32_t cursor_c);
void wentry_key(WEntry *e, int key);
void wentry_input(WEntry *e, int mx, int my, int buttons);
int wentry_was_changed(const WEntry *e);
void wentry_ack_changed(WEntry *e);
int wentry_view_dirty(const WEntry *e);
void wentry_ack_view(WEntry *e);

static inline Widget *wentry_widget(WEntry *e)
{
    return e ? &e->base : 0;
}

#endif
