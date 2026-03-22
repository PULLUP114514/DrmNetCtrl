#include <string.h>
#include "fbdraw.h"
#include "log.h"
#include "stb_image.h"


static inline uint32_t argb8888_blend_over(uint32_t dst, uint8_t src_r, uint8_t src_g, uint8_t src_b, uint8_t src_a)
{
    if(src_a == 0) return dst;
    if(src_a == 255) return (0xFFu << 24) | ((uint32_t)src_r << 16) | ((uint32_t)src_g << 8) | (uint32_t)src_b;

    const uint8_t dst_a = (dst >> 24) & 0xFF;
    const uint8_t dst_r = (dst >> 16) & 0xFF;
    const uint8_t dst_g = (dst >> 8) & 0xFF;
    const uint8_t dst_b = dst & 0xFF;

    const uint32_t inv_sa = 255u - src_a;
    const uint32_t out_a = (uint32_t)src_a + ((uint32_t)dst_a * inv_sa + 127u) / 255u;
    if(out_a == 0) return 0;

    /* 用预乘中间量计算，最终存回“非预乘(straight) ARGB8888” */
    const uint32_t out_r_premul = (uint32_t)src_r * src_a + (((uint32_t)dst_r * dst_a) * inv_sa + 127u) / 255u;
    const uint32_t out_g_premul = (uint32_t)src_g * src_a + (((uint32_t)dst_g * dst_a) * inv_sa + 127u) / 255u;
    const uint32_t out_b_premul = (uint32_t)src_b * src_a + (((uint32_t)dst_b * dst_a) * inv_sa + 127u) / 255u;

    const uint8_t out_r = (uint8_t)((out_r_premul + out_a / 2u) / out_a);
    const uint8_t out_g = (uint8_t)((out_g_premul + out_a / 2u) / out_a);
    const uint8_t out_b = (uint8_t)((out_b_premul + out_a / 2u) / out_a);

    return ((uint32_t)out_a << 24) | ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | (uint32_t)out_b;
}

void fbdraw_fill_rect(fbdraw_fb_t* fb, fbdraw_rect_t* rect, uint32_t color){
    int x0 = rect->x;
    int y0 = rect->y;
    int x1 = rect->x + rect->w;
    int y1 = rect->y + rect->h;

    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > fb->width) x1 = fb->width;
    if(y1 > fb->height) y1 = fb->height;

    if(x0 >= x1 || y0 >= y1) return;

    for(int y = y0; y < y1; y++){
        for(int x = x0; x < x1; x++){
            fb->vaddr[y * fb->width + x] = color;
        }
    }
}

void fbdraw_copy_rect(fbdraw_fb_t* src_fb, fbdraw_fb_t* dst_fb, fbdraw_rect_t* src_rect, fbdraw_rect_t* dst_rect){
    for(int y = dst_rect->y; y < dst_rect->y + dst_rect->h; y++){
        for(int x = dst_rect->x; x < dst_rect->x + dst_rect->w; x++){
            int src_x = src_rect->x + (x - dst_rect->x);
            int src_y = src_rect->y + (y - dst_rect->y);

            if(src_x >= src_rect->x && src_x < src_rect->x + src_rect->w &&
            src_y >= src_rect->y && src_y < src_rect->y + src_rect->h &&
            x >= 0 && x < dst_fb->width && y >= 0 && y < dst_fb->height &&
            src_x >= 0 && src_x < src_fb->width &&
            src_y >= 0 && src_y < src_fb->height) 
            {
                dst_fb->vaddr[y * dst_fb->width + x] = src_fb->vaddr[src_y * src_fb->width + src_x];
            }
        }
    }
}

