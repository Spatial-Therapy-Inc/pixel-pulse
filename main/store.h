// The four companions' state in this app's own NVS namespace (yk_pixelpulse): a few dozen bytes, written when
// something changed (at most every 20 s), when the screen goes off and when leaving.
#pragma once

#include <stdbool.h>

#include "pet.h"

/** Reads the save; false when there is none (or not ours): then it is pp_init()ed with `first`. */
bool store_load(pp_save_t *s, pp_id_t first);
void store_save(const pp_save_t *s);
