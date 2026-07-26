#include "libgui.h"
#include "user/guic/guic.h"

#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8
#define CW 320
#define CH 200
#define CANVAS_MAX (CW * CH)
#define TOOL_N 5
#define COL_N 12
#define SIZE_N 3
#define FILL_STACK 4096

enum {
    TOOL_PEN = 0,
    TOOL_ERASER = 1,
    TOOL_LINE = 2,
    TOOL_RECT = 3,
    TOOL_FILL = 4
};

static GuiScreen *gp;
static uint32_t canvas[CANVAS_MAX];
static uint32_t undo_buf[CANVAS_MAX];
static int have_undo;
static uint32_t color = 0x00000000u;
static uint32_t bg = 0x00FFFFFFu;
static int tool = TOOL_PEN;
static int brush = 1;
static const int brush_sizes[SIZE_N] = { 1, 3, 6 };
static WWindow win;
static WFrame root;
static WButton new_btn, save_btn, undo_btn;
static WButton tool_btn[TOOL_N];
static WButton size_btn[SIZE_N];
static WButton cols[COL_N];
static char tool_labs[TOOL_N][8];
static char size_labs[SIZE_N][4];
static const char *tool_names[TOOL_N] = { "Pen", "Eras", "Line", "Rect", "Fill" };
static const uint32_t palette[COL_N] = {
    0x00000000u, 0x00404040u, 0x00808080u, 0x00FFFFFFu,
    0x00FF0000u, 0x00FF8000u, 0x00FFFF00u, 0x0000FF00u,
    0x0000FFFFu, 0x000000FFu, 0x00FF00FFu, 0x00800040u
};
static int dirty = 1;
static int mx, my, mbtn, prev_btn;
static int prev_x = -1, prev_y = -1;
static int drag_x0 = -1, drag_y0 = -1;
static int dragging;
static int ox = 8, oy = 56;

static void canvas_clear(void)
{
    int i;
    for (i = 0; i < CW * CH; i++)
        canvas[i] = bg;
}

static void snapshot(void)
{
    int i;
    for (i = 0; i < CW * CH; i++)
        undo_buf[i] = canvas[i];
    have_undo = 1;
}

static void do_undo(void)
{
    int i;
    if (!have_undo)
        return;
    for (i = 0; i < CW * CH; i++) {
        uint32_t t = canvas[i];
        canvas[i] = undo_buf[i];
        undo_buf[i] = t;
    }
    dirty = 1;
}

static void plot_brush(int x, int y, uint32_t c)
{
    int r = brush / 2;
    int yy, xx;
    for (yy = -r; yy <= r; yy++) {
        for (xx = -r; xx <= r; xx++) {
            int px = x + xx, py = y + yy;
            if (px < 0 || py < 0 || px >= CW || py >= CH)
                continue;
            if (xx * xx + yy * yy > r * r + r)
                continue;
            canvas[py * CW + px] = c;
        }
    }
}

static void paint_line(int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int err = adx - ady;
    for (;;) {
        plot_brush(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = err * 2;
            if (e2 > -ady) {
                err -= ady;
                x0 += sx;
            }
            if (e2 < adx) {
                err += adx;
                y0 += sy;
            }
        }
    }
}

static void paint_rect(int x0, int y0, int x1, int y1, uint32_t c)
{
    int t;
    if (x0 > x1) {
        t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        t = y0;
        y0 = y1;
        y1 = t;
    }
    paint_line(x0, y0, x1, y0, c);
    paint_line(x0, y1, x1, y1, c);
    paint_line(x0, y0, x0, y1, c);
    paint_line(x1, y0, x1, y1, c);
}

static void flood_fill(int x, int y, uint32_t nc)
{
    static int sx[FILL_STACK], sy[FILL_STACK];
    int sp = 0;
    uint32_t oc;
    if (x < 0 || y < 0 || x >= CW || y >= CH)
        return;
    oc = canvas[y * CW + x];
    if (oc == nc)
        return;
    sx[sp] = x;
    sy[sp] = y;
    sp++;
    while (sp > 0) {
        int cx, cy, i;
        sp--;
        cx = sx[sp];
        cy = sy[sp];
        if (cx < 0 || cy < 0 || cx >= CW || cy >= CH)
            continue;
        if (canvas[cy * CW + cx] != oc)
            continue;
        canvas[cy * CW + cx] = nc;
        {
            int nx[4] = { cx + 1, cx - 1, cx, cx };
            int ny[4] = { cy, cy, cy + 1, cy - 1 };
            for (i = 0; i < 4; i++) {
                if (sp >= FILL_STACK)
                    break;
                sx[sp] = nx[i];
                sy[sp] = ny[i];
                sp++;
            }
        }
    }
}