void fbdraw_copy_rect_force_alpha(fbdraw_fb_t* src_fb, fbdraw_fb_t* dst_fb, fbdraw_rect_t* src_rect, fbdraw_rect_t* dst_rect,uint8_t alpha){
    for(int y = dst_rect->y; y < dst_rect->y + dst_rect->h; y++){
        for(int x = dst_rect->x; x < dst_rect->x + dst_rect->w; x++){
            int src_x = src_rect->x + (x - dst_rect->x);
            int src_y = src_rect->y + (y - dst_rect->y);

            if(src_x >= src_rect->x && src_x < src_rect->x + src_rect->w &&
            src_y >= src_rect->y && src_y < src_rect->y + src_rect->h &&
            x >= 0 && x < dst_fb->width && y >= 0 && y < dst_fb->height &&
            src_x >= 0 && src_x < src_fb->width &&
            src_y >= 0 && src_y < src_fb->height) 
            {
                dst_fb->vaddr[y * dst_fb->width + x] = src_fb->vaddr[src_y * src_fb->width + src_x] | ((alpha & 0xff) << 24);
            }
        }
    }
}

void fbdraw_draw_rgb565(fbdraw_fb_t* dst_fb, uint8_t* rgb565_data, int width, int height){
    for(int y = 0; y < height; y++){
        for(int x = 0; x < width; x++){
            if(x < 0 || x >= dst_fb->width || y < 0 || y >= dst_fb->height) continue;
            uint8_t rgb565_2 = rgb565_data[(y * width + x) * 2];
            uint8_t rgb565_1 = rgb565_data[(y * width + x) * 2 + 1];
            uint16_t rgb565 = (rgb565_1 << 8) | rgb565_2;
            uint8_t r5 = (rgb565 >> 11) & 0x1F;
            uint8_t g6 = (rgb565 >> 5) & 0x3F;
            uint8_t b5 = rgb565 & 0x1F;
            uint8_t r8 = (r5 << 3);
            uint8_t g8 = (g6 << 2);
            uint8_t b8 = (b5 << 3);
            uint32_t argb8888 = (0xFF << 24) | (r8 << 16) | (g8 << 8) | b8;
            dst_fb->vaddr[y * dst_fb->width + x] = argb8888;
        }
    }
}



// 将src_fb内的src_rect ，在它的alpha的基础上，乘 opacity / 255 ，再混合到dst_fb内的dst_rect中。
void fbdraw_alpha_opacity_rect(fbdraw_fb_t* src_fb, fbdraw_fb_t* dst_fb, fbdraw_rect_t* src_rect, fbdraw_rect_t* dst_rect,uint8_t opacity){
    if(!src_fb || !dst_fb || !src_rect || !dst_rect) return;
    if(!src_fb->vaddr || !dst_fb->vaddr) return;
    if(opacity == 0) return;
    if(dst_rect->w <= 0 || dst_rect->h <= 0 || src_rect->w <= 0 || src_rect->h <= 0) return;

    for(int y = dst_rect->y; y < dst_rect->y + dst_rect->h; y++){
        if(y < 0 || y >= dst_fb->height) continue;
        for(int x = dst_rect->x; x < dst_rect->x + dst_rect->w; x++){
            if(x < 0 || x >= dst_fb->width) continue;

            const int src_x = src_rect->x + (x - dst_rect->x);
            const int src_y = src_rect->y + (y - dst_rect->y);

            if(src_x < src_rect->x || src_x >= src_rect->x + src_rect->w ||
               src_y < src_rect->y || src_y >= src_rect->y + src_rect->h) {
                continue;
            }
            if(src_x < 0 || src_x >= src_fb->width || src_y < 0 || src_y >= src_fb->height) continue;

            const uint32_t src_px = src_fb->vaddr[src_y * src_fb->width + src_x];
            const uint8_t sa = (src_px >> 24) & 0xFF;
            if(sa == 0) continue;

            uint8_t a = sa;
            if(opacity != 255) a = (uint8_t)(((uint32_t)sa * opacity + 127u) / 255u);
            if(a == 0) continue;

            const uint8_t r = (src_px >> 16) & 0xFF;
            const uint8_t g = (src_px >> 8) & 0xFF;
            const uint8_t b = src_px & 0xFF;

            uint32_t * dst = dst_fb->vaddr + y * dst_fb->width + x;
            *dst = argb8888_blend_over(*dst, r, g, b, a);
        }
    }
}

