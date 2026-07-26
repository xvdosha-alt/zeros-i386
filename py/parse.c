#include "parse.h"
#include "heap.h"
#include "util.h"

static MpToken *cur(MpParser *p)
{
    if (p->pos >= p->lx->ntokens)
        return &p->lx->tokens[p->lx->ntokens - 1];
    return &p->lx->tokens[p->pos];
}

static int check(MpParser *p, MpTokenKind k)
{
    return cur(p)->kind == k;
}

static int match(MpParser *p, MpTokenKind k)
{
    if (!check(p, k))
        return 0;
    p->pos++;
    return 1;
}

static int expect(MpParser *p, MpTokenKind k, const char *msg)
{
    if (match(p, k))
        return 1;
    mp_strncpy(p->err, msg, sizeof(p->err));
    return 0;
}

static char *copy_slice(const char *s, size_t n)
{
    char *p = (char *)mp_alloc(n + 1);
    if (!p)
        return NULL;
    if (n)
        mp_memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static char *copy_string_lit(const char *s, size_t n, size_t *out_len)
{
    char *p = (char *)mp_alloc(n + 1);
    size_t i, j = 0;
    if (!p)
        return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char e = s[++i];
            if (e == 'n')
                p[j++] = '\n';
            else if (e == 't')
                p[j++] = '\t';
            else if (e == 'r')
                p[j++] = '\r';
            else
                p[j++] = e;
        } else {
            p[j++] = s[i];
        }
    }
    p[j] = 0;
    if (out_len)
        *out_len = j;
    return p;
}

static MpNode *node_new(MpParser *p, MpNodeKind kind)
{
    MpNode *n = (MpNode *)mp_alloc(sizeof(MpNode));
    if (!n) {
        mp_strncpy(p->err, "MemoryError: AST", sizeof(p->err));
        return NULL;
    }
    mp_memset(n, 0, sizeof(*n));
    n->kind = kind;
    return n;
}

static MpNode *parse_expr(MpParser *p);
static MpNode *parse_stmt(MpParser *p);
static MpNode *parse_block(MpParser *p);

static MpNode *parse_primary(MpParser *p)
{
    MpToken *t = cur(p);
    MpNode *node;

    if (match(p, TOK_NUMBER)) {
        node = node_new(p, ND_INT);
        if (!node) return NULL;
        node->ival = t->number;
    } else if (match(p, TOK_STRING)) {
        size_t slen = 0;
        node = node_new(p, ND_STR);
        if (!node) return NULL;
        node->sval = copy_string_lit(t->start, t->len, &slen);
        node->slen = slen;
        if (!node->sval) return NULL;
    } else if (match(p, TOK_TRUE)) {
        node = node_new(p, ND_BOOL);
        if (!node) return NULL;
        node->ival = 1;
    } else if (match(p, TOK_FALSE)) {
        node = node_new(p, ND_BOOL);
        if (!node) return NULL;
        node->ival = 0;
    } else if (match(p, TOK_NONE)) {
        node = node_new(p, ND_NONE);
        if (!node) return NULL;
    } else if (match(p, TOK_LBRACK)) {
        node = node_new(p, ND_LIST);
        if (!node) return NULL;
        if (!check(p, TOK_RBRACK)) {
            MpNode *tail = NULL;
            for (;;) {
                MpNode *el = parse_expr(p);
                if (!el) return NULL;
                if (!tail)
                    node->a = el;
                else
                    tail->next = el;
                tail = el;
                if (!match(p, TOK_COMMA))
                    break;
            }
        }
        if (!expect(p, TOK_RBRACK, "expected ']'"))
            return NULL;
    } else if (match(p, TOK_NAME)) {
        node = node_new(p, ND_NAME);
        if (!node) return NULL;
        node->sval = copy_slice(t->start, t->len);
        node->slen = t->len;
        if (!node->sval) return NULL;
        if (match(p, TOK_LPAREN)) {
            MpNode *call = node_new(p, ND_CALL);
            if (!call) return NULL;
            call->a = node;
            if (!check(p, TOK_RPAREN)) {
                MpNode *tail = NULL;
                for (;;) {
                    MpNode *arg = parse_expr(p);
                    if (!arg) return NULL;
                    if (!tail)
                        call->b = arg;
                    else
                        tail->next = arg;
                    tail = arg;
                    if (!match(p, TOK_COMMA))
                        break;
                }
            }
            if (!expect(p, TOK_RPAREN, "expected ')'"))
                return NULL;
            node = call;
        }
    } else if (match(p, TOK_LPAREN)) {
        node = parse_expr(p);
        if (!node) return NULL;
        if (!expect(p, TOK_RPAREN, "expected ')'"))
            return NULL;
    } else {
        mp_strncpy(p->err, "expected expression", sizeof(p->err));
        return NULL;
    }

    while (match(p, TOK_LBRACK)) {
        MpNode *idx = node_new(p, ND_INDEX);
        if (!idx) return NULL;
        idx->a = node;
        idx->b = parse_expr(p);
        if (!idx->b) return NULL;
        if (!expect(p, TOK_RBRACK, "expected ']'"))
            return NULL;
        node = idx;
    }
    return node;
}

