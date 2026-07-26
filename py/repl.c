#include "repl.h"
#include "host_io.h"
#include "object.h"
#include "util.h"
#include "lex.h"
#include "parse.h"
#include "ast.h"
#include "seed.h"

#ifndef MP_HOST
#include "../kernel/drivers/kbd.h"
#else
enum {
    KBD_KEY_UP = 0x100,
    KBD_KEY_DOWN,
    KBD_KEY_LEFT,
    KBD_KEY_RIGHT
};
#endif

static int count_open(const char *s)
{
    int paren = 0;
    int quote = 0;
    char q = 0;
    int i;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (quote) {
            if (c == '\\' && s[i + 1]) {
                i++;
                continue;
            }
            if (c == q)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = 1;
            q = c;
            continue;
        }
        if (c == '(')
            paren++;
        else if (c == ')')
            paren--;
    }
    return paren > 0 || quote;
}

static int ends_with_colon_line(const char *s)
{
    size_t n = mp_strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r'))
        n--;
    return n > 0 && s[n - 1] == ':';
}

int mp_buffer_complete(const char *src)
{
    if (count_open(src))
        return 0;
    if (ends_with_colon_line(src))
        return 0;
    {
        const char *p = src;
        int last_indent = 0;
        int line_indent = 0;
        int at_start = 1;
        int saw_block = 0;
        while (*p) {
            if (at_start) {
                line_indent = 0;
                while (*p == ' ') {
                    line_indent++;
                    p++;
                }
                at_start = 0;
                if (*p == '\n') {
                    p++;
                    at_start = 1;
                    continue;
                }
            }
            if (*p == '\n') {
                if (line_indent > 0)
                    saw_block = 1;
                last_indent = line_indent;
                p++;
                at_start = 1;
                continue;
            }
            p++;
        }
        if (saw_block && last_indent > 0)
            return 0;
    }
    return 1;
}

static void print_result(MpObject *o)
{
    char buf[256];
    if (!o || o == Mp_None)
        return;
    if (o->type == MP_T_STR) {
        mp_putc('\'');
        mp_write(o->v.s.data);
        mp_putc('\'');
        mp_putc('\n');
        return;
    }
    if (o->type == MP_T_BOOL) {
        mp_writeln(o->v.i ? "True" : "False");
        return;
    }
    if (o->type == MP_T_INT) {
        int32_t v = o->v.i;
        char tmp[16];
        int i = 0, neg = 0;
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
            mp_putc('-');
        while (i > 0)
            mp_putc(tmp[--i]);
        mp_putc('\n');
        return;
    }
    (void)buf;
}

static void redraw_line(const char *prompt, const char *buf, size_t len, size_t cursor)
{
    size_t i;
    size_t need = mp_strlen(prompt) + len;
    if (need > 78)
        need = 78;
    mp_write("\r");
    for (i = 0; i < need; i++)
        mp_putc(' ');
    mp_write("\r");
    mp_write(prompt);
    for (i = 0; i < len; i++)
        mp_putc(buf[i]);
    mp_write("\r");
    mp_write(prompt);
    for (i = 0; i < cursor; i++)
        mp_putc(buf[i]);
}

static void hosted_erase(int n)
{
    int i;
    for (i = 0; i < n; i++)
        mp_write("\b \b");
}

static int hosted_paint(int shown, const char *buf, size_t len, size_t cursor,
                        size_t prompt_len)
{
    size_t i;
    char sync[2];
    int c;
    hosted_erase(shown);
    for (i = 0; i < len; i++)
        mp_putc(buf[i]);
    c = (int)(prompt_len + cursor);
    if (c < 0)
        c = 0;
    if (c > 255)
        c = 255;
    sync[0] = 6;
    sync[1] = (char)c;
    mp_putc(sync[0]);
    mp_putc(sync[1]);
    return (int)len;
}

static void hosted_commit(int shown, const char *buf, size_t len)
{
    size_t i;
    hosted_erase(shown);
    for (i = 0; i < len; i++)
        mp_putc(buf[i]);
}

