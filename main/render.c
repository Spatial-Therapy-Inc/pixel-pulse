#include "render.h"

#include <string.h>

uint16_t rd_565(uint32_t rgb) {
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void put_cell(uint8_t *img, int w, int h, int x0, int y0, int cs, uint16_t c) {
    uint16_t *px = (uint16_t *)img;
    uint8_t *alpha = img + (size_t)w * h * 2;
    // a 1 px gap from 8 px up: the pixels read as tiles, as on the design page
    const int s = cs >= 8 ? cs - 1 : cs;
    for (int y = y0; y < y0 + s; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = x0; x < x0 + s; x++) {
            if (x < 0 || x >= w) continue;
            px[y * w + x] = c;
            alpha[y * w + x] = 0xFF;
        }
    }
}

static bool is_eye(char c) { return c == 'k' || c == 'e'; }

void rd_grid(uint8_t *img, int w, int h, const sp_grid_t *g, int cs, const rd_look_t *look, const rd_face_t *face) {
    memset(img, 0, rd_bytes(w, h));
    const int ox = (w - SP_G * cs) / 2, oy = (h - SP_G * cs) / 2;
    const rd_face_t none = {0};
    if (!face) face = &none;
    const uint32_t base = sp_color(look->id, g->base, look->egg);
    for (int y = 0; y < SP_G; y++)
        for (int x = 0; x < SP_G; x++) {
            const int sx = face->flip ? SP_G - 1 - x : x;
            const char ch = g->cell[y][sx];
            if (ch == '.') continue;
            uint32_t col;
            if (look->mono) {
                col = look->mono;
            } else if (is_eye(ch)) {
                const bool below = y + 1 < SP_G && is_eye(g->cell[y + 1][sx]);
                col = (face->blink && below) || (!face->blink && face->look) ? base : sp_color(look->id, 'k', false);
                if (!face->blink && !face->look) col = sp_color(look->id, ch, false);
            } else if (ch == 'm' && face->shut) {
                col = base;
            } else {
                col = sp_color(look->id, ch, look->egg);
                if (look->egg && look->crack)
                    for (int i = 0; i < look->cracks; i++)
                        if (look->crack[2 * i] == sx && look->crack[2 * i + 1] == y) col = sp_color(look->id, 'k', false);
            }
            if (col == SP_NONE) continue;
            put_cell(img, w, h, ox + x * cs, oy + y * cs, cs, rd_565(col));
        }
    if (look->mono || !face->look || face->blink) return;
    // looking aside: the eyes again, a cell over
    for (int y = 0; y < SP_G; y++)
        for (int x = 0; x < SP_G; x++) {
            const int sx = face->flip ? SP_G - 1 - x : x;
            const char ch = g->cell[y][sx];
            if (!is_eye(ch)) continue;
            put_cell(img, w, h, ox + (x + face->look) * cs, oy + y * cs, cs, rd_565(sp_color(look->id, ch, false)));
        }
}

void rd_mask(uint8_t *img, int w, int h, const char *const *rows, int nrows, int cs, const char *letters,
             const uint32_t *colors) {
    memset(img, 0, rd_bytes(w, h));
    const int cols = (int)strlen(rows[0]);
    const int ox = (w - cols * cs) / 2, oy = (h - nrows * cs) / 2;
    for (int y = 0; y < nrows; y++)
        for (int x = 0; x < cols; x++) {
            const char ch = rows[y][x];
            if (ch == '.' || ch == ' ') continue;
            const char *p = strchr(letters, ch);
            if (!p) continue;
            put_cell(img, w, h, ox + x * cs, oy + y * cs, cs, rd_565(colors[p - letters]));
        }
}
