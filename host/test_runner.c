#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "../py/runtime.h"
#include "../py/object.h"
#include "../py/host_io.h"
#include "../py/fs.h"
#include "../py/seed.h"

enum { MAX_CASES = 128, CODE_MAX = 4096, OUT_MAX = 8192, NAME_MAX = 64 };

static char capture[OUT_MAX];
static size_t capture_len;

static void capture_reset(void)
{
    capture_len = 0;
    capture[0] = 0;
}

static void capture_add(const char *s)
{
    size_t n = strlen(s);
    if (capture_len + n + 1 >= sizeof(capture))
        return;
    memcpy(capture + capture_len, s, n);
    capture_len += n;
    capture[capture_len] = 0;
}

static void test_print(MpObject *o, int nl)
{
    char buf[256];
    if (!o || o->type == MP_T_NONE)
        capture_add("None");
    else if (o->type == MP_T_BOOL)
        capture_add(o->v.i ? "True" : "False");
    else if (o->type == MP_T_STR)
        capture_add(o->v.s.data);
    else if (o->type == MP_T_INT) {
        snprintf(buf, sizeof(buf), "%d", o->v.i);
        capture_add(buf);
    } else if (o->type == MP_T_LIST)
        capture_add("list");
    else
        capture_add("?");
    if (nl)
        capture_add("\n");
}

static void normalize_nl(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = 0;
    if (n + 1 < OUT_MAX) {
        s[n++] = '\n';
        s[n] = 0;
    }
}

static void trim_trailing_space_lines(char *s)
{
    char *d = s;
    char *p = s;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n')
            p++;
        {
            char *e = p;
            while (e > line && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
                e--;
            while (line < e)
                *d++ = *line++;
        }
        if (*p == '\n') {
            *d++ = '\n';
            p++;
        }
    }
    *d = 0;
}

typedef struct {
    char name[NAME_MAX];
    char code[CODE_MAX];
    char expected[OUT_MAX];
    int is_error;
    int is_seed;
} Case;

static int run_exec_case(Case *c)
{
    MpRuntime rt;
    MpObject *result = NULL;
    char out[OUT_MAX];
    char exp[OUT_MAX];

    mpfs_format();
    mp_runtime_init(&rt);
    rt.eval.print_fn = test_print;
    capture_reset();
    out[0] = 0;

    if (!mp_runtime_exec(&rt, c->code, &result)) {
        snprintf(out, sizeof(out), "%s\n", rt.err);
        if (c->is_error) {
            if (strstr(out, c->expected) != NULL)
                return 1;
            fprintf(stderr, "FAIL %s (error)\n--- code ---\n%s\n--- expected substring ---\n%s\n--- got ---\n%s\n",
                    c->name, c->code, c->expected, out);
            return 0;
        }
        fprintf(stderr, "FAIL %s\n--- code ---\n%s\n--- expected ---\n%s\n--- got error ---\n%s\n",
                c->name, c->code, c->expected, out);
        return 0;
    }

    if (c->is_error) {
        fprintf(stderr, "FAIL %s: expected error containing '%s'\n", c->name, c->expected);
        return 0;
    }

    if (capture_len)
        snprintf(out, sizeof(out), "%s", capture);
    else if (result && result != Mp_None) {
        if (result->type == MP_T_INT)
            snprintf(out, sizeof(out), "%d\n", result->v.i);
        else if (result->type == MP_T_BOOL)
            snprintf(out, sizeof(out), "%s\n", result->v.i ? "True" : "False");
        else if (result->type == MP_T_STR)
            snprintf(out, sizeof(out), "%s\n", result->v.s.data);
        else
            out[0] = 0;
    } else
        out[0] = 0;

    strncpy(exp, c->expected, sizeof(exp) - 1);
    exp[sizeof(exp) - 1] = 0;
    normalize_nl(out);
    normalize_nl(exp);
    if (strcmp(out, exp) != 0) {
        fprintf(stderr, "FAIL %s\n--- code ---\n%s\n--- expected ---\n%s\n--- got ---\n%s\n",
                c->name, c->code, exp, out);
        return 0;
    }
    return 1;
}

static int run_seed_case(void)
{
    MpRuntime rt;

    mpfs_format();
    mp_runtime_init(&rt);
    rt.eval.print_fn = test_print;
    capture_reset();
    mp_seed_stdlib();
    if (!mp_try_run_main(&rt)) {
        fprintf(stderr, "FAIL seed: %s\n", rt.err);
        return 0;
    }
    if (strstr(capture, "zero-overhead boot") == NULL ||
        strstr(capture, "hi minipy") == NULL) {
        fprintf(stderr, "FAIL seed output:\n%s\n", capture);
        return 0;
    }
    return 1;
}

