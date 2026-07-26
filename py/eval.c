#include "eval.h"
#include "heap.h"
#include "util.h"
#include "host_io.h"
#include "fs.h"
#include "lex.h"
#include "parse.h"

void mp_eval_init(MpEvalCtx *ctx)
{
    mp_memset(ctx, 0, sizeof(*ctx));
}

static void set_err(MpEvalCtx *ctx, const char *msg)
{
    mp_strncpy(ctx->err, msg, sizeof(ctx->err));
    ctx->has_err = 1;
}

static int name_eq(const char *a, size_t alen, const char *b)
{
    size_t i;
    for (i = 0; i < alen; i++) {
        if (!b[i] || a[i] != b[i])
            return 0;
    }
    return b[alen] == 0;
}

MpObject *mp_env_get(MpEvalCtx *ctx, const char *name, size_t len)
{
    size_t i;
    for (i = 0; i < MP_ENV_SLOTS; i++) {
        if (ctx->slots[i].used && name_eq(name, len, ctx->slots[i].name))
            return ctx->slots[i].value;
    }
    return NULL;
}

int mp_env_set(MpEvalCtx *ctx, const char *name, size_t len, MpObject *v)
{
    size_t i;
    int free_slot = -1;
    if (len >= sizeof(ctx->slots[0].name)) {
        set_err(ctx, "NameError: name too long");
        return 0;
    }
    for (i = 0; i < MP_ENV_SLOTS; i++) {
        if (ctx->slots[i].used && name_eq(name, len, ctx->slots[i].name)) {
            ctx->slots[i].value = v;
            return 1;
        }
        if (!ctx->slots[i].used && free_slot < 0)
            free_slot = (int)i;
    }
    if (free_slot < 0) {
        set_err(ctx, "RuntimeError: env full");
        return 0;
    }
    mp_memcpy(ctx->slots[free_slot].name, name, len);
    ctx->slots[free_slot].name[len] = 0;
    ctx->slots[free_slot].value = v;
    ctx->slots[free_slot].used = 1;
    return 1;
}

static void obj_to_buf(MpObject *o, char *buf, size_t n)
{
    if (!o || o->type == MP_T_NONE) {
        mp_strncpy(buf, "None", n);
        return;
    }
    if (o->type == MP_T_BOOL) {
        mp_strncpy(buf, o->v.i ? "True" : "False", n);
        return;
    }
    if (o->type == MP_T_STR) {
        mp_strncpy(buf, o->v.s.data ? o->v.s.data : "", n);
        return;
    }
    if (o->type == MP_T_LIST) {
        mp_strncpy(buf, "[list]", n);
        return;
    }
    if (o->type == MP_T_FUNC) {
        mp_strncpy(buf, "<function>", n);
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
        {
            size_t j = 0;
            if (neg && j + 1 < n)
                buf[j++] = '-';
            while (i > 0 && j + 1 < n)
                buf[j++] = tmp[--i];
            buf[j] = 0;
        }
        return;
    }
    mp_strncpy(buf, "?", n);
}

static void default_print(MpObject *o, int nl)
{
    char buf[256];
    obj_to_buf(o, buf, sizeof(buf));
    mp_write(buf);
    if (nl)
        mp_putc('\n');
}

static MpObject *eval_node(MpEvalCtx *ctx, MpNode *n);

