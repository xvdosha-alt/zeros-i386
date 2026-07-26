#include "libmp.h"

#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103
#define KEY_HOME 0x104
#define KEY_END 0x105
#define KEY_DELETE 0x106
#define KEY_META 0x200
#define HIST_MAX 64
#define LINE_MAX 256
#define ARG_MAX 16
#define O_READ 1
#define O_WRITE 2
#define O_TRUNC 8
#define O_CREATE 16

static char line[LINE_MAX];
static char cwd[128] = "/";
static char oldpwd[128] = "/";
static char hist[HIST_MAX][LINE_MAX];
static int hist_n;
static int hist_base;
static char yank[LINE_MAX];
static int tab_armed;
static int draw_w;

static const char *builtins[] = {
    "help", "cd", "exit", "history", 0
};
static const char *bins[] = {
    "cat", "nano", "mvim", "pkg", "python", "ping", "httpd", "dns", "msh",
    "curl", "wget", "nc", "ssh", "sshd", "ifconfig", "telnet",
    "cp", "rm", "mv", "ls", "tree", "touch", "mkdir", "head", "tail",
    "which", "uname", "echo", "clear", "cls", "pwd", "true", "false", "date",
    "ps", "kill",
    "desktop", "run", "df", "du", "free",
    "jobs", "fg", "bg", "pkill", "top", "htop", "alias", 0
};

static void put_str(const char *s)
{
    sys_write(1, s, strlen_u(s));
}

static int hosted_repaint(int shown, int len, int cursor, int prompt_len)
{
    int i;
    char sync[2];
    int c;
    for (i = 0; i < shown; i++)
        sys_write(1, "\b \b", 3);
    if (len > 0)
        sys_write(1, line, len);
    c = prompt_len + cursor;
    if (c < 0)
        c = 0;
    if (c > 255)
        c = 255;
    sync[0] = 6;
    sync[1] = (char)c;
    sys_write(1, sync, 2);
    return len;
}

static int hosted_commit_echo(int shown, int len)
{
    int i;
    for (i = 0; i < shown; i++)
        sys_write(1, "\b \b", 3);
    if (len > 0)
        sys_write(1, line, len);
    return len;
}

static void put_int(int v)
{
    char tmp[16];
    int i = 0, n = 0, neg = 0;
    if (v < 0) {
        neg = 1;
        v = -v;
    }
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0 && i < 15) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (neg)
        sys_write(1, "-", 1);
    while (i > 0)
        sys_write(1, &tmp[--i], 1);
    (void)n;
}

static void redraw(const char *prompt, int len, int cursor)
{
    int i;
    int plen = strlen_u(prompt);
    int need = plen + len;
    int clear = draw_w > need ? draw_w : need;
    if (clear < need)
        clear = need;
    if (clear > 78)
        clear = 78;
    put_str("\r");
    for (i = 0; i < clear; i++)
        sys_write(1, " ", 1);
    put_str("\r");
    put_str(prompt);
    if (len > 0)
        sys_write(1, line, len);
    draw_w = need;
    put_str("\r");
    put_str(prompt);
    if (cursor > 0)
        sys_write(1, line, cursor);
}

static void hist_add(const char *s)
{
    int i;
    if (!s[0])
        return;
    if (hist_n > 0 && !strcmp_u(hist[hist_n - 1], s))
        return;
    if (hist_n < HIST_MAX) {
        strncpy_u(hist[hist_n++], s, LINE_MAX);
        return;
    }
    for (i = 1; i < HIST_MAX; i++)
        strncpy_u(hist[i - 1], hist[i], LINE_MAX);
    strncpy_u(hist[HIST_MAX - 1], s, LINE_MAX);
    hist_base++;
}

static int match_prefix(const char *name, const char *pref, int plen)
{
    int i;
    for (i = 0; i < plen; i++) {
        if (!name[i] || name[i] != pref[i])
            return 0;
    }
    return 1;
}

static int path_is_dir(const char *path)
{
    char buf[4];
    return sys_listdir(path, buf, sizeof(buf)) >= 0;
}

static void join_path(char *out, int outn, const char *dir, const char *name)
{
    int n;
    if (!strcmp_u(dir, "/")) {
        out[0] = '/';
        strncpy_u(out + 1, name, outn - 1);
        return;
    }
    if (!strcmp_u(dir, ".")) {
        strncpy_u(out, name, outn);
        return;
    }
    strncpy_u(out, dir, outn);
    n = strlen_u(out);
    if (n + 1 < outn)
        out[n++] = '/';
    strncpy_u(out + n, name, outn - n);
}

