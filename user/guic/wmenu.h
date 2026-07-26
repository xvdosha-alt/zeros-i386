#ifndef USER_WMENU_H
#define USER_WMENU_H

#include "widget.h"

#define WMENU_MAX 12
#define WMENU_ITEM_H 18

typedef struct WMenu WMenu;
typedef void (*WMenuPickFn)(WMenu *self, int index, void *userdata);

struct WMenu {
    Widget base;
    const char *items[WMENU_MAX];
    int nitems;
    int open;
    int hot;
    WMenuPickFn on_pick;
    void *userdata;
};

void wmenu_init(WMenu *m, WPos pos, WSize size);
void wmenu_clear(WMenu *m);
int wmenu_add(WMenu *m, const char *text);
void wmenu_set_open(WMenu *m, int on);
int wmenu_is_open(const WMenu *m);
void wmenu_set_handler(WMenu *m, WMenuPickFn fn, void *userdata);
void wmenu_input(WMenu *m, int mx, int my, int buttons);

static inline Widget *wmenu_widget(WMenu *m)
{
    return m ? &m->base : 0;
}

#endif
