#include "stb_truetype.h" 

#include "fbdrawttf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "log.h"

// Blend src over dst assuming dst is opaque (A=255) and output opaque pixel.
// This matches typical "XRGB/ARGB framebuffer displayed as opaque" behavior and avoids edge artifacts
// when dst alpha is 0/uninitialized.
static inline uint32_t argb8888_blend_over_opaque_dst(uint32_t dst, uint8_t src_r, uint8_t src_g, uint8_t src_b, uint8_t src_a)
{
    if(src_a == 0) return dst;
    if(src_a == 255) return (0xFFu << 24) | ((uint32_t)src_r << 16) | ((uint32_t)src_g << 8) | (uint32_t)src_b;

    const uint8_t dst_r = (dst >> 16) & 0xFF;
    const uint8_t dst_g = (dst >> 8) & 0xFF;
    const uint8_t dst_b = dst & 0xFF;

    const uint32_t inv_sa = 255u - src_a;
    const uint8_t out_r = (uint8_t)(((uint32_t)src_r * src_a + (uint32_t)dst_r * inv_sa + 127u) / 255u);
    const uint8_t out_g = (uint8_t)(((uint32_t)src_g * src_a + (uint32_t)dst_g * inv_sa + 127u) / 255u);
    const uint8_t out_b = (uint8_t)(((uint32_t)src_b * src_a + (uint32_t)dst_b * inv_sa + 127u) / 255u);

    return (0xFFu << 24) | ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | (uint32_t)out_b;
}

