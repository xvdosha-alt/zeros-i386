#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

#include "types.h"

#define PROC_MAX 16
#define PROC_NAME 32
#define PROC_CWD 128
#define PROC_CONS_OUT 2048
#define PROC_CONS_IN 128

enum {
    PROC_FREE = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE
};

typedef struct Proc {
    int used;
    int pid;
    int ppid;
    int state;
    int exit_code;
    int started;
    char name[PROC_NAME];
    char cwd[PROC_CWD]; /* per-process working directory */
    uint32_t entry;
    uint32_t user_esp;
    uint32_t user_eip;
    uint32_t kstack_top;
    uint8_t *image;
    uint32_t image_pages;
    uint8_t *image_shadow; /* swap-out when another proc needs our link address */
    int image_evicted;
    uint8_t *ustack;
    uint32_t ustack_pages;
    uint32_t brk;
    int wait_pid;
    uint32_t user_eax;
    /* Coop-yield: pointer into this proc's kstack syscall trap frame.
     * Resume via resume_user_frame (full GPR restore), not jump_to_user. */
    uint32_t *saved_kframe;
    /* Saved when a parent runs us via proc_run_blocking (nested wait-safe). */
    struct Proc *block_parent;
    uint32_t block_ret_ebp;
    uint32_t block_prev_kret;
    /* GUI / console attachment for text apps under a terminal shim */
    int cons_enable;
    int cons_host; /* set by sys_cons_attach — DOS-box parent */
    int cons_sink; /* pid whose cons_out receives this proc's stdout */
    int cons_forbid; /* sticky detach: proc_cons_repair must not re-enable */
    char cons_out[PROC_CONS_OUT];
    int cons_out_len;
    /* Keyboard ring: full int keycodes (arrows are 0x100+), not raw bytes. */
    int cons_in_keys[PROC_CONS_IN];
    int cons_in_len;
    int cons_in_pos;
    /* Optional redirected stdio (vfs/pipe fds); -1 = tty/cons default. */
    int redir_in;
    int redir_out;
    int need_preempt; /* soft RR hint from PIT */
} Proc;

int proc_evict_range(uint32_t lo, uint32_t hi);
void proc_ensure_resident(Proc *p);

void proc_init(void);
Proc *proc_current(void);
int proc_spawn_elf(const char *path, int argc, char **argv);
void proc_exit(int code);
int proc_kill(int pid); /* parent kills child (+ descendants); returns 0 ok */
int proc_wait(int pid, int *status);
void proc_yield(void);
void proc_yield_to_parent(uint32_t *frame);
void sched_on_timer(void);
void proc_schedule(void);
int proc_fg_pid(void);
void proc_set_fg(int pid);
Proc *proc_get(int pid);
Proc *proc_get_by_index(int i);
/* Fill *out with the n-th used process (0-based). Returns 0 ok, -1 done. */
int proc_psinfo(int n, int *pid, int *ppid, int *state, char *name, int namelen);
int proc_reap(int pid);
void proc_switch_user(Proc *p);
int proc_run_blocking(Proc *p);
void proc_return_to_kernel(void);
void proc_set_spawn_cons(int on);
void proc_set_spawn_redir(int in_fd, int out_fd);
int proc_cons_repair(Proc *p);
int proc_cons_read(int pid, char *buf, int n);
int proc_cons_write_in(int pid, const char *buf, int n);
int proc_cons_putkey(int pid, int key);
int proc_cons_putc(Proc *p, char c);
int proc_cons_write(Proc *p, const char *s, int n);
int proc_cons_getc(Proc *p);

#endif
