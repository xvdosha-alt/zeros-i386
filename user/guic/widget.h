#ifndef USER_WIDGET_H
#define USER_WIDGET_H

#include "../gui/libgui.h"

/* Win95 palette — defaults for guic widgets only (libgui GUI_* unchanged). */
enum {
    W95_FACE = 0x00C0C0C0u,
    W95_WINDOW = 0x00FFFFFFu,
    W95_TEXT = 0x00000000u,
    W95_DIM = 0x00808080u,
    W95_HIGHLIGHT = 0x00FFFFFFu,
    W95_SHADOW = 0x00808080u,
    W95_DARKSHADOW = 0x00404040u,
    W95_HOT = 0x00D4D0C8u,
    W95_DOWN = 0x00A0A0A0u,
    W95_TITLE = 0x00000080u,
    W95_TITLE_LOST = 0x00808080u,
    W95_DESK = 0x00008080u
};

/* --- Position / Size ----------------------------------------------------- */

enum {
    WPOS_ABS = 0,
    WPOS_REL = 1
};

/* Which point (x,y) / (relx,rely) refers to. For ABS, x/y are insets from
 * the matching parent edges (TL: from left/top; BR: from right/bottom; …). */
enum {
    WANCHOR_TL = 0,
    WANCHOR_TC,
    WANCHOR_TR,
    WANCHOR_ML,
    WANCHOR_MC,
    WANCHOR_MR,
    WANCHOR_BL,
    WANCHOR_BC,
    WANCHOR_BR
};

enum {
    WSIZE_ABS = 0,
    WSIZE_REL = 1
};

typedef struct {
    unsigned char mode;   /* WPOS_ABS / WPOS_REL */
    unsigned char anchor; /* WANCHOR_* (ABS; REL uses as placement point) */
    short _pad;
    int x, y;             /* ABS insets / offsets */
    float relx, rely;     /* REL fraction of parent content */
} WPos;

typedef struct {
    unsigned char mode; /* WSIZE_ABS / WSIZE_REL */
    unsigned char _pad[3];
    int w, h;           /* ABS */
    float relw, relh;   /* REL fraction of parent content */
} WSize;

WPos wpos_abs(int x, int y, unsigned char anchor);
WPos wpos_rel(float relx, float rely, unsigned char anchor);
WSize wsize_abs(int w, int h);
WSize wsize_rel(float relw, float relh);

/* Resolve size against parent content width/height. */
void wsize_resolve(const WSize *sz, int pw, int ph, int *ow, int *oh);
/* Top-left of widget inside parent content box (0,0 = content origin). */
void wpos_resolve_tl(const WPos *pos, int aw, int ah, int pw, int ph,
                     int *ox, int *oy);

/* --- Widget -------------------------------------------------------------- */

typedef struct Widget Widget;

enum {
    WKIND_BASE = 0,
    WKIND_LABEL,
    WKIND_BUTTON,
    WKIND_FRAME,
    WKIND_WINDOW
};

typedef struct {
    void (*draw)(Widget *self, GuiScreen *scr);
} WidgetOps;

struct Widget {
    const WidgetOps *ops;
    int kind;
    Widget *parent;
    Widget *child;
    Widget *next;
    WPos pos;
    WSize size;
    /* Resolved absolute box (screen / root FB coords). Always valid after layout. */
    int ax, ay, aw, ah;
    int visible;
};

void widget_init(Widget *w, const WidgetOps *ops, int kind, WPos pos, WSize size);
void widget_add_child(Widget *parent, Widget *child);
void widget_abs(const Widget *w, int *ax, int *ay);
int widget_hit(const Widget *w, int mx, int my);
int widget_pressed(const Widget *w, int mx, int my, int buttons);
void widget_draw(Widget *w, GuiScreen *scr);
void widget_set_pos(Widget *w, WPos pos);
void widget_set_size(Widget *w, WSize size);
/* Content box where children are laid out (may be inset for windows). */
void widget_content_box(const Widget *w, int *cx, int *cy, int *cw, int *ch);
/* Resolve root to fill scr, then all descendants. */
void widget_layout_root(Widget *root, int scr_w, int scr_h);
/* Set absolute box then layout children (for non-root containers). */
void widget_apply_box(Widget *w, int ax, int ay, int aw, int ah);
/* Re-resolve this node and descendants (parent abs/content already set). */
void widget_layout(Widget *w);

void widget_clear_children(Widget *parent);

/* Exactly one widget may hold keyboard focus in the process. */
void widget_focus_set(Widget *w);
Widget *widget_focus_get(void);
int widget_has_focus(const Widget *w);

/* --- WFrame -------------------------------------------------------------- */

enum {
    WFRAME_FLAT = 0,
    WFRAME_RAISED = 1,
    WFRAME_SUNKEN = 2
};

typedef struct {
    Widget base;
    uint32_t bg;
    uint32_t border;
    int style; /* WFRAME_* */
    int active;
} WFrame;

void wframe_init(WFrame *f, WPos pos, WSize size,
                 uint32_t bg, uint32_t border, int style);
void wframe_set_style(WFrame *f, int style);
void wframe_set_active(WFrame *f, int on);
int wframe_is_active(const WFrame *f);

static inline Widget *wframe_widget(WFrame *f)
{
    return f ? &f->base : 0;
}

#endif
