#include "libgui.h"
#include "user/guic/guic.h"

static GuiScreen *gp;
static char display[64] = "0";
static long acc;
static char pending;
static int fresh = 1;
static WWindow win;
static WFrame root;
static WEntry entry;
static WButton keys[16];
static char key_labs[16][2];
static const char labels[16] = {
    'C', '/', '*', '-',
    '7', '8', '9', '+',
    '4', '5', '6', '=',
    '1', '2', '3', '0'
};
static int dirty = 1;
static int mx, my, mbtn;

static long parse_display(void)
{
    const char *s = wentry_text(&entry);
    long v = 0;
    int neg = 0;
    if (!s || !s[0])
        return 0;
    if (s[0] == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        if (v > (2147483647L - (*s - '0')) / 10)
            break;
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

static void set_display(long v)
{
    char tmp[32];
    int i = 0, n = 0;
    long x = v;
    if (x < 0) {
        display[n++] = '-';
        x = -x;
    }
    if (x == 0)
        tmp[i++] = '0';
    while (x > 0 && i < 30) {
        tmp[i++] = (char)('0' + (x % 10));
        x /= 10;
    }
    while (i > 0 && n + 1 < (int)sizeof(display))
        display[n++] = tmp[--i];
    display[n] = 0;
    wentry_set_text(&entry, display);
    dirty = 1;
}

static void apply_op(char op, long v)
{
    if (pending == '+')
        acc += v;
    else if (pending == '-')
        acc -= v;
    else if (pending == '*')
        acc *= v;
    else if (pending == '/')
        acc = (v == 0) ? acc : (acc / v);
    else
        acc = v;
    pending = op;
    fresh = 1;
    set_display(acc);
}

static void on_keypad(WButton *b, void *userdata)
{
    char ch = (char)(unsigned long)userdata;
    (void)b;
    if (ch == 'C') {
        acc = 0;
        pending = 0;
        fresh = 1;
        set_display(0);
        return;
    }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '=') {
        long v = parse_display();
        if (ch == '=') {
            apply_op(0, v);
            pending = 0;
        } else
            apply_op(ch, v);
        return;
    }
    if (fresh) {
        display[0] = ch;
        display[1] = 0;
        fresh = 0;
    } else {
        int n = strlen_u(display);
        if (n == 1 && display[0] == '0') {
            display[0] = ch;
            display[1] = 0;
        } else if (n + 1 < (int)sizeof(display)) {
            display[n] = ch;
            display[n + 1] = 0;
        }
    }
    wentry_set_text(&entry, display);
    dirty = 1;
}

static void feed_char(int key)
{
    if (key == 27 || key == 'c' || key == 'C') {
        on_keypad(0, (void *)(unsigned long)'C');
        return;
    }
    if (key == '\r' || key == '\n' || key == '=') {
        on_keypad(0, (void *)(unsigned long)'=');
        return;
    }
    if (key == '+' || key == '-' || key == '*' || key == '/') {
        on_keypad(0, (void *)(unsigned long)key);
        return;
    }
    if (key >= '0' && key <= '9')
        on_keypad(0, (void *)(unsigned long)key);
}

int main(void)
{
    GuiScreen g;
    SysInputEvent ev;
    int run = 1, i, r, c;
    int prev_hot[16], prev_stage[16];

    if (gui_init_titled(&g, 220, 280, "Calculator") != 0)
        return 1;
    gp = &g;
    if (gui_hosted(&g))
        sys_gui_set_flags(g.win_id, GUI_FLAG_NO_MINMAX);

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "Calculator", 1, gui_hosted(&g) ? 0 : 1);
    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                W95_FACE, 0, WFRAME_FLAT);
    wframe_set_active(&root, 1);
    wentry_init(&entry, wpos_abs(8, 8, WANCHOR_TL), wsize_abs(204, 24),
                display, sizeof(display), WENTRY_SINGLE);
    wentry_set_readonly(&entry, 1);
    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wentry_widget(&entry));

    for (i = 0; i < 16; i++) {
        key_labs[i][0] = labels[i];
        key_labs[i][1] = 0;
        r = i / 4;
        c = i % 4;
        wbutton_init(&keys[i], wpos_abs(8 + c * 50, 40 + r * 50, WANCHOR_TL),
                     wsize_abs(46, 44), key_labs[i]);
        wbutton_set_handler(&keys[i], on_keypad, (void *)(unsigned long)labels[i]);
        widget_add_child(wframe_widget(&root), wbutton_widget(&keys[i]));
        prev_hot[i] = 0;
        prev_stage[i] = 0;
    }
    set_display(0);
    widget_layout_root(wwindow_widget(&win), g.w, g.h);

    while (run) {
        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_KEY) {
                if (ev.key == KEY_ALT_F4)
                    run = 0;
                else
                    feed_char(ev.key);
            }
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons & MOUSE_BTN_LEFT;
            }
        }
        for (i = 0; i < 16; i++) {
            wbutton_input(&keys[i], mx, my, mbtn);
            if (wbutton_was_clicked(&keys[i])) {
                if (keys[i].on_click)
                    keys[i].on_click(&keys[i], keys[i].userdata);
                wbutton_ack_click(&keys[i]);
                dirty = 1;
            }
            if (keys[i].hot != prev_hot[i] || keys[i].stage != prev_stage[i]) {
                prev_hot[i] = keys[i].hot;
                prev_stage[i] = keys[i].stage;
                dirty = 1;
            }
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