static void rstrip_cr(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static char *skip_ws(char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r')
        p++;
    return p;
}

static int starts_header(const char *line)
{
    return line[0] == '#' && line[1] == '#' && line[2] == '#';
}

static int parse_file(const char *path, Case *cases, int max)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long sz;
    char *p;
    int n = 0;

    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 512 * 1024) {
        fclose(f);
        return -1;
    }
    data = (char *)malloc((size_t)sz + 1);
    if (!data) {
        fclose(f);
        return -1;
    }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);
    data[sz] = 0;
    p = data;

    while (*p && n < max) {
        char *line_start = p;
        char *line_end;
        size_t llen;
        char linebuf[512];

        while (*p && *p != '\n')
            p++;
        line_end = p;
        if (*p == '\n')
            p++;
        llen = (size_t)(line_end - line_start);
        if (llen >= sizeof(linebuf))
            llen = sizeof(linebuf) - 1;
        memcpy(linebuf, line_start, llen);
        linebuf[llen] = 0;
        rstrip_cr(linebuf);

        {
            char *line = skip_ws(linebuf);
            if (!line[0] || (line[0] == '#' && !starts_header(line)))
                continue;
            if (!starts_header(line))
                continue;

            {
                Case *c = &cases[n];
                char *name;
                int mode = 0;
                memset(c, 0, sizeof(*c));
                name = skip_ws(line + 3);
                if (name[0] == '!') {
                    c->is_error = 1;
                    name = skip_ws(name + 1);
                } else if (name[0] == '@') {
                    name = skip_ws(name + 1);
                    if (strncmp(name, "seed", 4) == 0) {
                        c->is_seed = 1;
                        strncpy(c->name, "seed", NAME_MAX - 1);
                        n++;
                        continue;
                    }
                }
                strncpy(c->name, name, NAME_MAX - 1);

                while (*p) {
                    char *L0 = p;
                    char *Le;
                    char Lbuf[CODE_MAX];
                    size_t Llen;
                    while (*p && *p != '\n')
                        p++;
                    Le = p;
                    if (*p == '\n')
                        p++;
                    Llen = (size_t)(Le - L0);
                    if (Llen >= sizeof(Lbuf))
                        Llen = sizeof(Lbuf) - 1;
                    memcpy(Lbuf, L0, Llen);
                    Lbuf[Llen] = 0;
                    rstrip_cr(Lbuf);

                    if (starts_header(Lbuf)) {
                        p = L0;
                        break;
                    }
                    if (strcmp(Lbuf, "---") == 0) {
                        mode = 1;
                        continue;
                    }
                    if (mode == 0) {
                        if (strlen(c->code) + Llen + 2 < CODE_MAX) {
                            strcat(c->code, Lbuf);
                            strcat(c->code, "\n");
                        }
                    } else {
                        if (strlen(c->expected) + Llen + 2 < OUT_MAX) {
                            strcat(c->expected, Lbuf);
                            strcat(c->expected, "\n");
                        }
                    }
                }
                trim_trailing_space_lines(c->code);
                trim_trailing_space_lines(c->expected);
                if (c->is_error) {
                    size_t el = strlen(c->expected);
                    while (el > 0 && c->expected[el - 1] == '\n')
                        c->expected[--el] = 0;
                }
                if (!c->is_seed && !c->code[0]) {
                    fprintf(stderr, "empty code in case '%s'\n", c->name);
                    free(data);
                    return -1;
                }
                n++;
            }
        }
    }
    free(data);
    return n;
}

int main(int argc, char **argv)
{
    Case cases[MAX_CASES];
    const char *path = "tests/cases.txt";
    int n, i, ok = 0;

    if (argc >= 2)
        path = argv[1];

    mp_io_init();
    n = parse_file(path, cases, MAX_CASES);
    if (n < 0)
        return 2;
    if (n == 0) {
        fprintf(stderr, "no cases in %s\n", path);
        return 2;
    }

    printf("minipy tests: %s (%d cases)\n", path, n);
    for (i = 0; i < n; i++) {
        int pass;
        if (cases[i].is_seed)
            pass = run_seed_case();
        else
            pass = run_exec_case(&cases[i]);
        if (pass) {
            ok++;
            printf("  ok  %s\n", cases[i].name);
        } else
            printf("  FAIL %s\n", cases[i].name);
    }
    printf("%d/%d passed\n", ok, n);
    return ok == n ? 0 : 1;
}
