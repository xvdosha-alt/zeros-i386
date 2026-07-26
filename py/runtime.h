#ifndef MP_RUNTIME_H
#define MP_RUNTIME_H

#include "eval.h"

typedef struct {
    MpEvalCtx eval;
    char err[128];
} MpRuntime;

void mp_runtime_init(MpRuntime *rt);
int mp_runtime_exec(MpRuntime *rt, const char *src, MpObject **result_out);

#endif