static MpObject *eval_binop(MpEvalCtx *ctx, MpNode *n)
{
    if (n->op == TOK_AND) {
        MpObject *a = eval_node(ctx, n->a);
        if (ctx->has_err || ctx->returning) return a;
        if (!mp_is_truthy(a))
            return a;
        return eval_node(ctx, n->b);
    }
    if (n->op == TOK_OR) {
        MpObject *a = eval_node(ctx, n->a);
        if (ctx->has_err || ctx->returning) return a;
        if (mp_is_truthy(a))
            return a;
        return eval_node(ctx, n->b);
    }

    {
        MpObject *a = eval_node(ctx, n->a);
        MpObject *b;
        int32_t ai, bi;
        if (ctx->has_err || ctx->returning) return NULL;
        b = eval_node(ctx, n->b);
        if (ctx->has_err || ctx->returning) return NULL;

        if (n->op == TOK_PLUS && a->type == MP_T_STR && b->type == MP_T_STR) {
            size_t len = a->v.s.len + b->v.s.len;
            char *buf = (char *)mp_alloc(len + 1);
            if (!buf) {
                set_err(ctx, "MemoryError");
                return NULL;
            }
            mp_memcpy(buf, a->v.s.data, a->v.s.len);
            mp_memcpy(buf + a->v.s.len, b->v.s.data, b->v.s.len);
            buf[len] = 0;
            return mp_str_new(buf, len);
        }
        if (n->op == TOK_PLUS && a->type == MP_T_LIST && b->type == MP_T_LIST) {
            MpObject *out = mp_list_new();
            size_t i;
            if (!out) {
                set_err(ctx, "MemoryError");
                return NULL;
            }
            for (i = 0; i < a->v.list.len; i++)
                mp_list_append(out, a->v.list.items[i]);
            for (i = 0; i < b->v.list.len; i++)
                mp_list_append(out, b->v.list.items[i]);
            return out;
        }
        if (n->op == TOK_STAR && a->type == MP_T_STR && (b->type == MP_T_INT || b->type == MP_T_BOOL)) {
            size_t i, len;
            char *buf;
            if (b->v.i <= 0)
                return mp_str_new("", 0);
            len = a->v.s.len * (size_t)b->v.i;
            buf = (char *)mp_alloc(len + 1);
            if (!buf) {
                set_err(ctx, "MemoryError");
                return NULL;
            }
            for (i = 0; i < (size_t)b->v.i; i++)
                mp_memcpy(buf + i * a->v.s.len, a->v.s.data, a->v.s.len);
            buf[len] = 0;
            return mp_str_new(buf, len);
        }
        if (!mp_as_int(a, &ai) || !mp_as_int(b, &bi)) {
            set_err(ctx, "TypeError: unsupported operand types");
            return NULL;
        }
        switch (n->op) {
        case TOK_PLUS: return mp_int_new(ai + bi);
        case TOK_MINUS: return mp_int_new(ai - bi);
        case TOK_STAR: return mp_int_new(ai * bi);
        case TOK_SLASH:
            if (bi == 0) {
                set_err(ctx, "ZeroDivisionError");
                return NULL;
            }
            return mp_int_new(ai / bi);
        case TOK_PERCENT:
            if (bi == 0) {
                set_err(ctx, "ZeroDivisionError");
                return NULL;
            }
            return mp_int_new(ai % bi);
        default:
            set_err(ctx, "TypeError: bad binop");
            return NULL;
        }
    }
}

static MpObject *eval_compare(MpEvalCtx *ctx, MpNode *n)
{
    MpObject *a = eval_node(ctx, n->a);
    MpObject *b;
    int32_t ai, bi;
    int ok = 0;
    if (ctx->has_err || ctx->returning) return NULL;
    b = eval_node(ctx, n->b);
    if (ctx->has_err || ctx->returning) return NULL;
    if (a->type == MP_T_STR && b->type == MP_T_STR) {
        int cmp = mp_strcmp(a->v.s.data, b->v.s.data);
        switch (n->op) {
        case TOK_EQEQ: ok = cmp == 0; break;
        case TOK_NE: ok = cmp != 0; break;
        case TOK_LT: ok = cmp < 0; break;
        case TOK_GT: ok = cmp > 0; break;
        case TOK_LE: ok = cmp <= 0; break;
        case TOK_GE: ok = cmp >= 0; break;
        default: break;
        }
        return mp_bool_from(ok);
    }
    if (!mp_as_int(a, &ai) || !mp_as_int(b, &bi)) {
        if (n->op == TOK_EQEQ)
            return mp_bool_from(a == b);
        if (n->op == TOK_NE)
            return mp_bool_from(a != b);
        set_err(ctx, "TypeError: comparison");
        return NULL;
    }
    switch (n->op) {
    case TOK_EQEQ: ok = ai == bi; break;
    case TOK_NE: ok = ai != bi; break;
    case TOK_LT: ok = ai < bi; break;
    case TOK_GT: ok = ai > bi; break;
    case TOK_LE: ok = ai <= bi; break;
    case TOK_GE: ok = ai >= bi; break;
    default: break;
    }
    return mp_bool_from(ok);
}

