#include "runtime.h"
#include "heap.h"
#include "lex.h"
#include "parse.h"
#include "object.h"
#include "util.h"
#include "fs.h"

void mp_runtime_init(MpRuntime *rt)
{
    mp_heap_init();
    mp_objects_init();
    mpfs_format();
    mp_eval_init(&rt->eval);
    rt->err[0] = 0;
}

int mp_runtime_exec(MpRuntime *rt, const char *src, MpObject **result_out)
{
    static MpLexer lx;
    MpParser ps;
    MpNode *mod;
    MpObject *res;

    if (result_out)
        *result_out = Mp_None;
    rt->err[0] = 0;

    if (!mp_lex(&lx, src)) {
        mp_strncpy(rt->err, "SyntaxError: ", sizeof(rt->err));
        size_t n = mp_strlen(rt->err);
        mp_strncpy(rt->err + n, lx.err, sizeof(rt->err) - n);
        return 0;
    }

    mod = mp_parse(&ps, &lx);
    if (!mod) {
        mp_strncpy(rt->err, "SyntaxError: ", sizeof(rt->err));
        size_t n = mp_strlen(rt->err);
        mp_strncpy(rt->err + n, ps.err, sizeof(rt->err) - n);
        return 0;
    }

    res = mp_eval_module(&rt->eval, mod);
    if (rt->eval.has_err) {
        mp_strncpy(rt->err, rt->eval.err, sizeof(rt->err));
        return 0;
    }
    if (result_out)
        *result_out = res;
    return 1;
}
