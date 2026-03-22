#include <stdint.h>
#include "fbdraw.h"
#include "log.h"
#include "stb_image.h"

void fbdraw_image(fbdraw_fb_t* fb, fbdraw_rect_t* rect, char* image_path){
    int w,h,c;
    uint8_t* pixdata = stbi_load(image_path, &w, &h, &c, 4);
    if(!pixdata){
        log_error("failed to load image: %s", image_path);
        return;
    }

    for(int y = rect->y; y < rect->y + rect->h; y++){
        for(int x = rect->x; x < rect->x + rect->w; x++){

            int src_x = x - rect->x;
            int src_y = y - rect->y;

            if(src_x >= 0 && src_x < w && src_y >= 0 && src_y < h){
                uint32_t * dst = fb->vaddr + x + y * fb->width;
                uint32_t bgra_pixel = *((uint32_t *)(pixdata) + src_x + src_y * w);
                uint32_t rgb_pixel = (bgra_pixel & 0x000000FF) << 16 | (bgra_pixel & 0x0000FF00) | (bgra_pixel & 0x00FF0000) >> 16 | (bgra_pixel & 0xFF000000);
                *dst = rgb_pixel;
            }
        }
    }

    stbi_image_free(pixdata);
}