#include "wfiledlg.h"

static void join_path(const char *base, const char *name, char *out, int outmax)
{
    int n, k;
    if (!out || outmax < 2)
        return;
    if (!base || !base[0] || (base[0] == '/' && !base[1])) {
        out[0] = '/';
        strncpy_u(out + 1, name ? name : "", outmax - 1);
        return;
    }
    strncpy_u(out, base, outmax);
    n = strlen_u(out);
    if (n > 0 && out[n - 1] != '/' && n + 1 < outmax)
        out[n++] = '/';
    k = 0;
    while (name && name[k] && n + 1 < outmax)
        out[n++] = name[k++];
    out[n] = 0;
}

static void go_parent(char *p, int pathmax)
{
    int n;
    if (!p || pathmax < 2)
        return;
    n = strlen_u(p);
    if (n <= 1) {
        strncpy_u(p, "/", pathmax);
        return;
    }
    if (p[n - 1] == '/')
        n--;
    while (n > 1 && p[n - 1] != '/')
        n--;
    if (n <= 1)
        strncpy_u(p, "/", pathmax);
    else
        p[n] = 0;
}

static int is_dir_name(const char *base, const char *name)
{
    char full[128];
    char probe[4];
    if (!name || !name[0])
        return 0;
    if (!strcmp_u(name, ".") || !strcmp_u(name, ".."))
        return 1;
    join_path(base, name, full, sizeof(full));
    return sys_listdir(full, probe, sizeof(probe)) >= 0;
}

static void refresh(WFileDialog *d)
{
    int i, n, start, len;
    wlist_clear(&d->list);
    if (sys_listdir(d->path, d->listing, sizeof(d->listing)) < 0) {
        d->listing[0] = 0;
        return;
    }
    n = strlen_u(d->listing);
    start = 0;
    i = 0;
    while (start <= n && i < WLIST_MAX) {
        len = 0;
        while (start + len < n && d->listing[start + len] != '\n')
            len++;
        if (len > 0) {
            if (len >= WFILE_NAME_MAX)
                len = WFILE_NAME_MAX - 1;
            memcpy_u(d->names[i], d->listing + start, len);
            d->names[i][len] = 0;
            if (d->names[i][0] && strcmp_u(d->names[i], "."))
                wlist_add(&d->list, d->names[i]);
            i++;
        }
        start += len + 1;
    }
    wentry_set_text(&d->path_entry, d->path);
}

static void on_list(WListBox *l, int index, void *userdata)
{
    WFileDialog *d = (WFileDialog *)userdata;
    char full[WFILE_PATH_MAX];
    const char *name;
    (void)l;
    if (!d || index < 0 || index >= d->list.nitems)
        return;
    name = d->list.items[index];
    if (!strcmp_u(name, "..")) {
        go_parent(d->path, sizeof(d->path));
        refresh(d);
        return;
    }
    join_path(d->path, name, full, sizeof(full));
    if (is_dir_name(d->path, name)) {
        strncpy_u(d->path, full, sizeof(d->path));
        refresh(d);
        return;
    }
    strncpy_u(d->pathbuf, full, sizeof(d->pathbuf));
    wentry_set_text(&d->path_entry, d->pathbuf);
    if (!d->save_mode && d->on_ok) {
        d->on_ok(d, d->pathbuf, d->userdata);
        wfiledlg_hide(d);
    }
}

static void on_ok(WButton *b, void *userdata)
{
    WFileDialog *d = (WFileDialog *)userdata;
    const char *p;
    (void)b;
    if (!d)
        return;
    p = wentry_text(&d->path_entry);
    if (!p || !p[0])
        return;
    strncpy_u(d->pathbuf, p, sizeof(d->pathbuf));
    if (d->on_ok)
        d->on_ok(d, d->pathbuf, d->userdata);
    wfiledlg_hide(d);
}

static void on_cancel(WButton *b, void *userdata)
{
    WFileDialog *d = (WFileDialog *)userdata;
    (void)b;
    wfiledlg_hide(d);
}

