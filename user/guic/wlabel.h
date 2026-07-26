#ifndef USER_WLABEL_H
#define USER_WLABEL_H

#include "widget.h"

typedef struct {
    Widget base;
    const char *text;
    uint32_t fg;
    uint32_t bg;
} WLabel;

void wlabel_init(WLabel *l, WPos pos, WSize size, const char *text,
                 uint32_t fg, uint32_t bg);
void wlabel_set_text(WLabel *l, const char *text);

static inline Widget *wlabel_widget(WLabel *l)
{
    return l ? &l->base : 0;
}

#endif
