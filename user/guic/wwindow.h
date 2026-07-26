#ifndef USER_WWINDOW_H
#define USER_WWINDOW_H

#include "widget.h"

#define WWIN_TITLE_H 22
#define WWIN_FRAME   2
#define WWIN_FRAME_RESIZE 5
#define WWIN_GRIP 5

typedef struct {
    Widget base;
    const char *title;
    int resizable;
    int draw_chrome; /* 0 = content-only (hosted under wm chrome) */
    int show_minmax; /* 1 = draw minimize/maximize (default) */
    int active;
    uint32_t face;
    uint32_t title_foc;
    uint32_t title_lost;
} WWindow;

void wwindow_init(WWindow *win, WPos pos, WSize size, const char *title,
                  int resizable, int draw_chrome);
void wwindow_set_title(WWindow *win, const char *title);
void wwindow_set_active(WWindow *win, int on);
void wwindow_set_resizable(WWindow *win, int on);
void wwindow_set_minmax(WWindow *win, int on);
void wwindow_content_box(const WWindow *win, int *cx, int *cy, int *cw, int *ch);

static inline Widget *wwindow_widget(WWindow *win)
{
    return win ? &win->base : 0;
}

#endif