static int try_complete_list(const char **list, const char *pref, int plen, char *out, int outn, int force_list)
{
    const char *one = 0;
    int count = 0;
    int i;
    for (i = 0; list[i]; i++) {
        if (match_prefix(list[i], pref, plen)) {
            count++;
            one = list[i];
        }
    }
    if (count == 1 && one && !force_list) {
        strncpy_u(out, one, outn);
        return 1;
    }
    if (count > 1 || (force_list && count >= 1)) {
        println("");
        for (i = 0; list[i]; i++) {
            if (match_prefix(list[i], pref, plen)) {
                put_str(list[i]);
                put_str("  ");
            }
        }
        println("");
        return 2;
    }
    return 0;
}

static int try_complete_path(const char *pref, int plen, char *out, int outn, int dirs_only, int force_list)
{
    char dirpath[128];
    char base[64];
    char list[512];
    char names[32][48];
    char full[160];
    int nn = 0, count = 0, slash = -1, i, j, n, blen;
    const char *one = 0;

    for (i = 0; i < plen; i++)
        if (pref[i] == '/')
            slash = i;
    if (slash < 0) {
        strncpy_u(dirpath, ".", sizeof(dirpath));
        if (plen >= (int)sizeof(base))
            return 0;
        for (i = 0; i < plen; i++)
            base[i] = pref[i];
        base[plen] = 0;
    } else {
        if (slash == 0)
            strncpy_u(dirpath, "/", sizeof(dirpath));
        else {
            if (slash + 1 >= (int)sizeof(dirpath))
                return 0;
            for (i = 0; i < slash; i++)
                dirpath[i] = pref[i];
            dirpath[slash] = 0;
        }
        blen = plen - slash - 1;
        if (blen < 0 || blen >= (int)sizeof(base))
            return 0;
        for (i = 0; i < blen; i++)
            base[i] = pref[slash + 1 + i];
        base[blen] = 0;
    }
    blen = strlen_u(base);
    n = sys_listdir(dirpath, list, sizeof(list));
    if (n < 0)
        return 0;
    i = 0;
    while (list[i] && nn < 32) {
        j = 0;
        while (list[i] && list[i] != '\n' && j + 1 < (int)sizeof(names[0]))
            names[nn][j++] = list[i++];
        names[nn][j] = 0;
        if (list[i] == '\n')
            i++;
        if (!names[nn][0] || !match_prefix(names[nn], base, blen))
            continue;
        join_path(full, sizeof(full), dirpath, names[nn]);
        if (dirs_only && !path_is_dir(full))
            continue;
        one = names[nn];
        count++;
        nn++;
    }
    if (count == 1 && one && !force_list) {
        int pos = 0;
        if (slash >= 0) {
            for (i = 0; i <= slash && pos + 1 < outn; i++)
                out[pos++] = pref[i];
            out[pos] = 0;
        } else
            out[0] = 0;
        n = strlen_u(out);
        strncpy_u(out + n, one, outn - n);
        if (dirs_only || path_is_dir(out)) {
            n = strlen_u(out);
            if (n + 1 < outn) {
                out[n] = '/';
                out[n + 1] = 0;
            }
        }
        return 1;
    }
    if (count > 1 || (force_list && count >= 1)) {
        println("");
        for (i = 0; i < nn; i++) {
            put_str(names[i]);
            put_str("  ");
        }
        println("");
        return 2;
    }
    return 0;
}

static int first_word_is(const char *name, int arg_start)
{
    char cmd[32];
    int i = 0, j = 0;
    while (i < arg_start && line[i] == ' ')
        i++;
    while (i < arg_start && line[i] != ' ' && j + 1 < (int)sizeof(cmd))
        cmd[j++] = line[i++];
    cmd[j] = 0;
    return !strcmp_u(cmd, name);
}

