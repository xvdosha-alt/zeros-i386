#include "libgui.h"
#include "user/guic/guic.h"

#define O_READ 1
#define BUF_MAX 4096

static GuiScreen *gp;
static char path[128] = "/sys/etc/gui_launch.txt";
static unsigned char data[BUF_MAX];
static int data_len;
static int scroll;
static WWindow win;
static WFrame root;
static WButton open_btn;
static WLabel path_lbl;
static WFileDialog open_dlg;
static WScrollBar sb;
static int dirty = 1;
static int mx, my, mbtn;

static void load_file(const char *p)
{
    int fd, n;
    strncpy_u(path, p, sizeof(path));
    wlabel_set_text(&path_lbl, path);
    data_len = 0;
    scroll = 0;
    fd = sys_open(path, O_READ);
    if (fd < 0)
        return;
    n = sys_read(fd, data, BUF_MAX);
    sys_close(fd);
    if (n < 0)
        n = 0;
    data_len = n;
    dirty = 1;
}

static void on_open_ok(WFileDialog *d, const char *chosen, void *userdata)
{
    (void)d;
    (void)userdata;
    if (chosen && chosen[0])
        load_file(chosen);
}

static void on_open(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    wfiledlg_show(&open_dlg, path);
}

static void draw_hex(GuiScreen *g)
{
    int rows = (g->h - 48) / 12;
    int i, row, col, off;
    char line[80];
    for (row = 0; row < rows; row++) {
        off = (scroll + row) * 16;
        if (off >= data_len)
            break;
        /* offset */
        {
            unsigned o = (unsigned)off;
            int p = 0;
            char hexd[] = "0123456789ABCDEF";
            line[p++] = hexd[(o >> 12) & 15];
            line[p++] = hexd[(o >> 8) & 15];
            line[p++] = hexd[(o >> 4) & 15];
            line[p++] = hexd[o & 15];
            line[p++] = ':';
            line[p++] = ' ';
            for (col = 0; col < 16 && off + col < data_len; col++) {
                unsigned char b = data[off + col];
                line[p++] = hexd[b >> 4];
                line[p++] = hexd[b & 15];
                line[p++] = ' ';
            }
            while (col < 16) {
                line[p++] = ' ';
                line[p++] = ' ';
                line[p++] = ' ';
                col++;
            }
            line[p++] = ' ';
            for (col = 0; col < 16 && off + col < data_len; col++) {
                unsigned char b = data[off + col];
                line[p++] = (b >= 32 && b < 127) ? (char)b : '.';
            }
            line[p] = 0;
            gui_text(g, 8, 40 + row * 12, line, W95_TEXT, 0xFFFFFFFFu);
        }
    }
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1;

    if (gui_init_titled(&g, 640, 400, "Hex") != 0)
        return 1;
    gp = &g;
    if (gui_hosted(&g))
        sys_gui_set_flags(g.win_id, GUI_FLAG_RESIZABLE);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Hex Viewer", 1, gui_hosted(&g) ? 0 : 1);
    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, 0, WFRAME_FLAT);
    wbutton_init(&open_btn, wpos_abs(4, 4, WANCHOR_TL), wsize_abs(48, 22), "Open");
    wbutton_set_handler(&open_btn, on_open, 0);
    wlabel_init(&path_lbl, wpos_abs(60, 10, WANCHOR_TL), wsize_abs(0, 8),
                path, W95_TEXT, 0xFFFFFFFFu);
    wfiledlg_init(&open_dlg, 0);
    wfiledlg_set_handler(&open_dlg, on_open_ok, 0);
    wscroll_init(&sb, wpos_abs(0, 0, WANCHOR_TR), wsize_abs(WSCROLL_W, 100));
    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wbutton_widget(&open_btn));
    widget_add_child(wframe_widget(&root), wlabel_widget(&path_lbl));
    load_file(path);
    widget_layout_root(wwindow_widget(&win), g.w, g.h);

    while (run) {
        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY) {
                if (wfiledlg_visible(&open_dlg))
                    wfiledlg_key(&open_dlg, ev.key);
                else if (ev.key == KEY_ALT_F4)
                    run = 0;
                else if (ev.key == 0x100 && scroll > 0) {
                    scroll--;
                    dirty = 1;
                } else if (ev.key == 0x101) {
                    scroll++;
                    dirty = 1;
                }
            }
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            }
        }
        if (wfiledlg_visible(&open_dlg)) {
            wfiledlg_layout(&open_dlg, 0, 0, g.w, g.h);
            wfiledlg_input(&open_dlg, mx, my, mbtn);
            dirty = 1;
        } else {
            wbutton_input(&open_btn, mx, my, mbtn);
            if (wbutton_was_clicked(&open_btn)) {
                on_open(&open_btn, 0);
                wbutton_ack_click(&open_btn);
            }
        }
        if (dirty) {
            gui_fill(&g, W95_FACE);
            widget_draw(wwindow_widget(&win), &g);
            draw_hex(&g);
            if (wfiledlg_visible(&open_dlg)) {
                wfiledlg_layout(&open_dlg, 0, 0, g.w, g.h);
                wfiledlg_draw(&open_dlg, &g);
            }
            gui_present(&g);
            dirty = 0;
        }
        sys_yield();
    }
    gui_shutdown(&g);
    return 0;
}
