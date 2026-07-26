#include "libgui.h"
#include "termrun.h"
#include "user/guic/guic.h"

#define TERM_ROWS 56
#define TERM_LINE 124
#define TERM_BUF 8192
#define TERM_W 640
#define TERM_H 400
#define MARGIN 2
#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8

#define TERM_FG 0x00FFFFFFu
#define TERM_BG 0x00000000u

enum {
    MODE_PROMPT = 0,
    MODE_RUN = 1,
    MODE_DONE = 2
};

static GuiScreen *gp;
static GuiScreen g;
static int hosted;
static char lines[TERM_ROWS][TERM_LINE];
static int nlines;
static char input[TERM_LINE];
static int inlen;
static char live[TERM_LINE];
static char titlebar[48];
static char term_buf[TERM_BUF];
static int running;
static int mode;
static int dirty = 1;
static int quiet_submit;
static TermSession sess;
static int mx, my, mbtn;
static int col_max = 80;

static WWindow win;
static WFrame root;
static WEntry entry;

static void apply_title(void)
{
    if (hosted && g.win_id >= 0)
        sys_gui_title(g.win_id, titlebar);
}

static void set_title_prog(const char *path)
{
    const char *base = path ? path : "";
    const char *p;
    int n;
    if (path) {
        for (p = path; *p; p++) {
            if (*p == '/')
                base = p + 1;
        }
    }
    strncpy_u(titlebar, "gterm - ", sizeof(titlebar));
    n = strlen_u(titlebar);
    if (base[0])
        strncpy_u(titlebar + n, base, (int)sizeof(titlebar) - n);
    else
        strncpy_u(titlebar, "gterm", sizeof(titlebar));
    apply_title();
}

static void layout_chrome(void)
{
    int cx, cy, cw, ch;
    if (!gp)
        return;
    widget_layout_root(wwindow_widget(&win), gp->w, gp->h);
    widget_content_box(wframe_widget(&root), &cx, &cy, &cw, &ch);
    widget_set_pos(wentry_widget(&entry),
                   wpos_abs(MARGIN, MARGIN, WANCHOR_TL));
    widget_set_size(wentry_widget(&entry),
                    wsize_abs(cw - MARGIN * 2, ch - MARGIN * 2));
    widget_layout(wwindow_widget(&win));
    col_max = wentry_vis_cols(&entry);
    if (col_max > TERM_LINE - 3)
        col_max = TERM_LINE - 3;
    if (col_max < 8)
        col_max = 8;
}

static void rebuild_entry(void)
{
    int i, pos = 0;
    int live_off = -1;
    char prompt[TERM_LINE];

    for (i = 0; i < nlines; i++) {
        int L = strlen_u(lines[i]);
        if (pos + L + 2 >= TERM_BUF)
            break;
        if (pos)
            term_buf[pos++] = '\n';
        memcpy_u(term_buf + pos, lines[i], L);
        pos += L;
    }
    if (mode == MODE_RUN && live[0]) {
        int L = strlen_u(live);
        if (pos + L + 2 < TERM_BUF) {
            if (pos)
                term_buf[pos++] = '\n';
            live_off = pos;
            memcpy_u(term_buf + pos, live, L);
            pos += L;
        }
    } else if (mode == MODE_PROMPT) {
        prompt[0] = '}';
        prompt[1] = ' ';
        prompt[2] = 0;
        strncpy_u(prompt + 2, input, (int)sizeof(prompt) - 2);
        {
            int L = strlen_u(prompt);
            if (pos + L + 2 < TERM_BUF) {
                if (pos)
                    term_buf[pos++] = '\n';
                live_off = pos;
                memcpy_u(term_buf + pos, prompt, L);
                pos += L;
            }
        }
    }
    if (mode == MODE_RUN && !live[0] && pos > 0 && pos + 1 < TERM_BUF &&
        term_buf[pos - 1] != '\n')
        term_buf[pos++] = '\n';
    term_buf[pos] = 0;
    wentry_set_text(&entry, term_buf);
    wentry_scroll_to_end(&entry);
    if (mode == MODE_RUN && live_off >= 0) {
        int c = term_session_caret(&sess);
        int L = strlen_u(live);
        if (c < 0 || c > L)
            c = L;
        wentry_set_cursor(&entry, live_off + c);
        wentry_set_focused(&entry, 1);
    } else if (mode == MODE_PROMPT) {
        if (live_off >= 0)
            wentry_set_cursor(&entry, live_off + 2 + inlen);
        wentry_set_focused(&entry, 1);
    }
    dirty = 1;
}