static int do_tab(int *len, int *cursor, int force_list)
{
    char pref[96];
    char out[96];
    int start = *cursor;
    int i, r = 0, plen;
    while (start > 0 && line[start - 1] != ' ')
        start--;
    plen = *cursor - start;
    if (plen < 0 || plen >= (int)sizeof(pref))
        return 0;
    for (i = 0; i < plen; i++)
        pref[i] = line[start + i];
    pref[plen] = 0;
    out[0] = 0;
    if (start == 0) {
        if (plen > 0) {
            r = try_complete_list(builtins, pref, plen, out, sizeof(out), force_list);
            if (r == 0)
                r = try_complete_list(bins, pref, plen, out, sizeof(out), force_list);
        }
        if (r == 0)
            r = try_complete_path(pref, plen, out, sizeof(out), 0, force_list);
    } else if (first_word_is("cd", start)) {
        r = try_complete_path(pref, plen, out, sizeof(out), 1, force_list);
    } else {
        r = try_complete_path(pref, plen, out, sizeof(out), 0, force_list);
        if (r == 0 && plen > 0)
            r = try_complete_list(bins, pref, plen, out, sizeof(out), force_list);
    }
    if (r == 1) {
        int olen = strlen_u(out);
        int need = olen - plen;
        if (need < 0 || *len + need + 1 >= LINE_MAX)
            return 0;
        for (i = *len; i >= *cursor; i--)
            line[i + need] = line[i];
        for (i = 0; i < olen; i++)
            line[start + i] = out[i];
        *len += need;
        *cursor = start + olen;
        line[*len] = 0;
        tab_armed = 0;
        return 1;
    }
    if (r == 2)
        tab_armed = 0;
    else if (!force_list)
        tab_armed = 1;
    return r;
}

static int word_left(int cursor)
{
    int i = cursor;
    if (i > 0)
        i--;
    while (i > 0 && line[i] == ' ')
        i--;
    while (i > 0 && line[i - 1] != ' ')
        i--;
    return i;
}

static int word_right(int cursor, int len)
{
    int i = cursor;
    while (i < len && line[i] != ' ')
        i++;
    while (i < len && line[i] == ' ')
        i++;
    return i;
}

static void delete_range(int *len, int *cursor, int from, int to)
{
    int i, n;
    if (from < 0)
        from = 0;
    if (to > *len)
        to = *len;
    if (to <= from)
        return;
    n = to - from;
    if (n >= (int)sizeof(yank))
        n = (int)sizeof(yank) - 1;
    for (i = 0; i < n; i++)
        yank[i] = line[from + i];
    yank[n] = 0;
    for (i = from; i + (to - from) <= *len; i++)
        line[i] = line[i + (to - from)];
    *len -= (to - from);
    *cursor = from;
    line[*len] = 0;
}

static int hist_expand(char *buf, int buflen)
{
    char tmp[LINE_MAX];
    int i;
    if (!buf[0])
        return 0;
    if (!strcmp_u(buf, "!!")) {
        if (hist_n <= 0)
            return -1;
        strncpy_u(buf, hist[hist_n - 1], buflen);
        return 1;
    }
    if (buf[0] == '!' && buf[1] >= '0' && buf[1] <= '9') {
        int num = atoi_u(buf + 1);
        int idx = num - 1 - hist_base;
        if (idx < 0 || idx >= hist_n)
            return -1;
        strncpy_u(buf, hist[idx], buflen);
        return 1;
    }
    if (buf[0] == '!' && buf[1]) {
        for (i = hist_n - 1; i >= 0; i--) {
            if (match_prefix(hist[i], buf + 1, strlen_u(buf + 1))) {
                strncpy_u(buf, hist[i], buflen);
                return 1;
            }
        }
        return -1;
    }
    if (buf[0] == '^') {
        char *a = buf + 1;
        char *b = a;
        while (*b && *b != '^')
            b++;
        if (!*b)
            return -1;
        *b++ = 0;
        if (hist_n <= 0)
            return -1;
        strncpy_u(tmp, hist[hist_n - 1], sizeof(tmp));
        {
            char *p = tmp;
            char out[LINE_MAX];
            int oi = 0, al = strlen_u(a), bl = strlen_u(b), done = 0;
            while (*p && oi + 1 < LINE_MAX) {
                if (!done && match_prefix(p, a, al)) {
                    for (i = 0; i < bl && oi + 1 < LINE_MAX; i++)
                        out[oi++] = b[i];
                    p += al;
                    done = 1;
                } else
                    out[oi++] = *p++;
            }
            out[oi] = 0;
            if (!done)
                return -1;
            strncpy_u(buf, out, buflen);
            return 1;
        }
    }
    return 0;
}

