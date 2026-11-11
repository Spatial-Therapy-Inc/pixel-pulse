// Drawing a 32 x 32 grid into an RGB565A8 image (LVGL's LV_COLOR_FORMAT_RGB565A8: the RGB565 plane, then one alpha
// byte a pixel), cell by cell at `cs` pixels a cell, with the face's state. Pure C: host tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sprites.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool blink;   // eyes closed: only their lowest row
    bool shut;    // the mouth closed (chewing, a chirp)
    bool flip;    // mirrored (spinning, a guest facing its host)
    int look;     // -1, 0, 1: the eyes a cell to the side
} rd_face_t;

typedef struct {
    pp_id_t id;
    bool egg;              // the egg's palette (cracks from `crack`)
    const uint8_t *crack;  // x, y pairs of cracked cells, or NULL
    int cracks;
    uint32_t mono;         // != 0: every cell this colour (an egg still waiting its turn, a silhouette)
} rd_look_t;

/** The image's bytes for w x h. */
static inline uint32_t rd_bytes(int w, int h) { return (uint32_t)w * (uint32_t)h * 3u; }
/** RGB565 of 0xRRGGBB. */
uint16_t rd_565(uint32_t rgb);
/** Draws `g` centred in a w x h image (cleared first; the grid is 32 * cs square, w and h at least that). */
void rd_grid(uint8_t *img, int w, int h, const sp_grid_t *g, int cs, const rd_look_t *look, const rd_face_t *face);
/** Draws a small mask ('X' and other letters, '.' empty) into an image at cell size cs, letters coloured by
 *  `colors` (pairs: letter, 0xRRGGBB). Used for hearts, stars, notes, icons. */
void rd_mask(uint8_t *img, int w, int h, const char *const *rows, int nrows, int cs, const char *letters,
             const uint32_t *colors);

#ifdef __cplusplus
}
#endif