static void put_u(char *d, int *n, int v)
{
    char tmp[8];
    int i = 0;
    if (v < 0)
        v = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0 && i < 7) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        d[(*n)++] = tmp[--i];
}

static void sync_tool_sticky(void)
{
    int i;
    for (i = 0; i < TOOL_N; i++)
        wbutton_set_sticky(&tool_btn[i], i == tool);
    for (i = 0; i < SIZE_N; i++)
        wbutton_set_sticky(&size_btn[i], brush_sizes[i] == brush);
}

static void on_new(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    snapshot();
    canvas_clear();
    dirty = 1;
}

static void on_undo(WButton *b, void *userdata)
{
    (void)b;
    (void)userdata;
    do_undo();
}

static void on_save(WButton *b, void *userdata)
{
    int fd, y;
    char hdr[48];
    int n = 0;
    (void)b;
    (void)userdata;
    fd = sys_open("/sys/tmp/paint.ppm", O_WRITE | O_CREATE | O_TRUNC);
    if (fd < 0)
        return;
    hdr[n++] = 'P';
    hdr[n++] = '3';
    hdr[n++] = '\n';
    put_u(hdr, &n, CW);
    hdr[n++] = ' ';
    put_u(hdr, &n, CH);
    hdr[n++] = '\n';
    put_u(hdr, &n, 255);
    hdr[n++] = '\n';
    sys_write(fd, hdr, n);
    for (y = 0; y < CH; y++) {
        int x;
        for (x = 0; x < CW; x++) {
            uint32_t p = canvas[y * CW + x];
            int r = (p >> 16) & 255, g = (p >> 8) & 255, bl = p & 255;
            char trip[20];
            int tn = 0;
            put_u(trip, &tn, r);
            trip[tn++] = ' ';
            put_u(trip, &tn, g);
            trip[tn++] = ' ';
            put_u(trip, &tn, bl);
            trip[tn++] = ' ';
            sys_write(fd, trip, tn);
        }
        sys_write(fd, "\n", 1);
    }
    sys_close(fd);
}

static void on_tool(WButton *b, void *userdata)
{
    (void)b;
    tool = (int)(unsigned long)userdata;
    sync_tool_sticky();
    dirty = 1;
}

static void on_size(WButton *b, void *userdata)
{
    (void)b;
    brush = (int)(unsigned long)userdata;
    sync_tool_sticky();
    dirty = 1;
}

static void on_color(WButton *b, void *userdata)
{
    (void)b;
    color = (uint32_t)(unsigned long)userdata;
    dirty = 1;
}

static int in_canvas(int x, int y)
{
    return x >= ox && y >= oy && x < ox + CW && y < oy + CH;
}

