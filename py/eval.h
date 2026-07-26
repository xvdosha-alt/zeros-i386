#ifndef MP_EVAL_H
#define MP_EVAL_H

#include "ast.h"
#include "object.h"

typedef struct {
    char name[32];
    MpObject *value;
    int used;
} MpEnvSlot;

typedef struct {
    MpEnvSlot slots[MP_ENV_SLOTS];
    char err[128];
    int has_err;
    int returning;
    MpObject *retval;
    MpObject *(*input_fn)(const char *prompt);
    void (*print_fn)(MpObject *o, int nl);
} MpEvalCtx;

void mp_eval_init(MpEvalCtx *ctx);
MpObject *mp_eval_module(MpEvalCtx *ctx, MpNode *mod);
MpObject *mp_eval_expr(MpEvalCtx *ctx, MpNode *n);
int mp_env_set(MpEvalCtx *ctx, const char *name, size_t len, MpObject *v);
MpObject *mp_env_get(MpEvalCtx *ctx, const char *name, size_t len);

#endif
