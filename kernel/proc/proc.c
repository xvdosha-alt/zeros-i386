#include "proc.h"
#include "elf.h"
#include "mm.h"
#include "vfs.h"
#include "tty.h"
#include "string.h"
#include "display.h"

extern void tss_set_stack(uint32_t esp0);
extern void jump_to_user(uint32_t entry, uint32_t user_esp, uint32_t user_eax);
extern void resume_user_frame(uint32_t *frame);
extern uint8_t stack_top[];

static Proc procs[PROC_MAX];
static Proc *current;
static int next_pid = 1;
static int fg_pid = 1;
static uint32_t kret_esp;
static int spawn_cons_next;
static int spawn_redir_in = -1;
static int spawn_redir_out = -1;

void proc_set_spawn_redir(int in_fd, int out_fd)
{
    spawn_redir_in = in_fd;
    spawn_redir_out = out_fd;
}
static int resume_yielded;

/* Nested wait stack — globals were wrong for msh→child→exit; a small
 * explicit stack keeps leave;ret reliable with a real frame pointer. */
#define BLOCK_STACK_MAX 8
static struct {
    Proc *prev;
    uint32_t ret_ebp;
    uint32_t prev_kret;
} block_stack[BLOCK_STACK_MAX];
static int block_sp;

int proc_evict_range(uint32_t lo, uint32_t hi)
{
    int i;
    for (i = 0; i < PROC_MAX; i++) {
        uint32_t plo, phi;
        size_t bytes;
        if (!procs[i].used || !procs[i].image || !procs[i].image_pages)
            continue;
        if (procs[i].image_evicted)
            continue;
        /* Zombies will never run again — safe to overwrite without save. */
        if (procs[i].state == PROC_ZOMBIE)
            continue;
        plo = (uint32_t)procs[i].image;
        phi = plo + procs[i].image_pages * PAGE_SIZE;
        if (lo >= phi || hi <= plo)
            continue;
        bytes = (size_t)procs[i].image_pages * PAGE_SIZE;
        procs[i].image_shadow = (uint8_t *)mm_alloc_pages(procs[i].image_pages);
        if (!procs[i].image_shadow)
            return -1;
        kmemcpy(procs[i].image_shadow, procs[i].image, bytes);
        procs[i].image_evicted = 1;
    }
    return 0;
}

void proc_ensure_resident(Proc *p)
{
    int i;
    uint32_t lo, hi;
    size_t bytes;
    if (!p || !p->image || !p->image_pages || !p->image_evicted)
        return;
    if (!p->image_shadow)
        return; /* cannot restore — leave evicted rather than jump to junk */
    lo = (uint32_t)p->image;
    hi = lo + p->image_pages * PAGE_SIZE;
    /* Anyone living in our slot (except zombies) must be swapped out first. */
    for (i = 0; i < PROC_MAX; i++) {
        uint32_t plo, phi;
        size_t qb;
        if (!procs[i].used || &procs[i] == p || !procs[i].image)
            continue;
        if (procs[i].image_evicted || procs[i].state == PROC_ZOMBIE)
            continue;
        plo = (uint32_t)procs[i].image;
        phi = plo + procs[i].image_pages * PAGE_SIZE;
        if (lo >= phi || hi <= plo)
            continue;
        qb = (size_t)procs[i].image_pages * PAGE_SIZE;
        if (procs[i].image_shadow)
            continue; /* already has a shadow somehow */
        procs[i].image_shadow = (uint8_t *)mm_alloc_pages(procs[i].image_pages);
        if (!procs[i].image_shadow)
            return; /* keep p evicted; do not clobber live image */
        kmemcpy(procs[i].image_shadow, procs[i].image, qb);
        procs[i].image_evicted = 1;
    }
    bytes = (size_t)p->image_pages * PAGE_SIZE;
    kmemcpy(p->image, p->image_shadow, bytes);
    mm_free_pages(p->image_shadow, p->image_pages);
    p->image_shadow = 0;
    p->image_evicted = 0;
}

void proc_init(void)
{
    kmemset(procs, 0, sizeof(procs));
    current = &procs[0];
    current->used = 1;
    current->pid = next_pid++;
    current->ppid = 0;
    current->state = PROC_RUNNING;
    kstrncpy(current->name, "zerosd", PROC_NAME);
    kstrncpy(current->cwd, "/sys", sizeof(current->cwd));
    fg_pid = current->pid;
    spawn_cons_next = 0;
    resume_yielded = 0;
    block_sp = 0;
}