static void draw_preview(GuiScreen *g, int x0, int y0, int x1, int y1)
{
    int x, y, t;
    if (tool == TOOL_LINE) {
        int dx = x1 - x0, dy = y1 - y0;
        int sx = dx < 0 ? -1 : 1;
        int sy = dy < 0 ? -1 : 1;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int err = adx - ady;
        x = x0;
        y = y0;
        for (;;) {
            if (x >= 0 && y >= 0 && x < CW && y < CH)
                g->fb[(oy + y) * g->pitch + (ox + x)] = color;
            if (x == x1 && y == y1)
                break;
            {
                int e2 = err * 2;
                if (e2 > -ady) {
                    err -= ady;
                    x += sx;
                }
                if (e2 < adx) {
                    err += adx;
                    y += sy;
                }
            }
        }
        return;
    }
    if (tool == TOOL_RECT) {
        if (x0 > x1) {
            t = x0;
            x0 = x1;
            x1 = t;
        }
        if (y0 > y1) {
            t = y0;
            y0 = y1;
            y1 = t;
        }
        for (x = x0; x <= x1; x++) {
            if (x >= 0 && x < CW) {
                if (y0 >= 0 && y0 < CH)
                    g->fb[(oy + y0) * g->pitch + (ox + x)] = color;
                if (y1 >= 0 && y1 < CH)
                    g->fb[(oy + y1) * g->pitch + (ox + x)] = color;
            }
        }
        for (y = y0; y <= y1; y++) {
            if (y >= 0 && y < CH) {
                if (x0 >= 0 && x0 < CW)
                    g->fb[(oy + y) * g->pitch + (ox + x0)] = color;
                if (x1 >= 0 && x1 < CW)
                    g->fb[(oy + y) * g->pitch + (ox + x1)] = color;
            }
        }
    }
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1, i;
    int prev_hot[32], prev_stage[32], ntrack = 0;

    if (gui_init_titled(&g, 360, 300, "Paint") != 0)
        return 1;
    gp = &g;
    if (gui_hosted(&g))
        sys_gui_set_flags(g.win_id, GUI_FLAG_NO_MINMAX);
    canvas_clear();
    have_undo = 0;
    brush = brush_sizes[0];

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Paint", 1, gui_hosted(&g) ? 0 : 1);
    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, 0, WFRAME_FLAT);
    wframe_set_active(&root, 1);

    wbutton_init(&new_btn, wpos_abs(4, 4, WANCHOR_TL), wsize_abs(36, 20), "New");
    wbutton_set_handler(&new_btn, on_new, 0);
    wbutton_init(&save_btn, wpos_abs(44, 4, WANCHOR_TL), wsize_abs(40, 20), "Save");
    wbutton_set_handler(&save_btn, on_save, 0);
    wbutton_init(&undo_btn, wpos_abs(88, 4, WANCHOR_TL), wsize_abs(40, 20), "Undo");
    wbutton_set_handler(&undo_btn, on_undo, 0);

    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wbutton_widget(&new_btn));
    widget_add_child(wframe_widget(&root), wbutton_widget(&save_btn));
    widget_add_child(wframe_widget(&root), wbutton_widget(&undo_btn));

    for (i = 0; i < TOOL_N; i++) {
        int n = 0;
        const char *s = tool_names[i];
        while (s[n] && n < 7) {
            tool_labs[i][n] = s[n];
            n++;
        }
        tool_labs[i][n] = 0;
        wbutton_init(&tool_btn[i], wpos_abs(140 + i * 40, 4, WANCHOR_TL),
                     wsize_abs(38, 20), tool_labs[i]);
        wbutton_set_handler(&tool_btn[i], on_tool, (void *)(unsigned long)i);
        widget_add_child(wframe_widget(&root), wbutton_widget(&tool_btn[i]));
    }
    for (i = 0; i < SIZE_N; i++) {
        size_labs[i][0] = (char)('1' + i);
        size_labs[i][1] = 0;
        wbutton_init(&size_btn[i], wpos_abs(4 + i * 24, 28, WANCHOR_TL),
                     wsize_abs(22, 20), size_labs[i]);
        wbutton_set_handler(&size_btn[i], on_size,
                            (void *)(unsigned long)brush_sizes[i]);
        widget_add_child(wframe_widget(&root), wbutton_widget(&size_btn[i]));
    }
    for (i = 0; i < COL_N; i++) {
        wbutton_init(&cols[i], wpos_abs(90 + i * 20, 28, WANCHOR_TL),
                     wsize_abs(18, 20), " ");
        wbutton_set_colors(&cols[i], W95_TEXT, palette[i], palette[i]);
        wbutton_set_handler(&cols[i], on_color, (void *)(unsigned long)palette[i]);
        widget_add_child(wframe_widget(&root), wbutton_widget(&cols[i]));
    }
    sync_tool_sticky();
    widget_layout_root(wwindow_widget(&win), g.w, g.h);

    while (run) {
        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY) {
                int k = ev.key;
                if (k == KEY_ALT_F4)
                    run = 0;
                else if (k == '1')
                    on_tool(0, (void *)(unsigned long)TOOL_PEN);
                else if (k == '2')
                    on_tool(0, (void *)(unsigned long)TOOL_ERASER);
                else if (k == '3')
                    on_tool(0, (void *)(unsigned long)TOOL_LINE);
                else if (k == '4')
                    on_tool(0, (void *)(unsigned long)TOOL_RECT);
                else if (k == '5')
                    on_tool(0, (void *)(unsigned long)TOOL_FILL);
                else if (k == '[') {
                    if (brush == brush_sizes[2])
                        on_size(0, (void *)(unsigned long)brush_sizes[1]);
                    else
                        on_size(0, (void *)(unsigned long)brush_sizes[0]);
                } else if (k == ']') {
                    if (brush == brush_sizes[0])
                        on_size(0, (void *)(unsigned long)brush_sizes[1]);
                    else
                        on_size(0, (void *)(unsigned long)brush_sizes[2]);
                } else if (k == 26)
                    do_undo();
                else if (k == 'n' || k == 'N')
                    on_new(0, 0);
                else if (k == 19)
                    on_save(0, 0);
            }
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons & MOUSE_BTN_LEFT;
            }
        }

        ntrack = 0;
