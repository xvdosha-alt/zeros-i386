#ifndef USER_WFILEDLG_H
#define USER_WFILEDLG_H

#include "wlist.h"
#include "wentry.h"
#include "wbutton.h"
#include "wlabel.h"

#define WFILE_NAME_MAX 56
#define WFILE_PATH_MAX 96

typedef struct WFileDialog WFileDialog;
typedef void (*WFileOkFn)(WFileDialog *self, const char *path, void *userdata);

struct WFileDialog {
    WFrame frame;
    WLabel title;
    WListBox list;
    WEntry path_entry;
    WButton ok_btn;
    WButton cancel_btn;
    char path[WFILE_PATH_MAX];
    char listing[2048];
    char names[WLIST_MAX][WFILE_NAME_MAX];
    char pathbuf[WFILE_PATH_MAX];
    int visible;
    int save_mode; /* 1 = save (editable path), 0 = open */
    WFileOkFn on_ok;
    void *userdata;
};

void wfiledlg_init(WFileDialog *d, int save_mode);
void wfiledlg_set_handler(WFileDialog *d, WFileOkFn fn, void *userdata);
void wfiledlg_show(WFileDialog *d, const char *dir_or_path);
void wfiledlg_hide(WFileDialog *d);
int wfiledlg_visible(const WFileDialog *d);
void wfiledlg_layout(WFileDialog *d, int ax, int ay, int aw, int ah);
void wfiledlg_draw(WFileDialog *d, GuiScreen *scr);
void wfiledlg_input(WFileDialog *d, int mx, int my, int buttons);
void wfiledlg_key(WFileDialog *d, int key);

#endif