Proc *proc_current(void)
{
    return current;
}

Proc *proc_get(int pid)
{
    int i;
    for (i = 0; i < PROC_MAX; i++)
        if (procs[i].used && procs[i].pid == pid)
            return &procs[i];
    return 0;
}

Proc *proc_get_by_index(int i)
{
    if (i < 0 || i >= PROC_MAX || !procs[i].used)
        return 0;
    return &procs[i];
}

int proc_fg_pid(void)
{
    return fg_pid;
}

void proc_set_fg(int pid)
{
    fg_pid = pid;
    tty_set_fg(pid);
}

void proc_set_spawn_cons(int on)
{
    spawn_cons_next = on ? 1 : 0;
    /* Mark the caller as a DOS-box host so children always get a cons,
     * and so we can repair a missed attach on first stdin/stdout use. */
    if (current)
        current->cons_host = on ? 1 : 0;
}

/* If this proc is being wait()ed by a cons_host, force GUI console mode.
 * Prevents hanging the host on tty_getc when attach was missed. */
int proc_cons_repair(Proc *p)
{
    Proc *waiter;
    if (!p || p->cons_enable)
        return p ? p->cons_enable : 0;
    if (p->cons_forbid)
        return 0;
    if (block_sp <= 0)
        return 0;
    waiter = block_stack[block_sp - 1].prev;
    if (!waiter || !waiter->cons_host)
        return 0;
    p->cons_enable = 1;
    if (!p->cons_sink)
        p->cons_sink = p->pid;
    return 1;
}

static Proc *alloc_proc(void)
{
    int i;
    for (i = 1; i < PROC_MAX; i++) {
        if (!procs[i].used) {
            kmemset(&procs[i], 0, sizeof(procs[i]));
            procs[i].used = 1;
            return &procs[i];
        }
    }
    return 0;
}

int proc_cons_putc(Proc *p, char c)
{
    Proc *sink;
    if (!p || !p->cons_enable)
        return -1;
    sink = p->cons_sink ? proc_get(p->cons_sink) : p;
    if (!sink)
        sink = p;
    if (sink->cons_out_len + 1 >= PROC_CONS_OUT)
        return -1;
    sink->cons_out[sink->cons_out_len++] = c;
    sink->cons_out[sink->cons_out_len] = 0;
    return 0;
}

int proc_cons_write(Proc *p, const char *s, int n)
{
    int i;
    if (!p || !p->cons_enable || !s || n <= 0)
        return -1;
    for (i = 0; i < n; i++) {
        if (proc_cons_putc(p, s[i]) < 0)
            break;
    }
    return i;
}

int proc_cons_read(int pid, char *buf, int n)
{
    Proc *p = proc_get(pid);
    int got;
    if (!p || !buf || n <= 0)
        return -1;
    got = p->cons_out_len;
    if (got > n)
        got = n;
    if (got > 0) {
        kmemcpy(buf, p->cons_out, (size_t)got);
        /* slide remaining */
        if (got < p->cons_out_len) {
            int rest = p->cons_out_len - got;
            kmemcpy(p->cons_out, p->cons_out + got, (size_t)rest);
            p->cons_out_len = rest;
            p->cons_out[rest] = 0;
        } else {
            p->cons_out_len = 0;
            p->cons_out[0] = 0;
        }
    }
    return got;
}

static Proc *cons_input_target(Proc *p)
{
    Proc *t;
    int i, depth;
    if (!p)
        return 0;
    t = p;
    for (depth = 0; depth < PROC_MAX; depth++) {
        Proc *child = 0;
        for (i = 0; i < PROC_MAX; i++) {
            if (!procs[i].used || procs[i].ppid != t->pid)
                continue;
            if (!procs[i].cons_enable || procs[i].state == PROC_ZOMBIE)
                continue;
            child = &procs[i];
            break;
        }
        if (!child)
            break;
        t = child;
    }
    return t->cons_enable ? t : 0;
}

static void cons_in_compact(Proc *t)
{
    int rest;
    if (!t || t->cons_in_pos <= 0)
        return;
    rest = t->cons_in_len - t->cons_in_pos;
    if (rest > 0)
        kmemcpy(t->cons_in_keys, t->cons_in_keys + t->cons_in_pos,
                (size_t)rest * sizeof(int));
    else
        rest = 0;
    t->cons_in_len = rest;
    t->cons_in_pos = 0;
}