static MpNode *parse_unary(MpParser *p)
{
    if (match(p, TOK_NOT) || match(p, TOK_MINUS) || match(p, TOK_PLUS)) {
        MpTokenKind op = p->lx->tokens[p->pos - 1].kind;
        MpNode *n = node_new(p, ND_UNARY);
        if (!n) return NULL;
        n->op = op;
        n->a = parse_unary(p);
        if (!n->a) return NULL;
        return n;
    }
    return parse_primary(p);
}

static MpNode *parse_term(MpParser *p)
{
    MpNode *left = parse_unary(p);
    if (!left) return NULL;
    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        MpTokenKind op = cur(p)->kind;
        p->pos++;
        MpNode *n = node_new(p, ND_BINOP);
        if (!n) return NULL;
        n->op = op;
        n->a = left;
        n->b = parse_unary(p);
        if (!n->b) return NULL;
        left = n;
    }
    return left;
}

static MpNode *parse_arith(MpParser *p)
{
    MpNode *left = parse_term(p);
    if (!left) return NULL;
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        MpTokenKind op = cur(p)->kind;
        p->pos++;
        MpNode *n = node_new(p, ND_BINOP);
        if (!n) return NULL;
        n->op = op;
        n->a = left;
        n->b = parse_term(p);
        if (!n->b) return NULL;
        left = n;
    }
    return left;
}

static MpNode *parse_compare(MpParser *p)
{
    MpNode *left = parse_arith(p);
    if (!left) return NULL;
    if (check(p, TOK_EQEQ) || check(p, TOK_NE) || check(p, TOK_LT) ||
        check(p, TOK_GT) || check(p, TOK_LE) || check(p, TOK_GE)) {
        MpTokenKind op = cur(p)->kind;
        p->pos++;
        MpNode *n = node_new(p, ND_COMPARE);
        if (!n) return NULL;
        n->op = op;
        n->a = left;
        n->b = parse_arith(p);
        if (!n->b) return NULL;
        return n;
    }
    return left;
}

static MpNode *parse_and(MpParser *p)
{
    MpNode *left = parse_compare(p);
    if (!left) return NULL;
    while (match(p, TOK_AND)) {
        MpNode *n = node_new(p, ND_BINOP);
        if (!n) return NULL;
        n->op = TOK_AND;
        n->a = left;
        n->b = parse_compare(p);
        if (!n->b) return NULL;
        left = n;
    }
    return left;
}

static MpNode *parse_or(MpParser *p)
{
    MpNode *left = parse_and(p);
    if (!left) return NULL;
    while (match(p, TOK_OR)) {
        MpNode *n = node_new(p, ND_BINOP);
        if (!n) return NULL;
        n->op = TOK_OR;
        n->a = left;
        n->b = parse_and(p);
        if (!n->b) return NULL;
        left = n;
    }
    return left;
}

static MpNode *parse_expr(MpParser *p)
{
    return parse_or(p);
}

static void skip_newlines(MpParser *p)
{
    while (match(p, TOK_NEWLINE)) {
    }
}

static MpNode *parse_suite(MpParser *p)
{
    if (!expect(p, TOK_COLON, "expected ':'"))
        return NULL;
    if (match(p, TOK_NEWLINE)) {
        if (!expect(p, TOK_INDENT, "expected indent"))
            return NULL;
        MpNode *block = parse_block(p);
        if (!block) return NULL;
        if (!expect(p, TOK_DEDENT, "expected dedent"))
            return NULL;
        return block;
    }
    {
        MpNode *s = parse_stmt(p);
        MpNode *block;
        if (!s) return NULL;
        block = node_new(p, ND_BLOCK);
        if (!block) return NULL;
        block->body = s;
        return block;
    }
}

static MpNode *parse_if_body(MpParser *p)
{
    MpNode *n = node_new(p, ND_IF);
    if (!n) return NULL;
    n->a = parse_expr(p);
    if (!n->a) return NULL;
    n->body = parse_suite(p);
    if (!n->body) return NULL;
    skip_newlines(p);
    if (match(p, TOK_ELIF)) {
        n->orelse = parse_if_body(p);
        if (!n->orelse) return NULL;
    } else if (match(p, TOK_ELSE)) {
        n->orelse = parse_suite(p);
        if (!n->orelse) return NULL;
    }
    return n;
}

static MpNode *parse_for(MpParser *p)
{
    MpToken *t;
    MpNode *n = node_new(p, ND_FOR);
    if (!n) return NULL;
    t = cur(p);
    if (!expect(p, TOK_NAME, "expected name in for"))
        return NULL;
    n->sval = copy_slice(t->start, t->len);
    n->slen = t->len;
    if (!n->sval) return NULL;
    if (!expect(p, TOK_IN, "expected 'in'"))
        return NULL;
    n->a = parse_expr(p);
    if (!n->a) return NULL;
    n->body = parse_suite(p);
    if (!n->body) return NULL;
    return n;
}