static MpObject *call_user(MpEvalCtx *ctx, MpObject *fn, MpNode *args)
{
    MpNode *def = fn->v.fn.node;
    MpNode *param;
    MpNode *arg;
    MpObject *saved_vals[MP_FUNC_ARGS];
    char saved_names[MP_FUNC_ARGS][32];
    int saved_used[MP_FUNC_ARGS];
    int nparams = 0;
    int nargs = 0;
    int i;
    MpObject *result;

    for (param = def->a; param; param = param->next)
        nparams++;
    for (arg = args; arg; arg = arg->next)
        nargs++;
    if (nparams != nargs) {
        set_err(ctx, "TypeError: bad arity");
        return NULL;
    }
    if (nparams > MP_FUNC_ARGS) {
        set_err(ctx, "TypeError: too many params");
        return NULL;
    }

    param = def->a;
    arg = args;
    for (i = 0; i < nparams; i++) {
        MpObject *old = mp_env_get(ctx, param->sval, param->slen);
        MpObject *val = eval_node(ctx, arg);
        if (ctx->has_err) return NULL;
        saved_used[i] = old != NULL;
        if (old) {
            mp_strncpy(saved_names[i], param->sval, sizeof(saved_names[i]));
            saved_vals[i] = old;
        } else {
            mp_strncpy(saved_names[i], param->sval, sizeof(saved_names[i]));
            saved_vals[i] = NULL;
        }
        if (!mp_env_set(ctx, param->sval, param->slen, val))
            return NULL;
        param = param->next;
        arg = arg->next;
    }

    ctx->returning = 0;
    ctx->retval = Mp_None;
    result = eval_node(ctx, def->body);
    if (ctx->returning)
        result = ctx->retval;
    ctx->returning = 0;

    for (i = 0; i < nparams; i++) {
        if (saved_used[i])
            mp_env_set(ctx, saved_names[i], mp_strlen(saved_names[i]), saved_vals[i]);
        else {
            size_t j;
            for (j = 0; j < MP_ENV_SLOTS; j++) {
                if (ctx->slots[j].used && mp_strcmp(ctx->slots[j].name, saved_names[i]) == 0)
                    ctx->slots[j].used = 0;
            }
        }
    }
    return result ? result : Mp_None;
}

