#pragma once

#include <stdint.h>
#include "fbdraw.h"
#include "stb_truetype.h"

typedef struct {
    stbtt_fontinfo font;
    uint8_t* data;
} fbdraw_ttf_font_t;

void fbdraw_ttf_load_font(fbdraw_ttf_font_t * font, char* font_path);
void fbdraw_ttf_free_font(fbdraw_ttf_font_t * font);
void fbdraw_ttf_draw_text(fbdraw_fb_t* fb, fbdraw_rect_t* rect, fbdraw_ttf_font_t * font,char* text, float text_size_px, int text_color);