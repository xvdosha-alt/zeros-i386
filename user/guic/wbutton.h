#ifndef USER_WBUTTON_H
#define USER_WBUTTON_H

#include "widget.h"

enum {
    WBTN_STAGE_IDLE = 0,
    WBTN_STAGE_HELD = 1,
    WBTN_STAGE_CLICKED = 2
};

/* Bevel look: idle uses `style`, pressed/sticky uses `style_down`. */
enum {
    WBTN_FLAT = 0,
    WBTN_RAISED = 1,
    WBTN_SUNKEN = 2
};

typedef struct WButton WButton;
typedef void (*WButtonClickFn)(WButton *self, void *userdata);

struct WButton {
    Widget base;
    const char *text;
    uint32_t fg;
    uint32_t bg;
    uint32_t bg_hot;
    uint32_t bg_down;
    uint32_t border;
    uint32_t border_hot;
    uint32_t border_down;
    int style;      /* WBTN_* when idle */
    int style_down; /* WBTN_* when pressed or sticky */
    int sticky;     /* 1 = always draw as pressed (active task tab) */
    int hot;
    int stage;
    WButtonClickFn on_click;
    void *userdata;
};

void wbutton_init(WButton *b, WPos pos, WSize size, const char *text);
void wbutton_set_colors(WButton *b, uint32_t fg, uint32_t bg, uint32_t bg_hot);
void wbutton_set_borders(WButton *b, uint32_t border, uint32_t border_hot,
                         uint32_t border_down);
/* style = idle relief, style_down = pressed/sticky relief */
void wbutton_set_style(WButton *b, int style, int style_down);
void wbutton_set_sticky(WButton *b, int on);
/* Compat: on → raised/sunken, off → flat/flat */
void wbutton_set_bordered(WButton *b, int on);
void wbutton_set_handler(WButton *b, WButtonClickFn fn, void *userdata);
void wbutton_set_text(WButton *b, const char *text);
void wbutton_input(WButton *b, int mx, int my, int buttons);
int wbutton_is_held(const WButton *b);
int wbutton_was_clicked(const WButton *b);
void wbutton_ack_click(WButton *b);

static inline Widget *wbutton_widget(WButton *b)
{
    return b ? &b->base : 0;
}

#endif