static void push_line(const char *s)
{
    int i;
    if (nlines >= TERM_ROWS) {
        for (i = 0; i < TERM_ROWS - 1; i++)
            strncpy_u(lines[i], lines[i + 1], TERM_LINE);
        nlines = TERM_ROWS - 1;
    }
    strncpy_u(lines[nlines], s ? s : "", TERM_LINE);
    nlines++;
    live[0] = 0;
    rebuild_entry();
}

static void out_cb(void *ctx, const char *line)
{
    (void)ctx;
    push_line(line);
}

static int drain_child(int first_key)
{
    int fed = first_key;
    int spins = 0;
    int changed = 0;
    char prev_live[TERM_LINE];

    strncpy_u(prev_live, live, sizeof(prev_live));
    for (; spins++ < 64;) {
        if (!term_session_pump(&sess, fed, out_cb, 0)) {
            live[0] = 0;
            push_line("[process finished]");
            mode = MODE_DONE;
            strncpy_u(titlebar, "gterm", sizeof(titlebar));
            apply_title();
            return 1;
        }
        strncpy_u(live, term_session_live(&sess), sizeof(live));
        if (sess.painted)
            changed = 1;
        fed = -1;
        if (!sess.painted)
            break;
    }
    if (strcmp_u(prev_live, live) != 0)
        changed = 1;
    if (changed)
        rebuild_entry();
    return changed;
}

static int resolve_exec(const char *name, char *out, int outmax)
{
    char bin[128];
    int i, n;
    if (!name || !name[0] || !out || outmax < 2)
        return -1;
    for (i = 0; name[i]; i++) {
        if (name[i] == '/')
            break;
    }
    if (!name[i]) {
        strncpy_u(bin, "/sys/bin/", sizeof(bin));
        n = strlen_u(bin);
        strncpy_u(bin + n, name, (int)sizeof(bin) - n);
        if (sys_exists(bin)) {
            strncpy_u(out, bin, outmax);
            return 0;
        }
        strncpy_u(bin, "/sys/gui/", sizeof(bin));
        n = strlen_u(bin);
        strncpy_u(bin + n, name, (int)sizeof(bin) - n);
        {
            int L = strlen_u(bin);
            if (L + 4 < (int)sizeof(bin)) {
                bin[L] = '.';
                bin[L + 1] = 'w';
                bin[L + 2] = 'i';
                bin[L + 3] = 'n';
                bin[L + 4] = 0;
                if (sys_exists(bin)) {
                    strncpy_u(out, bin, outmax);
                    return 0;
                }
                bin[L] = 0;
            }
        }
        if (sys_exists(bin)) {
            strncpy_u(out, bin, outmax);
            return 0;
        }
    }
    if (sys_exists(name)) {
        strncpy_u(out, name, outmax);
        return 0;
    }
    return -1;
}

static void write_argv_tail(char *const *argv, int argc)
{
    char args[128];
    int i, pos, afd, L;
    args[0] = 0;
    pos = 0;
    for (i = 1; i < argc; i++) {
        L = strlen_u(argv[i]);
        if (pos + L + 2 >= (int)sizeof(args))
            break;
        if (pos)
            args[pos++] = ' ';
        memcpy_u(args + pos, argv[i], L);
        pos += L;
        args[pos] = 0;
    }
    afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
    if (afd >= 0) {
        sys_write(afd, args, strlen_u(args));
        sys_close(afd);
    }
}

static void clear_argv(void)
{
    int afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
    if (afd >= 0)
        sys_close(afd);
}

static void submit(void)
{
    char work[TERM_LINE];
    char path[128];
    char lined[TERM_LINE];
    char *argv[8];
    int argc;

    if (mode != MODE_PROMPT || !input[0])
        return;
    strncpy_u(work, input, sizeof(work));

    /* Keep "} command" in the scrollback; switch out of PROMPT before
     * rebuild so we don't flash an empty "} " after launch. */
    mode = MODE_RUN;
    live[0] = 0;
    if (!quiet_submit) {
        lined[0] = '}';
        lined[1] = ' ';
        lined[2] = 0;
        strncpy_u(lined + 2, input, (int)sizeof(lined) - 3);
        push_line(lined);
    } else {
        rebuild_entry();
    }
    inlen = 0;
    input[0] = 0;

    argc = split_args(work, argv, 8);
    if (argc < 1) {
        mode = MODE_PROMPT;
        rebuild_entry();
        return;
    }
    if (resolve_exec(argv[0], path, sizeof(path)) != 0) {
        push_line("not found");
        mode = MODE_PROMPT;
        rebuild_entry();
        return;
    }
    write_argv_tail(argv, argc);
    if (term_session_start(&sess, path) != 0) {
        push_line("spawn failed");
        mode = MODE_PROMPT;
        rebuild_entry();
        return;
    }
    set_title_prog(path);
    drain_child(-1);
    dirty = 1;
}

