#ifndef USER_WSCROLL_H
#define USER_WSCROLL_H

#include "widget.h"

#define WSCROLL_W 16
#define WSCROLL_BTN 16
#define WSCROLL_THUMB_MIN 12

typedef struct WScrollBar WScrollBar;
typedef void (*WScrollChangeFn)(WScrollBar *self, void *userdata);

struct WScrollBar {
    Widget base;
    int value;  /* scroll offset in content units */
    int page;   /* visible size */
    int range;  /* total content size; max value = max(0, range - page) */
    int dragging;
    int drag_off;   /* mouse y - thumb top when drag started */
    int arrow;      /* -1 up, +1 down, 0 none */
    int hold_ticks; /* repeat timer while arrow held */
    int track_dir;  /* page click: -1 above thumb, +1 below */
    int changed;
    WScrollChangeFn on_change;
    void *userdata;
};

void wscroll_init(WScrollBar *s, WPos pos, WSize size);
void wscroll_set_range(WScrollBar *s, int range, int page);
void wscroll_set_value(WScrollBar *s, int value);
int wscroll_value(const WScrollBar *s);
int wscroll_max(const WScrollBar *s); /* max valid value */
void wscroll_set_handler(WScrollBar *s, WScrollChangeFn fn, void *userdata);
void wscroll_input(WScrollBar *s, int mx, int my, int buttons);
int wscroll_was_changed(const WScrollBar *s);
void wscroll_ack(WScrollBar *s);

static inline Widget *wscroll_widget(WScrollBar *s)
{
    return s ? &s->base : 0;
}

#endif
