#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H

#include "types.h"

#define GUI_WIN_MAX 8
#define GUI_TITLE_MAX 32
#define GUI_EVT_MAX 16
#define GUI_LAUNCH_MAX 128

enum {
    GUI_INFO_W = 0,
    GUI_INFO_H = 1,
    GUI_INFO_FB = 2,
    GUI_INFO_DAMAGE = 3,
    GUI_INFO_PID = 4,
    GUI_INFO_MAPPED = 5,
    GUI_INFO_FLAGS = 6
};

enum {
    GUI_FLAG_RESIZABLE = 1,
    GUI_FLAG_CLOSE_HOOK = 2, /* client receives INP_CLOSE instead of hard kill */
    GUI_FLAG_NO_MINMAX = 4   /* hide minimize/maximize caption buttons */
};

void display_init(void);
int display_server_set(int on);
int display_server_pid(void);

int display_create(int w, int h, const char *title);
uint32_t display_fb(int id);
int display_damage(int id, uint32_t *frame);
int display_destroy(int id);
void display_destroy_pid(int pid); /* drop all windows of a client */
int display_info(int id, int field);
int display_set_flags(int id, int flags);
int display_resize(int id, int w, int h); /* server: realloc FB, post INP_RESIZE */
int display_set_title(int id, const char *title);
int display_get_title(int id, char *buf, int n);
int display_next_new(void); /* id of first unmapped, or -1 */
int display_ack_map(int id);
int display_ack_damage(int id);
int display_find_pid(int pid); /* window id for client pid, or -1 */

/* Client asks wm to spawn an app (not as child of the client). */
int display_request_launch(const char *path, const char *argv);
/* Server: copy next request into buf as path\\0argv\\0; returns 1 if any. */
int display_take_launch(char *buf, int n);

int display_post_event(int id, uint32_t type, int key, int x, int y, int buttons);
int display_poll_event(int id, uint32_t *type, int *key, int *x, int *y, int *buttons);

/* True if a display server owns the screen (clients must not SYS_FB_MODE). */
int display_fb_locked(void);

#endif