static void ensure_fb_size(void)
{
    int w, h;
    if (!gp || !gp->hosted || gp->win_id < 0)
        return;
    w = sys_gui_info(gp->win_id, GUI_INFO_W);
    h = sys_gui_info(gp->win_id, GUI_INFO_H);
    if (w < 32 || h < 32)
        return;
    if (w == gp->w && h == gp->h && gp->fb == (uint32_t *)sys_gui_fb(gp->win_id))
        return;
    if (gui_sync_size(gp) != 0)
        return;
    layout_chrome();
    rebuild_entry();
    dirty = 1;
}

int main(void)
{
    SysInputEvent ev;

    if (gui_init_titled(&g, TERM_W, TERM_H, "gterm") != 0) {
        println("gterm: fb init failed");
        return 1;
    }
    gp = &g;
    hosted = gui_hosted(&g);
    if (hosted)
        sys_gui_set_flags(g.win_id, GUI_FLAG_RESIZABLE);

    nlines = 0;
    inlen = 0;
    input[0] = 0;
    live[0] = 0;
    running = 1;
    mode = MODE_PROMPT;
    sess.alive = 0;
    dirty = 1;
    strncpy_u(titlebar, "gterm", sizeof(titlebar));
    apply_title();

    wwindow_init(&win, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                 "gterm", 1, hosted ? 0 : 1);
    wframe_init(&root, wpos_abs(0, 0, WANCHOR_TL), wsize_rel(1.0f, 1.0f),
                TERM_BG, 0, WFRAME_FLAT);
    wframe_set_active(&root, 1);

    wentry_init(&entry, wpos_abs(MARGIN, MARGIN, WANCHOR_TL),
                wsize_abs(100, 100), term_buf, TERM_BUF, WENTRY_MULTI);
    wentry_set_flat(&entry, 1);
    wentry_set_colors(&entry, TERM_FG, TERM_BG, TERM_FG);
    wentry_set_focused(&entry, 1);

    widget_add_child(wwindow_widget(&win), wframe_widget(&root));
    widget_add_child(wframe_widget(&root), wentry_widget(&entry));
    layout_chrome();

    {
        char argline[TERM_LINE];
        if (read_argv(argline, sizeof(argline)) > 0 && argline[0]) {
            clear_argv();
            strncpy_u(input, argline, sizeof(input));
            inlen = strlen_u(input);
            quiet_submit = 1;
            submit();
            quiet_submit = 0;
        } else {
            clear_argv();
            push_line("gterm - type an executable (bin name or path)");
        }
    }

    while (running) {
        int key_fed = -1;

        ensure_fb_size();

        while (gui_poll(&g, &ev) > 0) {
            if (ev.type == INP_MOUSE) {
                mx = ev.x;
                my = ev.y;
                mbtn = ev.buttons;
            } else if (ev.type == INP_RESIZE) {
                if (gui_sync_size(&g) == 0) {
                    layout_chrome();
                    rebuild_entry();
                    dirty = 1;
                }
            } else if (ev.type == INP_KEY) {
                if (ev.key == KEY_ALT_F4) {
                    running = 0;
                    dirty = 1;
                    break;
                }
                if (ev.key == 27) {
                    if (mode == MODE_RUN) {
                        key_fed = 27;
                        break;
                    }
                    continue;
                }
                if (mode == MODE_RUN) {
                    if (ev.key == 3) { /* Ctrl+C */
                        if (sess.pid > 0)
                            sys_kill(sess.pid);
                        key_fed = -1;
                        dirty = 1;
                        break;
                    }
                    if (ev.key == '\r')
                        key_fed = '\n';
                    else
                        key_fed = ev.key;
                    break;
                }
                if (mode != MODE_PROMPT)
                    continue;
                if (ev.key == '\r' || ev.key == '\n') {
                    submit();
                    continue;
                }
                if (ev.key == 8 || ev.key == 127) {
                    if (inlen > 0) {
                        inlen--;
                        input[inlen] = 0;
                        rebuild_entry();
                    }
                    continue;
                }
                if (ev.key >= 32 && ev.key < 127 && inlen + 1 < col_max) {
                    input[inlen++] = (char)ev.key;
                    input[inlen] = 0;
                    rebuild_entry();
                }
            }
        }

        if (mode == MODE_RUN) {
            if (drain_child(key_fed))
                dirty = 1;
        }

        if (dirty) {
            layout_chrome();
            gui_fill(&g, TERM_BG);
            widget_draw(wwindow_widget(&win), &g);
            if (!hosted)
                gui_cursor(&g, mx, my);
            gui_present(&g);
            dirty = 0;
        } else {
            sys_yield();
        }
    }
    if (mode == MODE_RUN)
        term_session_stop(&sess);
    gui_shutdown(&g);
    return 0;
}
