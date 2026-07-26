#ifndef MP_OBJECT_H
#define MP_OBJECT_H

#include "config.h"

struct MpNode;

typedef enum {
    MP_T_NONE = 0,
    MP_T_BOOL,
    MP_T_INT,
    MP_T_STR,
    MP_T_FILE,
    MP_T_LIST,
    MP_T_FUNC
} MpType;

typedef struct MpObject {
    MpType type;
    int refcnt;
    union {
        int32_t i;
        struct {
            char *data;
            size_t len;
        } s;
        int fd;
        struct {
            struct MpObject **items;
            size_t len;
        } list;
        struct {
            struct MpNode *node;
        } fn;
    } v;
} MpObject;

extern MpObject *Mp_None;
extern MpObject *Mp_True;
extern MpObject *Mp_False;

void mp_objects_init(void);
MpObject *mp_int_new(int32_t v);
MpObject *mp_str_new(const char *data, size_t len);
MpObject *mp_str_new_cstr(const char *data);
MpObject *mp_bool_from(int v);
MpObject *mp_file_new(int fd);
MpObject *mp_list_new(void);
int mp_list_append(MpObject *list, MpObject *item);
MpObject *mp_list_get(MpObject *list, int32_t idx);
MpObject *mp_func_new(struct MpNode *node);

void mp_incref(MpObject *o);
void mp_decref(MpObject *o);

int mp_is_truthy(MpObject *o);
const char *mp_type_name(MpObject *o);
int mp_as_int(MpObject *o, int32_t *out);
const char *mp_as_cstr(MpObject *o);

#endif