#define TRACK(b) do { \
            prev_hot[ntrack] = (b).hot; \
            prev_stage[ntrack] = (b).stage; \
            wbutton_input(&(b), mx, my, mbtn); \
            if (wbutton_was_clicked(&(b))) { \
                if ((b).on_click) (b).on_click(&(b), (b).userdata); \
                wbutton_ack_click(&(b)); \
                dirty = 1; \
            } \
            if ((b).hot != prev_hot[ntrack] || (b).stage != prev_stage[ntrack]) \
                dirty = 1; \
            ntrack++; \
        } while (0)

        TRACK(new_btn);
        TRACK(save_btn);
        TRACK(undo_btn);
        for (i = 0; i < TOOL_N; i++)
            TRACK(tool_btn[i]);
        for (i = 0; i < SIZE_N; i++)
            TRACK(size_btn[i]);
        for (i = 0; i < COL_N; i++)
            TRACK(cols[i]);
#undef TRACK

        if ((mbtn & 1) && in_canvas(mx, my)) {
            int cx = mx - ox, cy = my - oy;
            if (!(prev_btn & 1)) {
                if (tool == TOOL_PEN || tool == TOOL_ERASER || tool == TOOL_FILL)
                    snapshot();
                if (tool == TOOL_LINE || tool == TOOL_RECT) {
                    snapshot();
                    drag_x0 = cx;
                    drag_y0 = cy;
                    dragging = 1;
                } else if (tool == TOOL_FILL) {
                    flood_fill(cx, cy, color);
                    dirty = 1;
                } else if (tool == TOOL_PEN) {
                    plot_brush(cx, cy, color);
                    prev_x = cx;
                    prev_y = cy;
                    dirty = 1;
                } else if (tool == TOOL_ERASER) {
                    plot_brush(cx, cy, bg);
                    prev_x = cx;
                    prev_y = cy;
                    dirty = 1;
                }
            } else if (tool == TOOL_PEN || tool == TOOL_ERASER) {
                uint32_t c = (tool == TOOL_ERASER) ? bg : color;
                if (prev_x >= 0)
                    paint_line(prev_x, prev_y, cx, cy, c);
                else
                    plot_brush(cx, cy, c);
                prev_x = cx;
                prev_y = cy;
                dirty = 1;
            } else if (dragging) {
                dirty = 1;
            }
        } else {
            if ((prev_btn & 1) && dragging && drag_x0 >= 0) {
                int cx = mx - ox, cy = my - oy;
                if (cx < 0)
                    cx = 0;
                if (cy < 0)
                    cy = 0;
                if (cx >= CW)
                    cx = CW - 1;
                if (cy >= CH)
                    cy = CH - 1;
                if (tool == TOOL_LINE)
                    paint_line(drag_x0, drag_y0, cx, cy, color);
                else if (tool == TOOL_RECT)
                    paint_rect(drag_x0, drag_y0, cx, cy, color);
                dirty = 1;
            }
            dragging = 0;
            drag_x0 = drag_y0 = -1;
            prev_x = prev_y = -1;
        }
        prev_btn = mbtn;

        if (dirty || dragging) {
            int x, y;
            gui_fill(&g, W95_FACE);
            widget_draw(wwindow_widget(&win), &g);
            gui_rect(&g, ox - 1, oy - 1, CW + 2, CH + 2, W95_DARKSHADOW);
            for (y = 0; y < CH; y++)
                for (x = 0; x < CW; x++)
                    g.fb[(oy + y) * g.pitch + (ox + x)] = canvas[y * CW + x];
            if (dragging && drag_x0 >= 0 && in_canvas(mx, my))
                draw_preview(&g, drag_x0, drag_y0, mx - ox, my - oy);
            gui_rect(&g, 340, 28, 14, 20, color);
            gui_present(&g);
            dirty = 0;
        }
        sys_yield();
    }
    gui_shutdown(&g);
    return 0;
}