int proc_cons_write_in(int pid, const char *buf, int n)
{
    Proc *t = cons_input_target(proc_get(pid));
    int i;
    if (!t || !buf || n <= 0)
        return -1;
    cons_in_compact(t);
    for (i = 0; i < n; i++) {
        if (t->cons_in_len + 1 >= PROC_CONS_IN)
            break;
        t->cons_in_keys[t->cons_in_len++] = (unsigned char)buf[i];
    }
    return i;
}

int proc_cons_putkey(int pid, int key)
{
    Proc *t = cons_input_target(proc_get(pid));
    if (!t)
        return -1;
    cons_in_compact(t);
    if (t->cons_in_len + 1 >= PROC_CONS_IN)
        return -1;
    t->cons_in_keys[t->cons_in_len++] = key;
    return 1;
}

int proc_cons_getc(Proc *p)
{
    int c;
    if (!p || !p->cons_enable)
        return -1;
    if (p->cons_in_pos >= p->cons_in_len)
        return -1;
    c = p->cons_in_keys[p->cons_in_pos++];
    if (p->cons_in_pos >= p->cons_in_len) {
        p->cons_in_pos = 0;
        p->cons_in_len = 0;
    }
    return c;
}

int proc_spawn_elf(const char *path, int argc, char **argv)
{
    uint8_t *file;
    int sz;
    Proc *p;
    uint32_t entry, pages, brk;
    uint8_t *image;
    uint8_t *ustack;
    uint32_t *sp;
    const char *slash;
    const char *s;
    void *ks;
    (void)argc;
    (void)argv;

    sz = vfs_size(path);
    if (sz < 0)
        return -1;
    {
        size_t npages = ((size_t)sz + PAGE_SIZE - 1) / PAGE_SIZE;
        if (!npages)
            npages = 1;
        file = (uint8_t *)mm_alloc_pages(npages);
        if (!file)
            return -1;
        if (vfs_read_file(path, file, (size_t)sz) != sz) {
            mm_free_pages(file, npages);
            return -1;
        }
        {
            int el = elf_load(file, (size_t)sz, &entry, &image, &pages, &brk);
            if (el < 0) {
                mm_free_pages(file, npages);
                return el; /* -1 corrupt, -2 image busy (nested same binary) */
            }
        }
        mm_free_pages(file, npages);
    }

    p = alloc_proc();
    if (!p)
        return -1;

    ustack = (uint8_t *)mm_alloc_pages(USER_STACK_SIZE / PAGE_SIZE);
    if (!ustack)
        return -1;
    /* Soft stack guard canary at low end of user stack pages. */
    *(uint32_t *)ustack = 0xDEADC0DEu;
    ks = mm_alloc_pages(4);
    if (!ks)
        return -1;

    p->pid = next_pid++;
    p->ppid = current ? current->pid : 1;
    p->state = PROC_READY;
    p->entry = entry;
    p->user_esp = 0;
    p->image = image;
    p->image_pages = pages;
    p->ustack = ustack;
    p->ustack_pages = USER_STACK_SIZE / PAGE_SIZE;
    p->brk = brk;
    p->kstack_top = (uint32_t)ks + 4 * PAGE_SIZE;
    p->started = 0;
    /* Inherit GUI console like a DOS box under Windows: children share the sink
     * when spawned from an already-hosted process. A fresh cons_attach() starts
     * a new session — child owns its own ring so the host can cons_read(child). */
    if (spawn_cons_next || (current && (current->cons_enable || current->cons_host))) {
        p->cons_enable = 1;
        if (spawn_cons_next)
            p->cons_sink = p->pid;
        else if (current && current->cons_enable)
            p->cons_sink = current->cons_sink ? current->cons_sink : current->pid;
        else
            p->cons_sink = p->pid;
    } else {
        p->cons_enable = 0;
        p->cons_sink = 0;
    }
    p->cons_out_len = 0;
    p->cons_in_len = 0;
    p->cons_in_pos = 0;
    p->redir_in = spawn_redir_in;
    p->redir_out = spawn_redir_out;
    spawn_redir_in = -1;
    spawn_redir_out = -1;
    /* Inherit cwd from parent (each process has its own after chdir). */
    if (current && current->cwd[0])
        kstrncpy(p->cwd, current->cwd, sizeof(p->cwd));
    else
        kstrncpy(p->cwd, "/sys", sizeof(p->cwd));
    spawn_cons_next = 0;

    slash = path;
    for (s = path; *s; s++)
        if (*s == '/')
            slash = s + 1;
    kstrncpy(p->name, slash, PROC_NAME);

    sp = (uint32_t *)(ustack + USER_STACK_SIZE);
    sp -= 8;
    kmemset(sp, 0, 32);
    p->user_esp = (uint32_t)sp;
    p->user_eip = entry;
    p->user_eax = 0;
    p->saved_kframe = 0;
    /* Nested same-link-address spawn (e.g. gterm→gterm) leaves the caller
     * image_evicted with a stale shadow. Restore before returning to user so
     * post-spawn stores (session pid/alive) land in the caller's image and a
     * later wait() re-evicts with an up-to-date shadow. */
    if (current)
        proc_ensure_resident(current);
    return p->pid;
}

