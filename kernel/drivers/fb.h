#ifndef KERNEL_FB_H
#define KERNEL_FB_H

#include "types.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t addr;
} FbInfo;

int fb_init(uint32_t w, uint32_t h, uint32_t bpp);
void fb_textmode(void);
int fb_ready(void);
void fb_info(FbInfo *out);
uint32_t *fb_ptr(void);
uint32_t fb_lfb(void);
void fb_flip(void);
/* Copy a rectangle from the backbuffer to the hardware LFB. */
void fb_flip_rect(int x, int y, int w, int h);
void fb_fill(uint32_t color);
void fb_pixel(int x, int y, uint32_t color);
void fb_rect(int x, int y, int w, int h, uint32_t color);
void fb_rect_border(int x, int y, int w, int h, uint32_t fill, uint32_t border);
void fb_blit(int x, int y, const uint32_t *src, int w, int h, int src_pitch);

#endif
