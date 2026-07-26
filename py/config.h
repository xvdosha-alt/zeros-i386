#ifndef MP_CONFIG_H
#define MP_CONFIG_H

#ifdef MP_HOST
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define MP_HEAP_SIZE (512 * 1024)
#define MPFS_MAX_FILES_CFG 32
#define MPFS_FILE_MAX_CFG 4096
#define MP_MAX_TOKENS 2048
#else
#include "../kernel/core/kernel_stdint.h"
#include "../kernel/core/kernel_stddef.h"
#define MP_HEAP_SIZE (64 * 1024)
#define MPFS_MAX_FILES_CFG 8
#define MPFS_FILE_MAX_CFG 2048
#define MP_MAX_TOKENS 256
#endif

#define MP_ENV_SLOTS 128
#define MP_MAX_LINE 512
#define MP_HISTORY 8
#define MP_LIST_MAX 64
#define MP_FUNC_ARGS 8
#endif
