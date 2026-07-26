#ifndef USER_LIBGUI_H
#define USER_LIBGUI_H

#include "libmp.h"

enum {
    GUI_BG = 0x00101820u,
    GUI_PANEL = 0x001A2836u,
    GUI_WIN = 0x00202830u,
    GUI_TITLE = 0x002A3848u,
    GUI_TITLE_FOC = 0x00344858u,
    GUI_ACCENT = 0x00C4A35Au,
    GUI_TEXT = 0x00E8E4DCu,
    GUI_DIM = 0x00808890u,
    GUI_BTN = 0x00304050u,
    GUI_BTN_HOV = 0x00405870u,
    GUI_CLOSE = 0x00B05040u,
    GUI_BORDER = 0x00405060u,
    GUI_CURSOR = 0x00F0E8D8u
};

typedef struct {
    uint32_t *fb;
    int w;
    int h;
    int pitch;
    int hosted; /* 1 = window under display server */
    int win_id;
} GuiScreen;

/* If a display server (wm) is running, creates a client window.
 * Otherwise takes the fullscreen framebuffer (standalone). */
int gui_init(GuiScreen *g, int w, int h);
int gui_init_titled(GuiScreen *g, int w, int h, const char *title);
void gui_shutdown(GuiScreen *g);
void gui_present(GuiScreen *g);
void gui_present_rect(GuiScreen *g, int x, int y, int w, int h);
int gui_hosted(const GuiScreen *g);
/* Refresh w/h/fb after INP_RESIZE (hosted only). */
int gui_sync_size(GuiScreen *g);
/* Input: hosted apps get events from the server; else hardware poll. */
int gui_poll(GuiScreen *g, SysInputEvent *ev);

void gui_fill(GuiScreen *g, uint32_t color);
void gui_pixel(GuiScreen *g, int x, int y, uint32_t color);
void gui_rect(GuiScreen *g, int x, int y, int w, int h, uint32_t color);
void gui_border(GuiScreen *g, int x, int y, int w, int h, uint32_t color);
void gui_char(GuiScreen *g, int x, int y, char c, uint32_t fg, uint32_t bg);
void gui_text(GuiScreen *g, int x, int y, const char *s, uint32_t fg, uint32_t bg);
void gui_cursor(GuiScreen *g, int x, int y);
int gui_hit(int mx, int my, int x, int y, int w, int h);

#endif
