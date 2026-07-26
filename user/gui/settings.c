#include "libgui.h"
#include "user/guic/guic.h"

#define O_READ 1
#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8

static int beep_on = 1;
static int mouse_accel = 1;
static WWindow win;
static WFrame root;
static WLabel title;
static WLabel mode_lbl;
static WButton beep_btn;
static WButton accel_btn;
static WButton save_btn;
static char mode_text[64];
static char beep_lab[24];
static char accel_lab[24];
static int dirty = 1;
static int mx, my, mbtn;

static void refresh_labels(void)
{
    SysFbInfo fi;
    int w = 0, h = 0;
    if (sys_fb_info(&fi) == 0) {
        w = (int)fi.width;
        h = (int)fi.height;
    }
    /* mode_text: "Mode: WxH" */
    {
        int n = 0;
        char tmp[16];
        int i = 0, v;
        mode_text[n++] = 'M';
        mode_text[n++] = 'o';
        mode_text[n++] = 'd';
        mode_text[n++] = 'e';
        mode_text[n++] = ':';
        mode_text[n++] = ' ';
        v = w;
        if (v <= 0)
            v = 0;
        if (v == 0)
            tmp[i++] = '0';
        while (v > 0 && i < 15) {
            tmp[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (i > 0)
            mode_text[n++] = tmp[--i];
        mode_text[n++] = 'x';
        i = 0;
        v = h;
        if (v <= 0)
            v = 0;
        if (v == 0)
            tmp[i++] = '0';
        while (v > 0 && i < 15) {
            tmp[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (i > 0)
            mode_text[n++] = tmp[--i];
        mode_text[n] = 0;
    }
    strncpy_u(beep_lab, beep_on ? "Beep: ON" : "Beep: OFF", sizeof(beep_lab));
    strncpy_u(accel_lab, mouse_accel ? "Mouse accel: ON" : "Mouse accel: OFF",
              sizeof(accel_lab));
    wlabel_set_text(&mode_lbl, mode_text);
    wbutton_set_text(&beep_btn, beep_lab);
    wbutton_set_text(&accel_btn, accel_lab);
}

static void load_settings(void)
{
    char buf[256];
    int fd, n, i, ls;
    fd = sys_open("/sys/etc/gui_settings.txt", O_READ);
    if (fd < 0)
        return;
    n = sys_read(fd, buf, (int)sizeof(buf) - 1);
    sys_close(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    ls = 0;
    for (i = 0; i <= n; i++) {
        char line[64];
        int len;
        if (i < n && buf[i] != '\n' && buf[i] != '\r')
            continue;
        len = i - ls;
        if (len <= 0) {
            ls = i + 1;
            continue;
        }
        if (len >= (int)sizeof(line))
            len = (int)sizeof(line) - 1;
        memcpy_u(line, buf + ls, len);
        line[len] = 0;
        ls = i + 1;
        if (line[0] == '#')
            continue;
        if (line[0] == 'b' && line[4] == '=')
            beep_on = line[5] != '0';
        if (line[0] == 'm' && line[11] == '=')
            mouse_accel = line[12] != '0';
    }
}

static void on_beep(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    beep_on = !beep_on;
    if (beep_on)
        sys_beep(880, 40);
    refresh_labels();
    dirty = 1;
}

static void on_accel(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    mouse_accel = !mouse_accel;
    refresh_labels();
    dirty = 1;
}

static void on_save(WButton *b, void *userdata)
{
    int fd;
    char body[128];
    int n = 0;
    (void)b;
    (void)userdata;
    body[n++] = 'b';
    body[n++] = 'e';
    body[n++] = 'e';
    body[n++] = 'p';
    body[n++] = '=';
    body[n++] = beep_on ? '1' : '0';
    body[n++] = '\n';
    body[n++] = 'm';
    body[n++] = 'o';
    body[n++] = 'u';
    body[n++] = 's';
    body[n++] = 'e';
    body[n++] = '_';
    body[n++] = 'a';
    body[n++] = 'c';
    body[n++] = 'c';
    body[n++] = 'e';
    body[n++] = 'l';
    body[n++] = '=';
    body[n++] = mouse_accel ? '1' : '0';
    body[n++] = '\n';
    fd = sys_open("/sys/etc/gui_settings.txt", O_WRITE | O_CREATE | O_TRUNC);
    if (fd >= 0) {
        sys_write(fd, body, n);
        sys_close(fd);
    }
    if (beep_on)
        sys_beep(660, 50);
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1;

    load_settings();
    if (gui_init_titled(&g, 320, 200, "Settings") != 0)
        return 1;
    if (gui_hosted(&g))
        sys_gui_set_flags(g.win_id, GUI_FLAG_NO_MINMAX);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Settings", 1, gui_hosted(&g) ? 0 : 1);
    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, 0, WFRAME_FLAT);
    wlabel_init(&title, wpos_abs(12, 12, WANCHOR_TL), wsize_abs(0, 8),
                "zerOS Settings", W95_TEXT, 0xFFFFFFFFu);
    wlabel_init(&mode_lbl, wpos_abs(12, 36, WANCHOR_TL), wsize_abs(0, 8),
                mode_text, W95_TEXT, 0xFFFFFFFFu);
    wbutton_init(&beep_btn, wpos_abs(12, 60, WANCHOR_TL), wsize_abs(160, 24),
                 beep_lab);
    wbutton_set_handler(&beep_btn, on_beep, 0);
    wbutton_init(&accel_btn, wpos_abs(12, 92, WANCHOR_TL), wsize_abs(180, 24),
                 accel_lab);
    wbutton_set_handler(&accel_btn, on_accel, 0);
    wbutton_init(&save_btn, wpos_abs(12, 140, WANCHOR_TL), wsize_abs(80, 24),
                 "Save");
    wbutton_set_handler(&save_btn, on_save, 0);
    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wlabel_widget(&title));
    widget_add_child(wframe_widget(&root), wlabel_widget(&mode_lbl));
    widget_add_child(wframe_widget(&root), wbutton_widget(&beep_btn));
    widget_add_child(wframe_widget(&root), wbutton_widget(&accel_btn));
    widget_add_child(wframe_widget(&root), wbutton_widget(&save_btn));
    refresh_labels();
    widget_layout_root(wwindow_widget(&win), g.w, g.h);

    while (run) {
        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY && ev.key == KEY_ALT_F4)
                run = 0;
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            }
        }
        wbutton_input(&beep_btn, mx, my, mbtn);
        wbutton_input(&accel_btn, mx, my, mbtn);
        wbutton_input(&save_btn, mx, my, mbtn);
        if (wbutton_was_clicked(&beep_btn)) {
            on_beep(0, 0);
            wbutton_ack_click(&beep_btn);
        }
        if (wbutton_was_clicked(&accel_btn)) {
            on_accel(0, 0);
            wbutton_ack_click(&accel_btn);
        }
        if (wbutton_was_clicked(&save_btn)) {
            on_save(0, 0);
            wbutton_ack_click(&save_btn);
        }
        if (dirty) {
            gui_fill(&g, W95_FACE);
            widget_draw(wwindow_widget(&win), &g);
            gui_present(&g);
            dirty = 0;
        }
        sys_yield();
    }
    gui_shutdown(&g);
    return 0;
}
