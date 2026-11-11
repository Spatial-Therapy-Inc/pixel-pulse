// Pixel Pulse's sounds, all synthesised (no clips): each companion's chirp, the grain's tick, the egg's crack, a
// caught star, and the little fanfare when it hatches, grows up or dresses. Through yaykit_speak's effects (yk_sfx_*),
// so they follow the device's volume.
#pragma once

#include <stdbool.h>

#include "pet.h"

typedef enum {
    SOUND_CHIRP,     // the companion's own voice (pp_id_t picks it)
    SOUND_TICK,      // a grain falls (the rim's step)
    SOUND_CRACK,     // a tap on the egg
    SOUND_STAR,      // a star caught
    SOUND_HEART,     // a heart caught (three stars)
    SOUND_FANFARE,   // hatched, grown up, dressed
    SOUND_REFUSE,    // full: it turns its head away
    SOUND_YAWN,      // picked up after a sleep
} sound_t;

void sound_init(void);
void sound_play(sound_t s, pp_id_t who);
/** In a game sounds come any moment: keep the amplifier on. */
void sound_hold(bool on);
void sound_stop_all(void);