static MpNode *parse_def(MpParser *p)
{
    MpToken *t;
    MpNode *n = node_new(p, ND_FUNC);
    MpNode *tail = NULL;
    if (!n) return NULL;
    t = cur(p);
    if (!expect(p, TOK_NAME, "expected function name"))
        return NULL;
    n->sval = copy_slice(t->start, t->len);
    n->slen = t->len;
    if (!n->sval) return NULL;
    if (!expect(p, TOK_LPAREN, "expected '('"))
        return NULL;
    if (!check(p, TOK_RPAREN)) {
        for (;;) {
            MpToken *pt = cur(p);
            MpNode *param;
            if (!expect(p, TOK_NAME, "expected param"))
                return NULL;
            param = node_new(p, ND_NAME);
            if (!param) return NULL;
            param->sval = copy_slice(pt->start, pt->len);
            param->slen = pt->len;
            if (!param->sval) return NULL;
            if (!tail)
                n->a = param;
            else
                tail->next = param;
            tail = param;
            if (!match(p, TOK_COMMA))
                break;
        }
    }
    if (!expect(p, TOK_RPAREN, "expected ')'"))
        return NULL;
    n->body = parse_suite(p);
    if (!n->body) return NULL;
    return n;
}

static MpNode *parse_stmt(MpParser *p)
{
    skip_newlines(p);
    if (check(p, TOK_EOF) || check(p, TOK_DEDENT))
        return NULL;
    if (match(p, TOK_PASS)) {
        MpNode *n = node_new(p, ND_PASS);
        match(p, TOK_NEWLINE);
        return n;
    }
    if (match(p, TOK_RETURN)) {
        MpNode *n = node_new(p, ND_RETURN);
        if (!n) return NULL;
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF) && !check(p, TOK_DEDENT)) {
            n->a = parse_expr(p);
            if (!n->a) return NULL;
        }
        match(p, TOK_NEWLINE);
        return n;
    }
    if (match(p, TOK_IF))
        return parse_if_body(p);
    if (match(p, TOK_WHILE)) {
        MpNode *n = node_new(p, ND_WHILE);
        if (!n) return NULL;
        n->a = parse_expr(p);
        if (!n->a) return NULL;
        n->body = parse_suite(p);
        if (!n->body) return NULL;
        return n;
    }
    if (match(p, TOK_FOR))
        return parse_for(p);
    if (match(p, TOK_DEF))
        return parse_def(p);
    if (match(p, TOK_IMPORT)) {
        MpToken *t = cur(p);
        MpNode *n = node_new(p, ND_IMPORT);
        if (!n) return NULL;
        if (!expect(p, TOK_NAME, "expected module name"))
            return NULL;
        n->sval = copy_slice(t->start, t->len);
        n->slen = t->len;
        if (!n->sval) return NULL;
        match(p, TOK_NEWLINE);
        return n;
    }

    if (check(p, TOK_NAME)) {
        size_t save = p->pos;
        MpToken *t = cur(p);
        p->pos++;
        if (match(p, TOK_EQ)) {
            MpNode *name = node_new(p, ND_NAME);
            MpNode *n;
            if (!name) return NULL;
            name->sval = copy_slice(t->start, t->len);
            name->slen = t->len;
            if (!name->sval) return NULL;
            n = node_new(p, ND_ASSIGN);
            if (!n) return NULL;
            n->a = name;
            n->b = parse_expr(p);
            if (!n->b) return NULL;
            match(p, TOK_NEWLINE);
            return n;
        }
        p->pos = save;
    }

    {
        MpNode *e = parse_expr(p);
        MpNode *n;
        if (!e) return NULL;
        n = node_new(p, ND_EXPR_STMT);
        if (!n) return NULL;
        n->a = e;
        match(p, TOK_NEWLINE);
        return n;
    }
}

static MpNode *parse_block(MpParser *p)
{
    MpNode *block = node_new(p, ND_BLOCK);
    MpNode *tail = NULL;
    if (!block) return NULL;
    for (;;) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF))
            break;
        {
            MpNode *s = parse_stmt(p);
            if (!s) {
                if (p->err[0])
                    return NULL;
                break;
            }
            if (!tail)
                block->body = s;
            else
                tail->next = s;
            tail = s;
        }
    }
    return block;
}

MpNode *mp_parse(MpParser *p, MpLexer *lx)
{
    MpNode *mod;
    MpNode *tail = NULL;
    mp_memset(p, 0, sizeof(*p));
    p->lx = lx;
    p->pos = 0;
    mod = node_new(p, ND_MODULE);
    if (!mod) return NULL;
    for (;;) {
        skip_newlines(p);
        if (check(p, TOK_EOF))
            break;
        {
            MpNode *s = parse_stmt(p);
            if (!s) {
                if (p->err[0])
                    return NULL;
                break;
            }
            if (!tail)
                mod->body = s;
            else
                tail->next = s;
            tail = s;
        }
    }
    return mod;
}