void wfiledlg_init(WFileDialog *d, int save_mode)
{
    if (!d)
        return;
    memset_u(d, 0, sizeof(*d));
    d->save_mode = save_mode ? 1 : 0;
    wframe_init(&d->frame, wpos_abs(0, 0, WANCHOR_TL), wsize_abs(320, 240),
                W95_FACE, W95_DARKSHADOW, WFRAME_RAISED);
    wlabel_init(&d->title, wpos_abs(8, 8, WANCHOR_TL), wsize_abs(0, 8),
                save_mode ? "Save As" : "Open", W95_TEXT, 0xFFFFFFFFu);
    wlist_init(&d->list, wpos_abs(8, 28, WANCHOR_TL), wsize_abs(304, 140));
    wlist_set_handler(&d->list, on_list, d);
    wentry_init(&d->path_entry, wpos_abs(8, 176, WANCHOR_TL), wsize_abs(304, 20),
                d->pathbuf, WFILE_PATH_MAX, WENTRY_SINGLE);
    wbutton_init(&d->ok_btn, wpos_abs(160, 204, WANCHOR_TL), wsize_abs(72, 24),
                 save_mode ? "Save" : "Open");
    wbutton_set_handler(&d->ok_btn, on_ok, d);
    wbutton_init(&d->cancel_btn, wpos_abs(240, 204, WANCHOR_TL), wsize_abs(72, 24),
                 "Cancel");
    wbutton_set_handler(&d->cancel_btn, on_cancel, d);
    widget_add_child(wframe_widget(&d->frame), wlabel_widget(&d->title));
    widget_add_child(wframe_widget(&d->frame), wlist_widget(&d->list));
    widget_add_child(wframe_widget(&d->frame), wentry_widget(&d->path_entry));
    widget_add_child(wframe_widget(&d->frame), wbutton_widget(&d->ok_btn));
    widget_add_child(wframe_widget(&d->frame), wbutton_widget(&d->cancel_btn));
    d->visible = 0;
    d->frame.base.visible = 0;
}

void wfiledlg_set_handler(WFileDialog *d, WFileOkFn fn, void *userdata)
{
    if (!d)
        return;
    d->on_ok = fn;
    d->userdata = userdata;
}

void wfiledlg_show(WFileDialog *d, const char *dir_or_path)
{
    char probe[4];
    char suggested[WFILE_PATH_MAX];
    if (!d)
        return;
    suggested[0] = 0;
    if (dir_or_path && dir_or_path[0])
        strncpy_u(suggested, dir_or_path, sizeof(suggested));

    if (suggested[0] && sys_listdir(suggested, probe, sizeof(probe)) >= 0) {
        strncpy_u(d->path, suggested, sizeof(d->path));
        if (d->save_mode)
            join_path(d->path, "untitled.txt", d->pathbuf, sizeof(d->pathbuf));
        else
            strncpy_u(d->pathbuf, d->path, sizeof(d->pathbuf));
    } else if (suggested[0]) {
        strncpy_u(d->path, suggested, sizeof(d->path));
        go_parent(d->path, sizeof(d->path));
        if (sys_listdir(d->path, probe, sizeof(probe)) < 0)
            strncpy_u(d->path, "/sys", sizeof(d->path));
        strncpy_u(d->pathbuf, suggested, sizeof(d->pathbuf));
    } else {
        strncpy_u(d->path, "/sys", sizeof(d->path));
        if (d->save_mode)
            strncpy_u(d->pathbuf, "/sys/untitled.txt", sizeof(d->pathbuf));
        else
            strncpy_u(d->pathbuf, d->path, sizeof(d->pathbuf));
    }

    refresh(d);
    if (d->save_mode)
        wentry_set_text(&d->path_entry, d->pathbuf);
    d->visible = 1;
    d->frame.base.visible = 1;
    wentry_set_focused(&d->path_entry, 1);
}

void wfiledlg_hide(WFileDialog *d)
{
    if (!d)
        return;
    d->visible = 0;
    d->frame.base.visible = 0;
}

int wfiledlg_visible(const WFileDialog *d)
{
    return d && d->visible;
}

void wfiledlg_layout(WFileDialog *d, int ax, int ay, int aw, int ah)
{
    int dw = 320, dh = 240;
    int x, y;
    if (!d)
        return;
    x = ax + (aw - dw) / 2;
    y = ay + (ah - dh) / 2;
    if (x < ax)
        x = ax;
    if (y < ay)
        y = ay;
    widget_apply_box(wframe_widget(&d->frame), x, y, dw, dh);
}

void wfiledlg_draw(WFileDialog *d, GuiScreen *scr)
{
    if (!d || !d->visible || !scr)
        return;
    widget_draw(wframe_widget(&d->frame), scr);
}

void wfiledlg_input(WFileDialog *d, int mx, int my, int buttons)
{
    if (!d || !d->visible)
        return;
    wlist_input(&d->list, mx, my, buttons);
    wentry_input(&d->path_entry, mx, my, buttons);
    wbutton_input(&d->ok_btn, mx, my, buttons);
    wbutton_input(&d->cancel_btn, mx, my, buttons);
}

void wfiledlg_key(WFileDialog *d, int key)
{
    if (!d || !d->visible)
        return;
    if (key == 27) {
        wfiledlg_hide(d);
        return;
    }
    wlist_key(&d->list, key);
    wentry_key(&d->path_entry, key);
}
