#ifndef USER_TERMRUN_H
#define USER_TERMRUN_H

typedef void (*TermOutFn)(void *ctx, const char *line);

int term_run_cmd(const char *cmdline, TermOutFn out, void *ctx);

typedef struct {
    int pid;
    char acc[96];
    int accn;
    int caret;
    int alive;
    int painted;
} TermSession;

int term_session_start(TermSession *s, const char *path);
int term_session_pump(TermSession *s, int key, TermOutFn out, void *ctx);
const char *term_session_live(TermSession *s);
int term_session_caret(const TermSession *s);
void term_session_stop(TermSession *s);

#endif
