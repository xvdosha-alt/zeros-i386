#ifndef USER_WLIST_H
#define USER_WLIST_H

#include "widget.h"

#define WLIST_MAX 64
#define WLIST_ROW_H 14

typedef struct WListBox WListBox;
typedef void (*WListActivateFn)(WListBox *self, int index, void *userdata);

struct WListBox {
    Widget base;
    const char *items[WLIST_MAX];
    int nitems;
    int selected;
    int scroll;
    int hot;
    WListActivateFn on_activate;
    void *userdata;
};

void wlist_init(WListBox *l, WPos pos, WSize size);
void wlist_clear(WListBox *l);
int wlist_add(WListBox *l, const char *text); /* text must stay valid */
void wlist_set_selected(WListBox *l, int idx);
int wlist_selected(const WListBox *l);
void wlist_set_handler(WListBox *l, WListActivateFn fn, void *userdata);
void wlist_input(WListBox *l, int mx, int my, int buttons);
void wlist_key(WListBox *l, int key);

static inline Widget *wlist_widget(WListBox *l)
{
    return l ? &l->base : 0;
}

#endif