void mp_repl_run(void)
{
    static MpRuntime rt;
    static char line[MP_MAX_LINE];
    static char src[MP_MAX_LINE * 8];
    static char history[MP_HISTORY][MP_MAX_LINE];
    int hist_count = 0;
    int hist_pos = -1;
    size_t len = 0;
    size_t cursor = 0;
    int continuation = 0;
    int hosted = mp_cons_hosted();

    mp_runtime_init(&rt);
    mp_seed_stdlib();
    mp_writeln("minipy - zerOS");
    mp_writeln(">>> ready");

    src[0] = 0;
    for (;;) {
        const char *prompt = continuation ? "... " : ">>> ";
        size_t plen = mp_strlen(prompt);
        int shown = 0;
        len = 0;
        cursor = 0;
        line[0] = 0;
        hist_pos = -1;
#ifndef MP_HOST
        if (!hosted) {
            int pos = 0;
            __asm__ volatile ("int $0x80" : "=a"(pos) : "a"(18), "b"(3), "c"(0), "d"(0) : "memory");
            if ((pos & 0xFFFF) > 0)
                mp_putc('\n');
        }
#endif
        mp_write(prompt);
        if (hosted)
            shown = hosted_paint(0, line, 0, 0, plen);

        for (;;) {
            int c = mp_getc();
            if (c < 0)
                continue;
            if (c == '\n' || c == '\r') {
                if (hosted)
                    hosted_commit(shown, line, len);
                mp_putc('\n');
                break;
            }
            if (c == KBD_KEY_LEFT) {
                if (cursor > 0) {
                    cursor--;
                    if (hosted)
                        shown = hosted_paint(shown, line, len, cursor, plen);
                    else
                        redraw_line(prompt, line, len, cursor);
                }
                continue;
            }
            if (c == KBD_KEY_RIGHT) {
                if (cursor < len) {
                    cursor++;
                    if (hosted)
                        shown = hosted_paint(shown, line, len, cursor, plen);
                    else
                        redraw_line(prompt, line, len, cursor);
                }
                continue;
            }
            if (c == KBD_KEY_UP) {
                if (hist_count > 0) {
                    if (hist_pos < 0)
                        hist_pos = hist_count - 1;
                    else if (hist_pos > 0)
                        hist_pos--;
                    mp_strncpy(line, history[hist_pos], sizeof(line));
                    len = mp_strlen(line);
                    cursor = len;
                    if (hosted)
                        shown = hosted_paint(shown, line, len, cursor, plen);
                    else
                        redraw_line(prompt, line, len, cursor);
                }
                continue;
            }
            if (c == KBD_KEY_DOWN) {
                if (hist_pos >= 0) {
                    if (hist_pos + 1 >= hist_count) {
                        hist_pos = -1;
                        line[0] = 0;
                        len = 0;
                        cursor = 0;
                    } else {
                        hist_pos++;
                        mp_strncpy(line, history[hist_pos], sizeof(line));
                        len = mp_strlen(line);
                        cursor = len;
                    }
                    if (hosted)
                        shown = hosted_paint(shown, line, len, cursor, plen);
                    else
                        redraw_line(prompt, line, len, cursor);
                }
                continue;
            }
            if (c == '\b') {
                if (cursor > 0) {
                    size_t i;
                    for (i = cursor - 1; i < len; i++)
                        line[i] = line[i + 1];
                    len--;
                    cursor--;
                    line[len] = 0;
                    if (hosted)
                        shown = hosted_paint(shown, line, len, cursor, plen);
                    else
                        redraw_line(prompt, line, len, cursor);
                }
                continue;
            }
            if (c >= 0x100)
                continue;
            if (c < 32)
                continue;
            if (len + 1 >= sizeof(line))
                continue;
            {
                size_t i;
                for (i = len; i > cursor; i--)
                    line[i] = line[i - 1];
                line[cursor] = (char)c;
                len++;
                cursor++;
                line[len] = 0;
                if (hosted)
                    shown = hosted_paint(shown, line, len, cursor, plen);
                else
                    redraw_line(prompt, line, len, cursor);
            }
        }

        if (hist_count < MP_HISTORY) {
            mp_strncpy(history[hist_count++], line, MP_MAX_LINE);
        } else {
            int i;
            for (i = 1; i < MP_HISTORY; i++)
                mp_strncpy(history[i - 1], history[i], MP_MAX_LINE);
            mp_strncpy(history[MP_HISTORY - 1], line, MP_MAX_LINE);
        }

        if (!continuation) {
            mp_strncpy(src, line, sizeof(src));
            if (src[0]) {
                size_t n = mp_strlen(src);
                if (n + 1 < sizeof(src)) {
                    src[n] = '\n';
                    src[n + 1] = 0;
                }
            }
        } else {
            size_t n = mp_strlen(src);
            size_t m = mp_strlen(line);
            if (n + m + 2 < sizeof(src)) {
                mp_memcpy(src + n, line, m);
                src[n + m] = '\n';
                src[n + m + 1] = 0;
            }
        }

        if (!continuation && line[0] == 0)
            continue;

        if (!mp_buffer_complete(src)) {
            continuation = 1;
            continue;
        }
        continuation = 0;

        {
            MpObject *result = NULL;
            if (!mp_runtime_exec(&rt, src, &result)) {
                mp_writeln(rt.err);
            } else if (result && result != Mp_None) {
                MpLexer lx;
                MpParser ps;
                if (mp_lex(&lx, src)) {
                    MpNode *mod = mp_parse(&ps, &lx);
                    if (mod && mod->body && !mod->body->next &&
                        mod->body->kind == ND_EXPR_STMT &&
                        mod->body->a && mod->body->a->kind != ND_CALL) {
                        print_result(result);
                    }
                }
            }
        }
        src[0] = 0;
    }
}
