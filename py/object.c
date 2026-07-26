#include "object.h"
#include "heap.h"
#include "util.h"

static MpObject none_obj;
static MpObject true_obj;
static MpObject false_obj;

MpObject *Mp_None = &none_obj;
MpObject *Mp_True = &true_obj;
MpObject *Mp_False = &false_obj;

void mp_objects_init(void)
{
    none_obj.type = MP_T_NONE;
    none_obj.refcnt = 1;
    true_obj.type = MP_T_BOOL;
    true_obj.refcnt = 1;
    true_obj.v.i = 1;
    false_obj.type = MP_T_BOOL;
    false_obj.refcnt = 1;
    false_obj.v.i = 0;
}

static MpObject *alloc_obj(MpType t)
{
    MpObject *o = (MpObject *)mp_alloc(sizeof(MpObject));
    if (!o)
        return NULL;
    o->type = t;
    o->refcnt = 1;
    return o;
}

MpObject *mp_int_new(int32_t v)
{
    MpObject *o = alloc_obj(MP_T_INT);
    if (!o)
        return NULL;
    o->v.i = v;
    return o;
}

MpObject *mp_bool_from(int v)
{
    return v ? Mp_True : Mp_False;
}

MpObject *mp_str_new(const char *data, size_t len)
{
    MpObject *o = alloc_obj(MP_T_STR);
    if (!o)
        return NULL;
    o->v.s.data = (char *)mp_alloc(len + 1);
    if (!o->v.s.data)
        return NULL;
    if (len)
        mp_memcpy(o->v.s.data, data, len);
    o->v.s.data[len] = 0;
    o->v.s.len = len;
    return o;
}

MpObject *mp_str_new_cstr(const char *data)
{
    return mp_str_new(data, mp_strlen(data));
}

MpObject *mp_file_new(int fd)
{
    MpObject *o = alloc_obj(MP_T_FILE);
    if (!o)
        return NULL;
    o->v.fd = fd;
    return o;
}

MpObject *mp_list_new(void)
{
    MpObject *o = alloc_obj(MP_T_LIST);
    if (!o)
        return NULL;
    o->v.list.items = (MpObject **)mp_alloc(sizeof(MpObject *) * MP_LIST_MAX);
    if (!o->v.list.items)
        return NULL;
    o->v.list.len = 0;
    return o;
}

int mp_list_append(MpObject *list, MpObject *item)
{
    if (!list || list->type != MP_T_LIST)
        return 0;
    if (list->v.list.len >= MP_LIST_MAX)
        return 0;
    list->v.list.items[list->v.list.len++] = item;
    return 1;
}

MpObject *mp_list_get(MpObject *list, int32_t idx)
{
    if (!list || list->type != MP_T_LIST)
        return NULL;
    if (idx < 0)
        idx += (int32_t)list->v.list.len;
    if (idx < 0 || (size_t)idx >= list->v.list.len)
        return NULL;
    return list->v.list.items[idx];
}

MpObject *mp_func_new(struct MpNode *node)
{
    MpObject *o = alloc_obj(MP_T_FUNC);
    if (!o)
        return NULL;
    o->v.fn.node = node;
    return o;
}

void mp_incref(MpObject *o)
{
    if (o && o != Mp_None && o != Mp_True && o != Mp_False)
        o->refcnt++;
}

void mp_decref(MpObject *o)
{
    (void)o;
}

int mp_is_truthy(MpObject *o)
{
    if (!o || o->type == MP_T_NONE)
        return 0;
    if (o->type == MP_T_BOOL || o->type == MP_T_INT)
        return o->v.i != 0;
    if (o->type == MP_T_STR)
        return o->v.s.len != 0;
    if (o->type == MP_T_LIST)
        return o->v.list.len != 0;
    return 1;
}

const char *mp_type_name(MpObject *o)
{
    if (!o)
        return "NoneType";
    switch (o->type) {
    case MP_T_NONE: return "NoneType";
    case MP_T_BOOL: return "bool";
    case MP_T_INT: return "int";
    case MP_T_STR: return "str";
    case MP_T_FILE: return "file";
    case MP_T_LIST: return "list";
    case MP_T_FUNC: return "function";
    }
    return "object";
}

int mp_as_int(MpObject *o, int32_t *out)
{
    if (!o)
        return 0;
    if (o->type == MP_T_INT || o->type == MP_T_BOOL) {
        *out = o->v.i;
        return 1;
    }
    return 0;
}

const char *mp_as_cstr(MpObject *o)
{
    if (o && o->type == MP_T_STR)
        return o->v.s.data;
    return NULL;
}
