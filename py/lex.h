#ifndef MP_LEX_H
#define MP_LEX_H

#include "config.h"

typedef enum {
    TOK_EOF = 0,
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_DEDENT,
    TOK_NAME,
    TOK_NUMBER,
    TOK_STRING,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_EQEQ,
    TOK_NE,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COLON,
    TOK_COMMA,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_IF,
    TOK_ELIF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_IN,
    TOK_DEF,
    TOK_RETURN,
    TOK_IMPORT,
    TOK_PASS,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NONE,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_ERROR
} MpTokenKind;

typedef struct {
    MpTokenKind kind;
    const char *start;
    size_t len;
    int32_t number;
    int line;
    int col;
} MpToken;

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    int line;
    int col;
    int at_line_start;
    int indent_stack[32];
    int indent_top;
    int pending_dedents;
    MpToken tokens[MP_MAX_TOKENS];
    size_t ntokens;
    char err[128];
} MpLexer;

int mp_lex(MpLexer *lx, const char *src);
const char *mp_tok_name(MpTokenKind k);

#endif
