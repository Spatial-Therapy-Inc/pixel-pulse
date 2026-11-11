// The companions' pixel art (design v0.4, "四个伙伴"): each is a few shapes in a 24-unit box, sampled onto a 32 x 32
// grid at start, an edge shade along the lower right, the face on top, the eyes as fixed 2 x 3 blocks with a
// catch-light. Pure C (host tested): the same shapes as the design page's prototype, so the device matches it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SP_G 32
#define SP_NONE 0xFFFFFFFFu  // sp_color(): this cell is empty

typedef struct {
    char cell[SP_G][SP_G];  // '.' empty, else a palette letter (sp_color)
    int top, bottom;        // first and last rows with anything
    char base;              // the body's letter: closed eyes and a shut mouth take it
} sp_grid_t;

/** A companion as it looks (dressed: with its outfit). */
void sp_build(pp_id_t id, bool dressed, sp_grid_t *out);
/** The egg ('c' spots take the companion's colour: sp_color(id, 'c') with egg = true). */
void sp_build_egg(sp_grid_t *out);
/** 0xRRGGBB for a letter of this companion's picture (egg: the egg's palette); SP_NONE for '.' or unknown. */
uint32_t sp_color(pp_id_t id, char ch, bool egg);
/** The egg's crack, cell by cell from the top: x, y pairs into `xy` (2 * max). Returns how many. */
int sp_crack(uint8_t *xy, int max);
/** The outfit's name, for what the buddy says ("耳机" / "headphones"). */
const char *sp_outfit(pp_id_t id, bool english);

#ifdef __cplusplus
}
#endif