static MpObject *call_builtin(MpEvalCtx *ctx, const char *name, size_t nlen, MpNode *args)
{
    size_t argc = 0;
    MpNode *a;
    for (a = args; a; a = a->next)
        argc++;

    if (nlen == 5 && name[0] == 'p' && name[1] == 'r' && name[2] == 'i' && name[3] == 'n' && name[4] == 't') {
        void (*prn)(MpObject *, int) = ctx->print_fn ? ctx->print_fn : default_print;
        if (!args) {
            prn(mp_str_new_cstr(""), 1);
            return Mp_None;
        }
        for (a = args; a; a = a->next) {
            MpObject *v = eval_node(ctx, a);
            if (ctx->has_err) return NULL;
            prn(v, a->next ? 0 : 1);
            if (a->next)
                mp_putc(' ');
        }
        return Mp_None;
    }

    if (nlen == 5 && name[0] == 'r' && name[1] == 'a' && name[2] == 'n' && name[3] == 'g' && name[4] == 'e') {
        int32_t start = 0, stop, step = 1, x;
        MpObject *out;
        MpObject *a0;
        if (argc < 1 || argc > 3) {
            set_err(ctx, "TypeError: range() args");
            return NULL;
        }
        a0 = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (argc == 1) {
            if (!mp_as_int(a0, &stop)) {
                set_err(ctx, "TypeError: range()");
                return NULL;
            }
        } else {
            MpObject *a1 = eval_node(ctx, args->next);
            if (ctx->has_err) return NULL;
            if (!mp_as_int(a0, &start) || !mp_as_int(a1, &stop)) {
                set_err(ctx, "TypeError: range()");
                return NULL;
            }
            if (argc == 3) {
                MpObject *a2 = eval_node(ctx, args->next->next);
                if (ctx->has_err) return NULL;
                if (!mp_as_int(a2, &step) || step == 0) {
                    set_err(ctx, "ValueError: range step");
                    return NULL;
                }
            }
        }
        out = mp_list_new();
        if (!out) {
            set_err(ctx, "MemoryError");
            return NULL;
        }
        if (step > 0) {
            for (x = start; x < stop; x += step)
                mp_list_append(out, mp_int_new(x));
        } else {
            for (x = start; x > stop; x += step)
                mp_list_append(out, mp_int_new(x));
        }
        return out;
    }

    if (nlen == 6 && name[0] == 'a' && name[1] == 'p' && name[2] == 'p' && name[3] == 'e' &&
        name[4] == 'n' && name[5] == 'd') {
        MpObject *lst;
        MpObject *item;
        if (argc != 2) {
            set_err(ctx, "TypeError: append() takes 2 arguments");
            return NULL;
        }
        lst = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        item = eval_node(ctx, args->next);
        if (ctx->has_err) return NULL;
        if (lst->type != MP_T_LIST) {
            set_err(ctx, "TypeError: append() expects list");
            return NULL;
        }
        if (!mp_list_append(lst, item)) {
            set_err(ctx, "RuntimeError: list full");
            return NULL;
        }
        return Mp_None;
    }

    if (nlen == 3 && name[0] == 'l' && name[1] == 'e' && name[2] == 'n') {
        MpObject *v;
        if (argc != 1) {
            set_err(ctx, "TypeError: len() takes 1 argument");
            return NULL;
        }
        v = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (v->type == MP_T_STR)
            return mp_int_new((int32_t)v->v.s.len);
        if (v->type == MP_T_LIST)
            return mp_int_new((int32_t)v->v.list.len);
        set_err(ctx, "TypeError: object has no len()");
        return NULL;
    }

    if (nlen == 3 && name[0] == 'i' && name[1] == 'n' && name[2] == 't') {
        MpObject *v;
        if (argc != 1) {
            set_err(ctx, "TypeError: int() takes 1 argument");
            return NULL;
        }
        v = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (v->type == MP_T_INT || v->type == MP_T_BOOL)
            return mp_int_new(v->v.i);
        if (v->type == MP_T_STR) {
            int32_t x = 0, sign = 1;
            size_t i = 0;
            if (v->v.s.len && v->v.s.data[0] == '-') {
                sign = -1;
                i = 1;
            }
            if (i >= v->v.s.len) {
                set_err(ctx, "ValueError: invalid literal for int()");
                return NULL;
            }
            for (; i < v->v.s.len; i++) {
                char c = v->v.s.data[i];
                if (c < '0' || c > '9') {
                    set_err(ctx, "ValueError: invalid literal for int()");
                    return NULL;
                }
                x = x * 10 + (c - '0');
            }
            return mp_int_new(sign * x);
        }
        set_err(ctx, "TypeError: int() argument");
        return NULL;
    }

    if (nlen == 3 && name[0] == 's' && name[1] == 't' && name[2] == 'r') {
        MpObject *v;
        char buf[256];
        if (argc != 1) {
            set_err(ctx, "TypeError: str() takes 1 argument");
            return NULL;
        }
        v = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        obj_to_buf(v, buf, sizeof(buf));
        return mp_str_new_cstr(buf);
    }

    if (nlen == 4 && name[0] == 't' && name[1] == 'y' && name[2] == 'p' && name[3] == 'e') {
        MpObject *v;
        if (argc != 1) {
            set_err(ctx, "TypeError: type() takes 1 argument");
            return NULL;
        }
        v = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        return mp_str_new_cstr(mp_type_name(v));
    }

    if (nlen == 4 && name[0] == 'o' && name[1] == 'p' && name[2] == 'e' && name[3] == 'n') {
        const char *path;
        const char *mode = "r";
        int flags = 0;
        int fd;
        MpObject *pobj;
        if (argc < 1 || argc > 2) {
            set_err(ctx, "TypeError: open() takes 1 or 2 arguments");
            return NULL;
        }
        pobj = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (pobj->type != MP_T_STR) {
            set_err(ctx, "TypeError: path must be str");
            return NULL;
        }
        path = pobj->v.s.data;
        if (argc == 2) {
            MpObject *mobj = eval_node(ctx, args->next);
            if (ctx->has_err) return NULL;
            if (mobj->type != MP_T_STR) {
                set_err(ctx, "TypeError: mode must be str");
                return NULL;
            }
            mode = mobj->v.s.data;
        }
        if (mode[0] == 'r') {
            flags = MPFS_O_READ;
            if (mode[1] == '+')
                flags |= MPFS_O_WRITE;
        } else if (mode[0] == 'w') {
            flags = MPFS_O_WRITE | MPFS_O_CREATE | MPFS_O_TRUNC;
            if (mode[1] == '+')
                flags |= MPFS_O_READ;
        } else if (mode[0] == 'a') {
            flags = MPFS_O_WRITE | MPFS_O_CREATE | MPFS_O_APPEND;
            if (mode[1] == '+')
                flags |= MPFS_O_READ;
        } else {
            set_err(ctx, "ValueError: invalid mode");
            return NULL;
        }
        fd = mpfs_open(path, flags);
        if (fd < 0) {
            set_err(ctx, mpfs_strerror(fd));
            return NULL;
        }
        return mp_file_new(fd);
    }

    if (nlen == 4 && name[0] == 'r' && name[1] == 'e' && name[2] == 'a' && name[3] == 'd') {
        MpObject *f;
        char tmp[MPFS_FILE_MAX];
        int nread;
        int32_t want = -1;
        if (argc < 1 || argc > 2) {
            set_err(ctx, "TypeError: read()");
            return NULL;
        }
        f = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (f->type != MP_T_FILE) {
            set_err(ctx, "TypeError: read() expects file");
            return NULL;
        }
        if (argc == 2) {
            MpObject *nobj = eval_node(ctx, args->next);
            if (ctx->has_err) return NULL;
            if (!mp_as_int(nobj, &want)) {
                set_err(ctx, "TypeError: size must be int");
                return NULL;
            }
        }
        if (want < 0 || want > MPFS_FILE_MAX)
            want = MPFS_FILE_MAX;
        nread = mpfs_read(f->v.fd, tmp, (size_t)want);
        if (nread < 0) {
            set_err(ctx, mpfs_strerror(nread));
            return NULL;
        }
        return mp_str_new(tmp, (size_t)nread);
    }

    if (nlen == 5 && name[0] == 'w' && name[1] == 'r' && name[2] == 'i' && name[3] == 't' && name[4] == 'e') {
        MpObject *f;
        MpObject *data;
        int nw;
        if (argc != 2) {
            set_err(ctx, "TypeError: write()");
            return NULL;
        }
        f = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        data = eval_node(ctx, args->next);
        if (ctx->has_err) return NULL;
        if (f->type != MP_T_FILE || data->type != MP_T_STR) {
            set_err(ctx, "TypeError: write()");
            return NULL;
        }
        nw = mpfs_write(f->v.fd, data->v.s.data, data->v.s.len);
        if (nw < 0) {
            set_err(ctx, mpfs_strerror(nw));
            return NULL;
        }
        return mp_int_new(nw);
    }

    if (nlen == 5 && name[0] == 'c' && name[1] == 'l' && name[2] == 'o' && name[3] == 's' && name[4] == 'e') {
        MpObject *f;
        int rc;
        if (argc != 1) {
            set_err(ctx, "TypeError: close()");
            return NULL;
        }
        f = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (f->type != MP_T_FILE) {
            set_err(ctx, "TypeError: close()");
            return NULL;
        }
        rc = mpfs_close(f->v.fd);
        if (rc < 0) {
            set_err(ctx, mpfs_strerror(rc));
            return NULL;
        }
        f->v.fd = -1;
        return Mp_None;
    }

    if (nlen == 6 && name[0] == 'r' && name[1] == 'e' && name[2] == 'm' && name[3] == 'o' &&
        name[4] == 'v' && name[5] == 'e') {
        MpObject *p;
        int rc;
        if (argc != 1) {
            set_err(ctx, "TypeError: remove()");
            return NULL;
        }
        p = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (p->type != MP_T_STR) {
            set_err(ctx, "TypeError: remove()");
            return NULL;
        }
        rc = mpfs_unlink(p->v.s.data);
        if (rc < 0) {
            set_err(ctx, mpfs_strerror(rc));
            return NULL;
        }
        return Mp_None;
    }

    if (nlen == 6 && name[0] == 'e' && name[1] == 'x' && name[2] == 'i' && name[3] == 's' &&
        name[4] == 't' && name[5] == 's') {
        MpObject *p;
        if (argc != 1) {
            set_err(ctx, "TypeError: exists()");
            return NULL;
        }
        p = eval_node(ctx, args);
        if (ctx->has_err) return NULL;
        if (p->type != MP_T_STR) {
            set_err(ctx, "TypeError: exists()");
            return NULL;
        }
        return mp_bool_from(mpfs_exists(p->v.s.data));
    }

    if (nlen == 7 && name[0] == 'l' && name[1] == 'i' && name[2] == 's' && name[3] == 't' &&
        name[4] == 'd' && name[5] == 'i' && name[6] == 'r') {
        char buf[1024];
        int n;
        if (argc != 0) {
            set_err(ctx, "TypeError: listdir()");
            return NULL;
        }
        n = mpfs_listdir(buf, sizeof(buf));
        if (n < 0) {
            set_err(ctx, mpfs_strerror(n));
            return NULL;
        }
        return mp_str_new(buf, (size_t)n);
    }

    if (nlen == 6 && name[0] == 'f' && name[1] == 'o' && name[2] == 'r' && name[3] == 'm' &&
        name[4] == 'a' && name[5] == 't') {
        if (argc != 0) {
            set_err(ctx, "TypeError: format()");
            return NULL;
        }
        mpfs_format();
        return Mp_None;
    }

    if (nlen == 5 && name[0] == 'i' && name[1] == 'n' && name[2] == 'p' && name[3] == 'u' && name[4] == 't') {
        const char *prompt = "";
        char buf[MP_MAX_LINE];
        size_t i = 0;
        if (argc > 1) {
            set_err(ctx, "TypeError: input()");
            return NULL;
        }
        if (args) {
            MpObject *p = eval_node(ctx, args);
            if (ctx->has_err) return NULL;
            if (p->type != MP_T_STR) {
                set_err(ctx, "TypeError: input prompt");
                return NULL;
            }
            prompt = p->v.s.data;
        }
        if (ctx->input_fn)
            return ctx->input_fn(prompt);
        mp_write(prompt);
        for (;;) {
            int c = mp_getc();
            if (c == '\n' || c == '\r' || c == 0)
                break;
            if (c == '\b') {
                if (i > 0) {
                    i--;
                    mp_write("\b \b");
                }
                continue;
            }
            if (c >= 0x100 || c < 32)
                continue;
            if (i + 1 < sizeof(buf)) {
                buf[i++] = (char)c;
                mp_putc((char)c);
            }
        }
        buf[i] = 0;
        mp_putc('\n');
        return mp_str_new(buf, i);
    }

    set_err(ctx, "NameError: unknown function");
    return NULL;
}