// Decode one UTF-8 sequence into Unicode codepoint.
// Returns number of bytes consumed (>=1). On invalid sequence, returns 1 and sets codepoint to U+FFFD.
static int utf8_decode_1(const char *s, int *out_codepoint)
{
    const unsigned char c0 = (unsigned char)s[0];
    if(c0 == 0) { *out_codepoint = 0; return 0; }
    if(c0 < 0x80) { *out_codepoint = (int)c0; return 1; }

    // 2-byte
    if((c0 & 0xE0u) == 0xC0u) {
        const unsigned char c1 = (unsigned char)s[1];
        if((c1 & 0xC0u) != 0x80u) goto invalid;
        const int cp = ((int)(c0 & 0x1Fu) << 6) | (int)(c1 & 0x3Fu);
        if(cp < 0x80) goto invalid; // overlong
        *out_codepoint = cp;
        return 2;
    }

    // 3-byte
    if((c0 & 0xF0u) == 0xE0u) {
        const unsigned char c1 = (unsigned char)s[1];
        const unsigned char c2 = (unsigned char)s[2];
        if((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) goto invalid;
        const int cp = ((int)(c0 & 0x0Fu) << 12) | ((int)(c1 & 0x3Fu) << 6) | (int)(c2 & 0x3Fu);
        if(cp < 0x800) goto invalid; // overlong
        if(cp >= 0xD800 && cp <= 0xDFFF) goto invalid; // surrogate
        *out_codepoint = cp;
        return 3;
    }

    // 4-byte
    if((c0 & 0xF8u) == 0xF0u) {
        const unsigned char c1 = (unsigned char)s[1];
        const unsigned char c2 = (unsigned char)s[2];
        const unsigned char c3 = (unsigned char)s[3];
        if((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) goto invalid;
        const int cp = ((int)(c0 & 0x07u) << 18) | ((int)(c1 & 0x3Fu) << 12) | ((int)(c2 & 0x3Fu) << 6) | (int)(c3 & 0x3Fu);
        if(cp < 0x10000) goto invalid; // overlong
        if(cp > 0x10FFFF) goto invalid;
        *out_codepoint = cp;
        return 4;
    }

invalid:
    *out_codepoint = 0xFFFD;
    return 1;
}

void fbdraw_ttf_load_font(fbdraw_ttf_font_t * font, char* font_path){
    if(font == NULL){
        log_error("font is NULL");
        return;
    }
    FILE * font_file = fopen(font_path, "rb");
    if (font_file == NULL) {
        log_error("Failed to open font file: %s", font_path);
        return;
    }
    fseek(font_file, 0, SEEK_END);
    size_t size = ftell(font_file); /* how long is the file ? */
    fseek(font_file, 0, SEEK_SET); /* reset */

    font->data = malloc(size);
    if(font->data == NULL) {
        log_error("Failed to allocate memory for font data");
        fclose(font_file);
        return;
    }
    fread(font->data, size, 1, font_file);
    fclose(font_file);

    stbtt_InitFont(&font->font, font->data, stbtt_GetFontOffsetForIndex(font->data,0));

}

void fbdraw_ttf_free_font(fbdraw_ttf_font_t * font){
    if(font == NULL){
        log_error("font is NULL");
        return;
    }
    free(font->data);
    font->data = NULL;
}

void fbdraw_ttf_draw_text(fbdraw_fb_t* fb, fbdraw_rect_t* rect, fbdraw_ttf_font_t * font,char* text, float text_size_px, int text_color){
    if(!fb || !rect || !font || !text){
        log_error("fbdraw_ttf_draw_text: invalid args");
        return;
    }
    if(!fb->vaddr || fb->width <= 0 || fb->height <= 0) return;
    if(rect->w <= 0 || rect->h <= 0) return;
    if(text_size_px <= 0.0f) return;

    const int rect_x0 = rect->x;
    const int rect_y0 = rect->y;
    const int rect_x1 = rect->x + rect->w;
    const int rect_y1 = rect->y + rect->h;

    // text_color 是 ARGB8888。RGB 作为文字颜色；alpha 作为“全局透明度”乘到字形 coverage 上（若 alpha=255 则不影响）。
    const uint8_t tr = (uint8_t)((text_color >> 16) & 0xFF);
    const uint8_t tg = (uint8_t)((text_color >> 8) & 0xFF);
    const uint8_t tb = (uint8_t)(text_color & 0xFF);
    const uint8_t ta = (uint8_t)((text_color >> 24) & 0xFF);

    const float scale = stbtt_ScaleForPixelHeight(&font->font, text_size_px);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font->font, &ascent, &descent, &lineGap);

    int ascent_px = (int)ceilf((float)ascent * scale);
    int descent_px = (int)ceilf((float)(-descent) * scale); // descent 通常为负
    int line_gap_px = (int)ceilf((float)lineGap * scale);
    int line_height_px = ascent_px + descent_px + line_gap_px;
    if(line_height_px <= 0) line_height_px = (int)ceilf(text_size_px);
    if(ascent_px <= 0) ascent_px = (int)ceilf(text_size_px);

    int pen_x = rect_x0;
    int baseline_y = rect_y0 + ascent_px; // 顶边对齐：baseline = top + ascent

    // 首行就放不下：直接结束
    if(baseline_y + descent_px > rect_y1) return;

    int prev_cp = 0;
    const char *p = text;
    while(*p) {
        int cp = 0;
        const int n = utf8_decode_1(p, &cp);
        if(n <= 0) break;
        p += n;

        if(cp == '\r') continue;
        if(cp == '\n') {
            pen_x = rect_x0;
            baseline_y += line_height_px;
            prev_cp = 0;
            if(baseline_y + descent_px > rect_y1) break; // 超高停止绘制后续字符
            continue;
        }

        // kerning
        if(prev_cp != 0) {
            const int kern = stbtt_GetCodepointKernAdvance(&font->font, prev_cp, cp);
            if(kern != 0) pen_x += (int)floorf((float)kern * scale + 0.5f);
        }

        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        stbtt_GetCodepointBitmapBox(&font->font, cp, scale, scale, &ix0, &iy0, &ix1, &iy1);

        // 超宽自动换行：如果该字形右侧会超过 rect 右边界，并且当前不在行首，则先换行再尝试绘制
        if(pen_x != rect_x0 && (pen_x + ix1) > rect_x1) {
            pen_x = rect_x0;
            baseline_y += line_height_px;
            prev_cp = 0;
            if(baseline_y + descent_px > rect_y1) break;
            // 换行后重新获取 box（不含 kerning）
            stbtt_GetCodepointBitmapBox(&font->font, cp, scale, scale, &ix0, &iy0, &ix1, &iy1);
        }

        const int gw = ix1 - ix0;
        const int gh = iy1 - iy0;

        if(gw > 0 && gh > 0) {
            unsigned char *bmp = (unsigned char *)malloc((size_t)gw * (size_t)gh);
            if(!bmp) {
                log_error("fbdraw_ttf_draw_text: malloc glyph bitmap failed");
                return;
            }

            stbtt_MakeCodepointBitmap(&font->font, bmp, gw, gh, gw, scale, scale, cp);

            const int draw_x0 = pen_x + ix0;
            const int draw_y0 = baseline_y + iy0;

            for(int y = 0; y < gh; y++){
                const int dy = draw_y0 + y;
                if(dy < rect_y0 || dy >= rect_y1) continue;
                if(dy < 0 || dy >= fb->height) continue;
                for(int x = 0; x < gw; x++){
                    const int dx = draw_x0 + x;
                    if(dx < rect_x0 || dx >= rect_x1) continue;
                    if(dx < 0 || dx >= fb->width) continue;

                    uint8_t cov = bmp[y * gw + x];
                    if(ta != 255) cov = (uint8_t)(((uint32_t)cov * (uint32_t)ta + 127u) / 255u);
                    if(cov == 0) continue;

                    uint32_t *dst = fb->vaddr + dy * fb->width + dx;
                    *dst = argb8888_blend_over_opaque_dst(*dst, tr, tg, tb, cov);
                }
            }

            free(bmp);
        }

        int advanceWidth = 0, leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&font->font, cp, &advanceWidth, &leftSideBearing);
        pen_x += (int)floorf((float)advanceWidth * scale + 0.5f);

        prev_cp = cp;
    }
}
