#pragma once

#include <stdint.h>

typedef struct {
    uint32_t* vaddr;
    int width;
    int height;
} fbdraw_fb_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} fbdraw_rect_t;


void fbdraw_init();
void fbdraw_destory();

void fbdraw_fill_rect(fbdraw_fb_t* fb, fbdraw_rect_t* rect, uint32_t color);
void fbdraw_copy_rect(fbdraw_fb_t* src_fb, fbdraw_fb_t* dst_fb, fbdraw_rect_t* src_rect, fbdraw_rect_t* dst_rect);
void fbdraw_copy_rect_force_alpha(fbdraw_fb_t* src_fb, fbdraw_fb_t* dst_fb, fbdraw_rect_t* src_rect, fbdraw_rect_t* dst_rect,uint8_t alpha);
void fbdraw_draw_rgb565(fbdraw_fb_t* dst_fb, uint8_t* rgb565_data, int width, int height);

void fbdraw_image(fbdraw_fb_t* fb, fbdraw_rect_t* rect, char* image_path);
void fbdraw_alpha_opacity_rect(fbdraw_fb_t* src_fb, fbdraw_fb_t* dst_fb, fbdraw_rect_t* src_rect, fbdraw_rect_t* dst_rect,uint8_t opacity);