static MpObject *eval_node(MpEvalCtx *ctx, MpNode *n)
{
    if (!n || ctx->has_err || ctx->returning)
        return ctx->retval;
    switch (n->kind) {
    case ND_NONE: return Mp_None;
    case ND_INT: return mp_int_new(n->ival);
    case ND_BOOL: return mp_bool_from(n->ival);
    case ND_STR: return mp_str_new(n->sval, n->slen);
    case ND_NAME: {
        MpObject *v = mp_env_get(ctx, n->sval, n->slen);
        if (!v) {
            set_err(ctx, "NameError: name is not defined");
            return NULL;
        }
        return v;
    }
    case ND_LIST: {
        MpObject *lst = mp_list_new();
        MpNode *el;
        if (!lst) {
            set_err(ctx, "MemoryError");
            return NULL;
        }
        for (el = n->a; el; el = el->next) {
            MpObject *v = eval_node(ctx, el);
            if (ctx->has_err) return NULL;
            mp_list_append(lst, v);
        }
        return lst;
    }
    case ND_INDEX: {
        MpObject *base = eval_node(ctx, n->a);
        MpObject *idx;
        int32_t i;
        if (ctx->has_err) return NULL;
        idx = eval_node(ctx, n->b);
        if (ctx->has_err) return NULL;
        if (!mp_as_int(idx, &i)) {
            set_err(ctx, "TypeError: indices must be int");
            return NULL;
        }
        if (base->type == MP_T_LIST) {
            MpObject *v = mp_list_get(base, i);
            if (!v) {
                set_err(ctx, "IndexError");
                return NULL;
            }
            return v;
        }
        if (base->type == MP_T_STR) {
            if (i < 0)
                i += (int32_t)base->v.s.len;
            if (i < 0 || (size_t)i >= base->v.s.len) {
                set_err(ctx, "IndexError");
                return NULL;
            }
            return mp_str_new(base->v.s.data + i, 1);
        }
        set_err(ctx, "TypeError: not subscriptable");
        return NULL;
    }
    case ND_UNARY: {
        MpObject *a = eval_node(ctx, n->a);
        int32_t x;
        if (ctx->has_err) return NULL;
        if (n->op == TOK_NOT)
            return mp_bool_from(!mp_is_truthy(a));
        if (!mp_as_int(a, &x)) {
            set_err(ctx, "TypeError: bad unary");
            return NULL;
        }
        if (n->op == TOK_PLUS)
            return mp_int_new(x);
        if (n->op == TOK_MINUS)
            return mp_int_new(-x);
        set_err(ctx, "TypeError: bad unary");
        return NULL;
    }
    case ND_BINOP: return eval_binop(ctx, n);
    case ND_COMPARE: return eval_compare(ctx, n);
    case ND_CALL: {
        if (!n->a || n->a->kind != ND_NAME) {
            set_err(ctx, "TypeError: not callable");
            return NULL;
        }
        {
            MpObject *fn = mp_env_get(ctx, n->a->sval, n->a->slen);
            if (fn && fn->type == MP_T_FUNC)
                return call_user(ctx, fn, n->b);
        }
        return call_builtin(ctx, n->a->sval, n->a->slen, n->b);
    }
    case ND_FUNC: {
        MpObject *fn = mp_func_new(n);
        if (!fn) {
            set_err(ctx, "MemoryError");
            return NULL;
        }
        if (!mp_env_set(ctx, n->sval, n->slen, fn))
            return NULL;
        return fn;
    }
    case ND_RETURN:
        ctx->retval = n->a ? eval_node(ctx, n->a) : Mp_None;
        if (ctx->has_err) return NULL;
        ctx->returning = 1;
        return ctx->retval;
    case ND_ASSIGN: {
        MpObject *v = eval_node(ctx, n->b);
        if (ctx->has_err) return NULL;
        if (!mp_env_set(ctx, n->a->sval, n->a->slen, v))
            return NULL;
        return v;
    }
    case ND_EXPR_STMT: {
        MpObject *v = eval_node(ctx, n->a);
        if (ctx->has_err || ctx->returning) return v;
        if (n->a && n->a->kind != ND_CALL && n->a->kind != ND_ASSIGN)
            return v;
        return Mp_None;
    }
    case ND_PASS: return Mp_None;
    case ND_IF: {
        MpObject *cond = eval_node(ctx, n->a);
        if (ctx->has_err || ctx->returning) return cond;
        if (mp_is_truthy(cond))
            return eval_node(ctx, n->body);
        if (n->orelse)
            return eval_node(ctx, n->orelse);
        return Mp_None;
    }
    case ND_WHILE: {
        int guard = 0;
        while (guard++ < 100000) {
            MpObject *cond = eval_node(ctx, n->a);
            if (ctx->has_err || ctx->returning) return cond;
            if (!mp_is_truthy(cond))
                break;
            eval_node(ctx, n->body);
            if (ctx->has_err || ctx->returning) return ctx->retval;
        }
        return Mp_None;
    }
    case ND_FOR: {
        MpObject *iter = eval_node(ctx, n->a);
        size_t i;
        if (ctx->has_err || ctx->returning) return iter;
        if (iter->type == MP_T_LIST) {
            for (i = 0; i < iter->v.list.len; i++) {
                if (!mp_env_set(ctx, n->sval, n->slen, iter->v.list.items[i]))
                    return NULL;
                eval_node(ctx, n->body);
                if (ctx->has_err || ctx->returning) return ctx->retval;
            }
            return Mp_None;
        }
        if (iter->type == MP_T_STR) {
            for (i = 0; i < iter->v.s.len; i++) {
                MpObject *ch = mp_str_new(iter->v.s.data + i, 1);
                if (!mp_env_set(ctx, n->sval, n->slen, ch))
                    return NULL;
                eval_node(ctx, n->body);
                if (ctx->has_err || ctx->returning) return ctx->retval;
            }
            return Mp_None;
        }
        set_err(ctx, "TypeError: for expects list/str");
        return NULL;
    }
    case ND_IMPORT: {
        char path[40];
        char src[MPFS_FILE_MAX];
        int fd, nread;
        MpLexer lx;
        MpParser ps;
        MpNode *mod;
        size_t i = 0;
        while (i < n->slen && i + 4 < sizeof(path)) {
            path[i] = n->sval[i];
            i++;
        }
        path[i++] = '.';
        path[i++] = 'p';
        path[i++] = 'y';
        path[i] = 0;
        fd = mpfs_open(path, MPFS_O_READ);
        if (fd < 0) {
            set_err(ctx, "ImportError: module not found");
            return NULL;
        }
        nread = mpfs_read(fd, src, sizeof(src) - 1);
        mpfs_close(fd);
        if (nread < 0) {
            set_err(ctx, "ImportError: read failed");
            return NULL;
        }
        src[nread] = 0;
        if (!mp_lex(&lx, src)) {
            set_err(ctx, lx.err);
            return NULL;
        }
        mod = mp_parse(&ps, &lx);
        if (!mod) {
            set_err(ctx, ps.err);
            return NULL;
        }
        eval_node(ctx, mod);
        return Mp_None;
    }
    case ND_BLOCK:
    case ND_MODULE: {
        MpObject *last = Mp_None;
        MpNode *s;
        for (s = n->body; s; s = s->next) {
            last = eval_node(ctx, s);
            if (ctx->has_err || ctx->returning)
                return ctx->returning ? ctx->retval : last;
        }
        return last;
    }
    default:
        set_err(ctx, "RuntimeError: unknown node");
        return NULL;
    }
}

MpObject *mp_eval_expr(MpEvalCtx *ctx, MpNode *n)
{
    ctx->has_err = 0;
    ctx->err[0] = 0;
    ctx->returning = 0;
    return eval_node(ctx, n);
}

MpObject *mp_eval_module(MpEvalCtx *ctx, MpNode *mod)
{
    ctx->has_err = 0;
    ctx->err[0] = 0;
    ctx->returning = 0;
    return eval_node(ctx, mod);
}
