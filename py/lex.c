#include "lex.h"
#include "util.h"

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_alnum(char c)
{
    return is_alpha(c) || is_digit(c);
}

static char peek(MpLexer *lx)
{
    if (lx->pos >= lx->len)
        return 0;
    return lx->src[lx->pos];
}

static char advance(MpLexer *lx)
{
    char c = peek(lx);
    if (!c)
        return 0;
    lx->pos++;
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

static int push_tok(MpLexer *lx, MpTokenKind kind, const char *start, size_t len, int32_t number)
{
    if (lx->ntokens >= MP_MAX_TOKENS) {
        mp_strncpy(lx->err, "too many tokens", sizeof(lx->err));
        return 0;
    }
    MpToken *t = &lx->tokens[lx->ntokens++];
    t->kind = kind;
    t->start = start;
    t->len = len;
    t->number = number;
    t->line = lx->line;
    t->col = lx->col;
    return 1;
}

static MpTokenKind keyword(const char *s, size_t n)
{
    if (n == 2 && s[0] == 'i' && s[1] == 'f') return TOK_IF;
    if (n == 2 && s[0] == 'i' && s[1] == 'n') return TOK_IN;
    if (n == 3 && s[0] == 'a' && s[1] == 'n' && s[2] == 'd') return TOK_AND;
    if (n == 2 && s[0] == 'o' && s[1] == 'r') return TOK_OR;
    if (n == 3 && s[0] == 'n' && s[1] == 'o' && s[2] == 't') return TOK_NOT;
    if (n == 3 && s[0] == 'd' && s[1] == 'e' && s[2] == 'f') return TOK_DEF;
    if (n == 3 && s[0] == 'f' && s[1] == 'o' && s[2] == 'r') return TOK_FOR;
    if (n == 4 && s[0] == 'e' && s[1] == 'l' && s[2] == 'i' && s[3] == 'f') return TOK_ELIF;
    if (n == 4 && s[0] == 'e' && s[1] == 'l' && s[2] == 's' && s[3] == 'e') return TOK_ELSE;
    if (n == 4 && s[0] == 'T' && s[1] == 'r' && s[2] == 'u' && s[3] == 'e') return TOK_TRUE;
    if (n == 4 && s[0] == 'N' && s[1] == 'o' && s[2] == 'n' && s[3] == 'e') return TOK_NONE;
    if (n == 4 && s[0] == 'p' && s[1] == 'a' && s[2] == 's' && s[3] == 's') return TOK_PASS;
    if (n == 5 && s[0] == 'F' && s[1] == 'a' && s[2] == 'l' && s[3] == 's' && s[4] == 'e') return TOK_FALSE;
    if (n == 5 && s[0] == 'w' && s[1] == 'h' && s[2] == 'i' && s[3] == 'l' && s[4] == 'e') return TOK_WHILE;
    if (n == 6 && s[0] == 'r' && s[1] == 'e' && s[2] == 't' && s[3] == 'u' && s[4] == 'r' && s[5] == 'n') return TOK_RETURN;
    if (n == 6 && s[0] == 'i' && s[1] == 'm' && s[2] == 'p' && s[3] == 'o' && s[4] == 'r' && s[5] == 't') return TOK_IMPORT;
    return TOK_NAME;
}

static int emit_indent(MpLexer *lx)
{
    int spaces = 0;
    while (peek(lx) == ' ') {
        advance(lx);
        spaces++;
    }
    if (peek(lx) == '\n' || peek(lx) == '#' || peek(lx) == 0)
        return 1;
    if (peek(lx) == '\t') {
        mp_strncpy(lx->err, "tabs not allowed", sizeof(lx->err));
        return 0;
    }
    int cur = lx->indent_stack[lx->indent_top];
    if (spaces == cur)
        return 1;
    if (spaces > cur) {
        if (lx->indent_top + 1 >= 32) {
            mp_strncpy(lx->err, "indent too deep", sizeof(lx->err));
            return 0;
        }
        lx->indent_stack[++lx->indent_top] = spaces;
        return push_tok(lx, TOK_INDENT, lx->src + lx->pos, 0, 0);
    }
    while (lx->indent_top > 0 && lx->indent_stack[lx->indent_top] > spaces) {
        lx->indent_top--;
        if (!push_tok(lx, TOK_DEDENT, lx->src + lx->pos, 0, 0))
            return 0;
    }
    if (lx->indent_stack[lx->indent_top] != spaces) {
        mp_strncpy(lx->err, "inconsistent indent", sizeof(lx->err));
        return 0;
    }
    return 1;
}

int mp_lex(MpLexer *lx, const char *src)
{
    mp_memset(lx, 0, sizeof(*lx));
    lx->src = src;
    lx->len = mp_strlen(src);
    lx->line = 1;
    lx->col = 1;
    lx->at_line_start = 1;
    lx->indent_stack[0] = 0;
    lx->indent_top = 0;

    while (peek(lx) || lx->pending_dedents) {
        if (lx->pending_dedents > 0) {
            lx->pending_dedents--;
            if (!push_tok(lx, TOK_DEDENT, lx->src + lx->pos, 0, 0))
                return 0;
            continue;
        }

        if (lx->at_line_start) {
            lx->at_line_start = 0;
            if (!emit_indent(lx))
                return 0;
            if (!peek(lx))
                break;
        }

        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lx);
            continue;
        }
        if (c == '#') {
            while (peek(lx) && peek(lx) != '\n')
                advance(lx);
            continue;
        }
        if (c == '\n') {
            advance(lx);
            if (!push_tok(lx, TOK_NEWLINE, lx->src + lx->pos - 1, 1, 0))
                return 0;
            lx->at_line_start = 1;
            continue;
        }

        const char *start = lx->src + lx->pos;
        if (is_alpha(c)) {
            while (is_alnum(peek(lx)))
                advance(lx);
            size_t n = (size_t)((lx->src + lx->pos) - start);
            MpTokenKind k = keyword(start, n);
            if (!push_tok(lx, k, start, n, 0))
                return 0;
            continue;
        }
        if (is_digit(c)) {
            int32_t v = 0;
            while (is_digit(peek(lx))) {
                char d = advance(lx);
                v = v * 10 + (d - '0');
            }
            if (!push_tok(lx, TOK_NUMBER, start, (size_t)((lx->src + lx->pos) - start), v))
                return 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = advance(lx);
            start = lx->src + lx->pos;
            while (peek(lx) && peek(lx) != q) {
                if (peek(lx) == '\\') {
                    advance(lx);
                    if (!peek(lx))
                        break;
                }
                if (peek(lx) == '\n') {
                    mp_strncpy(lx->err, "unterminated string", sizeof(lx->err));
                    return 0;
                }
                advance(lx);
            }
            if (peek(lx) != q) {
                mp_strncpy(lx->err, "unterminated string", sizeof(lx->err));
                return 0;
            }
            size_t n = (size_t)((lx->src + lx->pos) - start);
            advance(lx);
            if (!push_tok(lx, TOK_STRING, start, n, 0))
                return 0;
            continue;
        }

        advance(lx);
        if (c == '+') { push_tok(lx, TOK_PLUS, start, 1, 0); continue; }
        if (c == '-') { push_tok(lx, TOK_MINUS, start, 1, 0); continue; }
        if (c == '*') { push_tok(lx, TOK_STAR, start, 1, 0); continue; }
        if (c == '/') {
            if (peek(lx) == '/') {
                advance(lx);
                push_tok(lx, TOK_SLASH, start, 2, 0);
            } else {
                push_tok(lx, TOK_SLASH, start, 1, 0);
            }
            continue;
        }
        if (c == '%') { push_tok(lx, TOK_PERCENT, start, 1, 0); continue; }
        if (c == '(') { push_tok(lx, TOK_LPAREN, start, 1, 0); continue; }
        if (c == ')') { push_tok(lx, TOK_RPAREN, start, 1, 0); continue; }
        if (c == '[') { push_tok(lx, TOK_LBRACK, start, 1, 0); continue; }
        if (c == ']') { push_tok(lx, TOK_RBRACK, start, 1, 0); continue; }
        if (c == ':') { push_tok(lx, TOK_COLON, start, 1, 0); continue; }
        if (c == ',') { push_tok(lx, TOK_COMMA, start, 1, 0); continue; }
        if (c == '=') {
            if (peek(lx) == '=') {
                advance(lx);
                push_tok(lx, TOK_EQEQ, start, 2, 0);
            } else {
                push_tok(lx, TOK_EQ, start, 1, 0);
            }
            continue;
        }
        if (c == '!') {
            if (peek(lx) == '=') {
                advance(lx);
                push_tok(lx, TOK_NE, start, 2, 0);
                continue;
            }
        }
        if (c == '<') {
            if (peek(lx) == '=') {
                advance(lx);
                push_tok(lx, TOK_LE, start, 2, 0);
            } else {
                push_tok(lx, TOK_LT, start, 1, 0);
            }
            continue;
        }
        if (c == '>') {
            if (peek(lx) == '=') {
                advance(lx);
                push_tok(lx, TOK_GE, start, 2, 0);
            } else {
                push_tok(lx, TOK_GT, start, 1, 0);
            }
            continue;
        }

        mp_strncpy(lx->err, "invalid character", sizeof(lx->err));
        return 0;
    }

    while (lx->indent_top > 0) {
        lx->indent_top--;
        if (!push_tok(lx, TOK_DEDENT, lx->src + lx->pos, 0, 0))
            return 0;
    }
    return push_tok(lx, TOK_EOF, lx->src + lx->pos, 0, 0);
}

const char *mp_tok_name(MpTokenKind k)
{
    (void)k;
    return "tok";
}