static void ensure_fresh_line(void)
{
    int pos, col;
    if (sys_cons_hosted())
        return;
    pos = sys_ioctl(3, 0, 0);
    col = pos & 0xFFFF;
    if (col > 0)
        sys_write(1, "\n", 1);
}

static void after_cmd(void)
{
    
    sys_write(1, "\n", 1);
}

static int readline(const char *prompt)
{
    int len = 0, cursor = 0, hist_pos = -1, key, rev = 0;
    char rquery[64];
    int rq = 0;
    line[0] = 0;
    yank[0] = 0;
    draw_w = 0;
    tab_armed = 0;
    ensure_fresh_line();
    put_str(prompt);
    draw_w = strlen_u(prompt);

    if (sys_cons_hosted()) {
        int plen = strlen_u(prompt);
        int shown = hosted_repaint(0, 0, 0, plen);

        for (;;) {
            key = 0;
            if (sys_read(0, &key, 4) != 4)
                continue;
            if (key == 3) {
                println("^C");
                line[0] = 0;
                return -1;
            }
            if (key == '\n' || key == '\r') {
                hosted_commit_echo(shown, len);
                sys_write(1, "\n", 1);
                line[len] = 0;
                return len;
            }
            if (key == '\b' || key == 127) {
                if (cursor > 0) {
                    int i;
                    for (i = cursor - 1; i < len; i++)
                        line[i] = line[i + 1];
                    len--;
                    cursor--;
                    line[len] = 0;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            if (key == KEY_LEFT) {
                if (cursor > 0) {
                    cursor--;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            if (key == KEY_RIGHT) {
                if (cursor < len) {
                    cursor++;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            if (key == KEY_HOME) {
                if (cursor != 0) {
                    cursor = 0;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            if (key == KEY_END) {
                if (cursor != len) {
                    cursor = len;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            if (key == KEY_DELETE) {
                if (cursor < len) {
                    int i;
                    for (i = cursor; i < len; i++)
                        line[i] = line[i + 1];
                    len--;
                    line[len] = 0;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            if (key == KEY_UP) {
                if (hist_n > 0) {
                    if (hist_pos < 0)
                        hist_pos = hist_n - 1;
                    else if (hist_pos > 0)
                        hist_pos--;
                    strncpy_u(line, hist[hist_pos], LINE_MAX);
                    len = strlen_u(line);
                    cursor = len;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            if (key == KEY_DOWN) {
                if (hist_pos >= 0) {
                    if (hist_pos + 1 >= hist_n) {
                        hist_pos = -1;
                        line[0] = 0;
                        len = cursor = 0;
                    } else {
                        hist_pos++;
                        strncpy_u(line, hist[hist_pos], LINE_MAX);
                        len = strlen_u(line);
                        cursor = len;
                    }
                } else {
                    line[0] = 0;
                    len = cursor = 0;
                }
                shown = hosted_repaint(shown, len, cursor, plen);
                continue;
            }
            if (key == '\t') {
                int r = do_tab(&len, &cursor, tab_armed);
                if (r == 2) {
                    put_str(prompt);
                    shown = hosted_repaint(0, len, cursor, plen);
                } else if (r == 1) {
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
                continue;
            }
            tab_armed = 0;
            if (key >= 32 && key < 256 && len + 1 < LINE_MAX) {
                char ch = (char)key;
                if (cursor < len) {
                    int i;
                    for (i = len; i >= cursor; i--)
                        line[i + 1] = line[i];
                    line[cursor] = ch;
                    len++;
                    cursor++;
                    line[len] = 0;
                    shown = hosted_repaint(shown, len, cursor, plen);
                } else {
                    line[len++] = ch;
                    line[len] = 0;
                    cursor = len;
                    shown = hosted_repaint(shown, len, cursor, plen);
                }
            }
        }
    }

    for (;;) {
        key = 0;
        if (sys_read(0, &key, 4) != 4)
            continue;
        if (rev) {
            if (key == 3 || key == 7) {
                rev = 0;
                println("^C");
                line[0] = 0;
                return -1;
            }
            if (key == '\n' || key == '\r') {
                rev = 0;
                sys_write(1, "\n", 1);
                return len;
            }
            if (key == '\b' || key == 127) {
                if (rq > 0) {
                    rquery[--rq] = 0;
                }
            } else if (key >= 32 && key < 256 && rq + 1 < (int)sizeof(rquery)) {
                rquery[rq++] = (char)key;
                rquery[rq] = 0;
            } else if (key != 18)
                continue;
            {
                int i, found = 0;
                for (i = hist_n - 1; i >= 0; i--) {
                    const char *h = hist[i];
                    int j, ok = 0;
                    if (!rq) {
                        ok = 1;
                    } else {
                        for (j = 0; h[j]; j++)
                            if (match_prefix(h + j, rquery, rq)) {
                                ok = 1;
                                break;
                            }
                    }
                    if (ok) {
                        strncpy_u(line, h, LINE_MAX);
                        len = strlen_u(line);
                        cursor = len;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    line[0] = 0;
                    len = cursor = 0;
                }
                {
                    char rp[96];
                    strncpy_u(rp, "(reverse-i-search)`", sizeof(rp));
                    strncpy_u(rp + strlen_u(rp), rquery, (int)sizeof(rp) - strlen_u(rp));
                    strncpy_u(rp + strlen_u(rp), "': ", (int)sizeof(rp) - strlen_u(rp));
                    redraw(rp, len, cursor);
                }
            }
            continue;
        }
        if (key == 18) {
            rev = 1;
            rq = 0;
            rquery[0] = 0;
            redraw("(reverse-i-search)`': ", len, cursor);
            continue;
        }
        if (key == 3) {
            println("^C");
            line[0] = 0;
            return -1;
        }
        if (key == 4) {
            if (len == 0)
                return -2;
            continue;
        }
        if (key == 12) {
            sys_ioctl(1, 0, 0);
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == '\n' || key == '\r') {
            sys_write(1, "\n", 1);
            line[len] = 0;
            tab_armed = 0;
            return len;
        }
        if (key == '\t') {
            int r = do_tab(&len, &cursor, tab_armed);
            if (r)
                redraw(prompt, len, cursor);
            continue;
        }
        tab_armed = 0;
        if (key == KEY_HOME || key == 1) {
            cursor = 0;
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == KEY_END || key == 5) {
            cursor = len;
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == KEY_LEFT) {
            if (cursor > 0) {
                cursor--;
                redraw(prompt, len, cursor);
            }
            continue;
        }
        if (key == KEY_RIGHT) {
            if (cursor < len) {
                cursor++;
                redraw(prompt, len, cursor);
            }
            continue;
        }
        if (key == (KEY_META | 'b')) {
            cursor = word_left(cursor);
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == (KEY_META | 'f')) {
            cursor = word_right(cursor, len);
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == (KEY_META | 'd')) {
            delete_range(&len, &cursor, cursor, word_right(cursor, len));
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == 23) {
            int from = word_left(cursor);
            delete_range(&len, &cursor, from, cursor);
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == 21) {
            delete_range(&len, &cursor, 0, cursor);
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == 11) {
            delete_range(&len, &cursor, cursor, len);
            redraw(prompt, len, cursor);
            continue;
        }
        if (key == 25) {
            int yl = strlen_u(yank), i;
            if (len + yl + 1 < LINE_MAX) {
                for (i = len; i >= cursor; i--)
                    line[i + yl] = line[i];
                for (i = 0; i < yl; i++)
                    line[cursor + i] = yank[i];
                len += yl;
                cursor += yl;
                line[len] = 0;
                redraw(prompt, len, cursor);
            }
            continue;
        }
        if (key == KEY_UP) {
            if (hist_n > 0) {
                if (hist_pos < 0)
                    hist_pos = hist_n - 1;
                else if (hist_pos > 0)
                    hist_pos--;
                strncpy_u(line, hist[hist_pos], LINE_MAX);
                len = strlen_u(line);
                cursor = len;
                redraw(prompt, len, cursor);
            }
            continue;
        }
        if (key == KEY_DOWN) {
            if (hist_pos >= 0) {
                if (hist_pos + 1 >= hist_n) {
                    hist_pos = -1;
                    line[0] = 0;
                    len = cursor = 0;
                } else {
                    hist_pos++;
                    strncpy_u(line, hist[hist_pos], LINE_MAX);
                    len = strlen_u(line);
                    cursor = len;
                }
                redraw(prompt, len, cursor);
            }
            continue;
        }
        if (key == KEY_DELETE) {
            if (cursor < len) {
                int i;
                for (i = cursor; i < len; i++)
                    line[i] = line[i + 1];
                len--;
                line[len] = 0;
                redraw(prompt, len, cursor);
            }
            continue;
        }
        if (key == '\b' || key == 127) {
            if (cursor > 0) {
                int i;
                for (i = cursor - 1; i < len; i++)
                    line[i] = line[i + 1];
                len--;
                cursor--;
                line[len] = 0;
                redraw(prompt, len, cursor);
            }
            continue;
        }
        if (key >= 32 && key < 256 && len + 1 < LINE_MAX) {
            char ch = (char)key;
            int i;
            for (i = len; i > cursor; i--)
                line[i] = line[i - 1];
            line[cursor] = ch;
            len++;
            cursor++;
            line[len] = 0;
            redraw(prompt, len, cursor);
        }
    }
}

static int split(char *s, char **argv, int max)
{
    int argc = 0;
    while (*s && argc < max) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        argv[argc++] = s;
        while (*s && *s != ' ')
            s++;
        if (*s)
            *s++ = 0;
    }
    return argc;
}

static const char *home_dir(void)
{
    return "/sys";
}

static void ensure_home(void)
{
    /* Default workspace is /sys (initrd); nothing to create. */
}

static void cmd_help(void)
{
    println("builtins: cd exit history help");
    println("bins:     ls tree pwd echo clear/cls touch mkdir head tail which uname date");
    println("          cat cp rm mv nano mvim pkg python true false");
    println("          ps kill desktop run df du free");
    println("shell:    history !! !n !cmd ^a^b");
    println("keys:     Tab/TabTab  arrows  Ctrl+A/E/W/U/K/Y/L/R/C/D");
    println("          Alt+B/F/D  Home/End/Delete");
    println("net:      curl wget nc ssh sshd telnet ifconfig ping httpd dns");
    println("disk:     /disk is FAT; make fat-reset wipes it");
    println("root:     / lists mounts - /sys (initrd) and /disk (ATA FAT)");
}

static void cmd_history(void)
{
    int i;
    for (i = 0; i < hist_n; i++) {
        put_int(hist_base + i + 1);
        put_str("  ");
        println(hist[i]);
    }
}

static int find_pipe(int argc, char **argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        if (argv[i] && argv[i][0] == '|' && !argv[i][1])
            return i;
    }
    return -1;
}

static void spawn_cmd(int argc, char **argv)
{
    char path[128];
    char args[128];
    int pid, st = 0, afd, i, pos = 0;
    args[0] = 0;
    for (i = 1; i < argc; i++) {
        int L = strlen_u(argv[i]);
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
    if (argv[0][0] == '/')
        strncpy_u(path, argv[0], sizeof(path));
    else {
        strncpy_u(path, "/sys/bin/", sizeof(path));
        strncpy_u(path + 9, argv[0], (int)sizeof(path) - 9);
    }
    pid = sys_spawn(path);
    if (pid == -2) {
        println("spawn: out of memory (image swap)");
        after_cmd();
        return;
    }
    if (pid < 0) {
        print("not found: ");
        println(argv[0]);
        after_cmd();
        return;
    }
    {
        int child = pid;
        do {
            st = 0;
            pid = sys_wait(child, &st);
            /* Under gterm: don't busy-spin - yield so the host can paint
             * and feed keys to the nested child. */
            if (pid == -2 && sys_cons_hosted())
                sys_yield();
        } while (pid == -2);
    }
    after_cmd();
}

static void spawn_pipeline(int argc, char **argv)
{
    int pipe_at = find_pipe(argc, argv);
    int i, left_argc, right_argc;
    char *left[ARG_MAX];
    char *right[ARG_MAX];
    char path[128];
    char args[128];
    int pid, st, afd, pos;
    int fds[2];

    if (pipe_at < 1 || pipe_at + 1 >= argc) {
        spawn_pipeline(argc, argv);
        return;
    }
    left_argc = right_argc = 0;
    for (i = 0; i < pipe_at && left_argc < ARG_MAX; i++)
        left[left_argc++] = argv[i];
    for (i = pipe_at + 1; i < argc && right_argc < ARG_MAX; i++)
        right[right_argc++] = argv[i];
    if (left_argc < 1 || right_argc < 1 || sys_pipe(fds) != 0) {
        if (left_argc >= 1)
            spawn_cmd(argc, argv);
        else
            after_cmd();
        return;
    }

    pos = 0;
    args[0] = 0;
    for (i = 1; i < left_argc; i++) {
        int L = strlen_u(left[i]);
        if (pos + L + 2 >= (int)sizeof(args))
            break;
        if (pos)
            args[pos++] = ' ';
        memcpy_u(args + pos, left[i], L);
        pos += L;
        args[pos] = 0;
    }
    afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
    if (afd >= 0) {
        sys_write(afd, args, strlen_u(args));
        sys_close(afd);
    }
    if (left[0][0] == '/')
        strncpy_u(path, left[0], sizeof(path));
    else {
        strncpy_u(path, "/sys/bin/", sizeof(path));
        strncpy_u(path + 9, left[0], (int)sizeof(path) - 9);
    }
    sys_dup2_spawn(-1, fds[1]);
    pid = sys_spawn(path);
    sys_close(fds[1]);
    if (pid > 0) {
        int child = pid;
        do {
            st = 0;
            pid = sys_wait(child, &st);
            if (pid == -2 && sys_cons_hosted())
                sys_yield();
        } while (pid == -2);
    }

    pos = 0;
    args[0] = 0;
    for (i = 1; i < right_argc; i++) {
        int L = strlen_u(right[i]);
        if (pos + L + 2 >= (int)sizeof(args))
            break;
        if (pos)
            args[pos++] = ' ';
        memcpy_u(args + pos, right[i], L);
        pos += L;
        args[pos] = 0;
    }
    afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
    if (afd >= 0) {
        sys_write(afd, args, strlen_u(args));
        sys_close(afd);
    }
    if (right[0][0] == '/')
        strncpy_u(path, right[0], sizeof(path));
    else {
        strncpy_u(path, "/sys/bin/", sizeof(path));
        strncpy_u(path + 9, right[0], (int)sizeof(path) - 9);
    }
    sys_dup2_spawn(fds[0], -1);
    pid = sys_spawn(path);
    sys_close(fds[0]);
    if (pid > 0) {
        int child = pid;
        do {
            st = 0;
            pid = sys_wait(child, &st);
            if (pid == -2 && sys_cons_hosted())
                sys_yield();
        } while (pid == -2);
    }
    after_cmd();
}


int main(void)
{
    char *argv[ARG_MAX];
    char work[LINE_MAX];
    int argc, n, i;
    ensure_home();
    sys_chdir(home_dir());
    sys_getcwd(cwd, sizeof(cwd));
    println("msh - zerOS shell  (help for keys & commands)");

    for (;;) {
        char prompt[160];
        strncpy_u(prompt, cwd, (int)sizeof(prompt) - 3);
        i = strlen_u(prompt);
        prompt[i++] = '>';
        prompt[i++] = ' ';
        prompt[i] = 0;
        n = readline(prompt);
        if (n == -2) {
            println("exit");
            return 0;
        }
        if (n < 0)
            continue;
        strncpy_u(work, line, sizeof(work));
        {
            int he = hist_expand(work, sizeof(work));
            if (he < 0) {
                println("history: event not found");
                continue;
            }
            if (he > 0) {
                put_str(work);
                println("");
            }
        }
        if (work[0])
            hist_add(work);
        argc = split(work, argv, ARG_MAX);
        if (!argc)
            continue;
        if (!strcmp_u(argv[0], "exit"))
            return 0;
        if (!strcmp_u(argv[0], "help")) {
            cmd_help();
            continue;
        }
        if (!strcmp_u(argv[0], "history")) {
            cmd_history();
            continue;
        }
        if (!strcmp_u(argv[0], "cd")) {
            const char *target = home_dir();
            char prev[128];
            sys_getcwd(prev, sizeof(prev));
            if (argc >= 2) {
                if (!strcmp_u(argv[1], "-"))
                    target = oldpwd;
                else if (!strcmp_u(argv[1], "~") || !strcmp_u(argv[1], "~/"))
                    target = home_dir();
                else
                    target = argv[1];
            }
            ensure_home();
            if (sys_chdir(target) == 0) {
                strncpy_u(oldpwd, prev, sizeof(oldpwd));
                sys_getcwd(cwd, sizeof(cwd));
            } else
                println("cd: fail");
            continue;
        }
        spawn_cmd(argc, argv);
        sys_getcwd(cwd, sizeof(cwd));
    }
    return 0;
}
