#ifndef MP_AST_H
#define MP_AST_H

#include "config.h"
#include "lex.h"

typedef enum {
    ND_NONE = 0,
    ND_INT,
    ND_STR,
    ND_NAME,
    ND_BOOL,
    ND_UNARY,
    ND_BINOP,
    ND_COMPARE,
    ND_CALL,
    ND_ASSIGN,
    ND_EXPR_STMT,
    ND_PASS,
    ND_IF,
    ND_WHILE,
    ND_FOR,
    ND_FUNC,
    ND_RETURN,
    ND_LIST,
    ND_INDEX,
    ND_IMPORT,
    ND_BLOCK,
    ND_MODULE
} MpNodeKind;

typedef struct MpNode MpNode;

struct MpNode {
    MpNodeKind kind;
    MpTokenKind op;
    int32_t ival;
    char *sval;
    size_t slen;
    MpNode *a;
    MpNode *b;
    MpNode *c;
    MpNode *next;
    MpNode *body;
    MpNode *orelse;
};

#endif