void proc_return_to_kernel(void)
{
    Proc *parent;
    uint32_t ret_ebp;
    int code;

    if (block_sp <= 0)
        return;
    block_sp--;
    parent = block_stack[block_sp].prev;
    ret_ebp = block_stack[block_sp].ret_ebp;
    code = resume_yielded ? -2 : (current ? current->exit_code : 0);
    resume_yielded = 0;

    current = parent;
    kret_esp = block_stack[block_sp].prev_kret;
    if (parent && parent->kstack_top)
        tss_set_stack(parent->kstack_top);
    else
        tss_set_stack((uint32_t)stack_top);
    if (parent) {
        fg_pid = parent->pid;
        tty_set_fg(parent->pid);
        parent->state = PROC_RUNNING;
        proc_ensure_resident(parent);
    }

    __asm__ volatile (
        "movl %0, %%ebp\n\t"
        "movl %1, %%eax\n\t"
        "leave\n\t"
        "ret\n\t"
        :
        : "r"(ret_ebp), "r"(code)
        : "memory"
    );
}

__attribute__((noinline))
int proc_run_blocking(Proc *p)
{
    uint32_t eip, esp, ebp;
    if (!p)
        return -1;
    if (block_sp >= BLOCK_STACK_MAX)
        return -1;

    block_stack[block_sp].prev = current;
    block_stack[block_sp].prev_kret = kret_esp;
    if (current)
        current->state = PROC_READY;
    current = p;
    p->state = PROC_RUNNING;
    p->block_parent = block_stack[block_sp].prev;
    fg_pid = p->pid;
    tty_set_fg(p->pid);
    tss_set_stack(p->kstack_top);
    resume_yielded = 0;

    eip = p->user_eip ? p->user_eip : p->entry;
    esp = p->user_esp;
    p->started = 1;
    proc_ensure_resident(p);
    if (p->image_evicted) {
        /* Swap-in failed — do not run with another proc's image. */
        current = block_stack[block_sp].prev;
        if (current)
            current->state = PROC_RUNNING;
        p->state = PROC_READY;
        return -2;
    }

    __asm__ volatile ("movl %%ebp, %0" : "=r"(ebp) : : "memory");
    block_stack[block_sp].ret_ebp = ebp;
    block_sp++;
    __asm__ volatile ("movl %%esp, %0" : "=r"(kret_esp) : : "memory");

    /* DOS-box resume: finish the interrupted syscall with a full iret so
     * ebx/ecx/edx/esi/edi survive (jump_to_user alone corrupts them). */
    if (p->saved_kframe) {
        uint32_t *f = p->saved_kframe;
        p->saved_kframe = 0;
        f[7] = p->user_eax;
        resume_user_frame(f);
    }
    jump_to_user(eip, esp, p->user_eax);

    /* Only reached if jump_to_user returned (should not). */
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void proc_exit(int code)
{
    if (!current || current->pid == 1)
        return;
    current->exit_code = code;
    current->state = PROC_ZOMBIE;
    current->saved_kframe = 0;
    resume_yielded = 0;
    proc_return_to_kernel();
}

static void proc_kill_one(Proc *p);

int proc_reap(int pid)
{
    Proc *p = proc_get(pid);
    if (!p || p->state != PROC_ZOMBIE)
        return -1;
    if (p->image_shadow)
        mm_free_pages(p->image_shadow, p->image_pages);
    if (p->ustack)
        mm_free_pages(p->ustack, p->ustack_pages);
    if (p->kstack_top)
        mm_free_pages((void *)(p->kstack_top - 4 * PAGE_SIZE), 4);
    p->used = 0;
    return 0;
}

static void proc_kill_one(Proc *p)
{
    int i;
    if (!p || !p->used || p->pid <= 1)
        return;
    if (p->state == PROC_ZOMBIE)
        return;
    /* Kill then immediately reap descendants — otherwise they stay
     * zombies under a parent that is about to become a zombie itself. */
    for (i = 0; i < PROC_MAX; i++) {
        if (procs[i].used && procs[i].ppid == p->pid) {
            int cpid = procs[i].pid;
            proc_kill_one(&procs[i]);
            if (procs[i].used && procs[i].state == PROC_ZOMBIE)
                proc_reap(cpid);
        }
    }
    display_destroy_pid(p->pid);
    p->exit_code = 137;
    p->state = PROC_ZOMBIE;
    p->saved_kframe = 0;
    p->cons_enable = 0;
    p->cons_out_len = 0;
    p->cons_in_len = 0;
    p->cons_in_pos = 0;
}

int proc_kill(int pid)
{
    Proc *p = proc_get(pid);
    Proc *cur = proc_current();
    if (!p || !cur || pid <= 1)
        return -1;
    /* Refuse to kill yourself via this path (use exit). */
    if (pid == cur->pid)
        return -1;
    if (p->state == PROC_ZOMBIE)
        return 0;
    proc_kill_one(p);
    return 0;
}

int proc_psinfo(int n, int *pid, int *ppid, int *state, char *name, int namelen)
{
    int i, seen = 0;
    if (n < 0 || !pid || !ppid || !state)
        return -1;
    for (i = 0; i < PROC_MAX; i++) {
        if (!procs[i].used)
            continue;
        if (seen == n) {
            *pid = procs[i].pid;
            *ppid = procs[i].ppid;
            *state = procs[i].state;
            if (name && namelen > 0)
                kstrncpy(name, procs[i].name, (size_t)namelen);
            return 0;
        }
        seen++;
    }
    return -1;
}

/* Cooperative yield back to parent that is running us via wait/continue.
 * Like a Win3.1 VM blocking on INT 16h: keep the full trap frame so the
 * next slice can iret with every user register intact. */
void proc_yield_to_parent(uint32_t *frame)
{
    if (!current || block_sp <= 0)
        return;
    /* frame: pusha + segs; user EIP at [12], user ESP at [15], EAX at [7] */
    current->user_eip = frame[12];
    current->user_esp = frame[15];
    current->user_eax = frame[7];
    current->saved_kframe = frame;
    current->state = PROC_READY;
    resume_yielded = 1;
    proc_return_to_kernel();
}

void proc_yield(void)
{
    /* idle only when no parent slice */
    __asm__ volatile ("sti; hlt");
}

int proc_wait(int pid, int *status)
{
    Proc *child = 0;
    int i;
    int code;
    if (pid > 0)
        child = proc_get(pid);
    else {
        for (i = 0; i < PROC_MAX; i++) {
            if (procs[i].used && procs[i].ppid == current->pid &&
                (procs[i].state == PROC_READY || procs[i].state == PROC_RUNNING ||
                 procs[i].state == PROC_ZOMBIE)) {
                child = &procs[i];
                break;
            }
        }
    }
    if (!child)
        return -1;
    if (child->state != PROC_ZOMBIE) {
        code = proc_run_blocking(child);
        if (child->state != PROC_ZOMBIE) {
            /* Child cooperatively yielded — still running. */
            if (status)
                *status = -2;
            return -2;
        }
        code = child->exit_code;
    } else {
        code = child->exit_code;
    }
    if (status)
        *status = code;
    pid = child->pid;
    proc_reap(pid);
    return pid;
}

void proc_switch_user(Proc *p)
{
    proc_run_blocking(p);
}

void sched_on_timer(void)
{
    /* Soft preemption: ask the in-flight child to yield back to its
     * parent at the next safe syscall boundary (no full IRQ switch). */
    if (current && current->state == PROC_RUNNING)
        current->need_preempt = 1;
}

void proc_schedule(void)
{
    /* Ready-queue rotation is parent-driven via wait/yield today. */
}
