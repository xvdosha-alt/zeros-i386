#ifndef MP_PARSE_H
#define MP_PARSE_H

#include "ast.h"
#include "lex.h"

typedef struct {
    MpLexer *lx;
    size_t pos;
    char err[128];
} MpParser;

MpNode *mp_parse(MpParser *p, MpLexer *lx);

#endif
