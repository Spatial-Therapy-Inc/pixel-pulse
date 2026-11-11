// Pixel Pulse's rules (design v0.4, "怎么养"): four companions raised from an egg. Pure C, no ESP-IDF: host tested
// (test/test_pet.c). The firmware feeds it the touches, the motion and the clock, and draws what it says.
//
//   egg      10 taps hatch it
//   baby     40 care points grow it up (and the next egg appears)
//   grown    80 more put on its outfit
//   dressed  the last stage
//
// Care points count only when the companion needed it (fed while not full, played while not merry, slept while
// tired), so they cannot be farmed; they never go down. Nothing is punished: no death, no running away; a companion
// left alone only naps.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PP_PIP = 0, PP_MOTE, PP_DIAL, PP_BLOOM, PP_PETS } pp_id_t;
typedef enum { PP_LOCKED = 0, PP_EGG, PP_BABY, PP_GROWN, PP_DRESSED } pp_stage_t;
typedef enum { PP_NEED_NONE = 0, PP_NEED_FOOD, PP_NEED_PLAY, PP_NEED_SLEEP } pp_need_t;
typedef enum { PP_EV_NONE = 0, PP_EV_REFUSED, PP_EV_DONE, PP_EV_CARE, PP_EV_HATCHED, PP_EV_GREW, PP_EV_DRESSED } pp_event_t;

#define PP_EGG_TAPS 10
#define PP_BABY_PTS 40
#define PP_DRESS_PTS 80
#define PP_FULL 1000             // the meters are permille
#define PP_EMPTY_S (12 * 3600)   // full to empty: food and joy
#define PP_TIRED_S (14 * 3600)   // energy
#define PP_NEED_BELOW 350        // a bubble below this
#define PP_NAP_BELOW 150         // food and joy both below this: it naps
#define PP_LOVE_STEP 10          // the heart grows every 10 touches, up to 3 sizes

typedef struct {
    uint8_t stage;   // pp_stage_t
    uint8_t taps;    // egg taps
    uint16_t care;   // care points since hatching
    uint16_t love;   // touches
    uint16_t full, joy, energy;  // permille
} pp_pet_t;

typedef struct {
    uint8_t version;  // PP_SAVE_VERSION
    uint8_t first;    // the companion picked in the app, the first egg
    pp_pet_t pet[PP_PETS];
    int64_t clock_s;  // wall clock of the last decay (0: none yet)
} pp_save_t;

#define PP_SAVE_VERSION 1

/** A new save: `first` is an egg, the others wait. */
void pp_init(pp_save_t *s, pp_id_t first);
/** A save read back: false when it is not one of ours (then pp_init it). Clamps what is out of range. */
bool pp_valid(pp_save_t *s);
/** The companion for a buddy name ("Pip", "Mote", "Dial", "Bloom", any case); Pip for anything else. */
pp_id_t pp_from_name(const char *name);
const char *pp_name(pp_id_t id);

/** Time passing on the wall clock: the meters go down. A clock going back or a first reading only sets it. */
void pp_decay_to(pp_save_t *s, int64_t now_s);
/** The same for a span of seconds (the prototype's "4 hours"). */
void pp_decay(pp_save_t *s, int64_t seconds);

bool pp_alive(const pp_pet_t *p);   // baby, grown or dressed: it eats and plays
bool pp_big(const pp_pet_t *p);     // grown or dressed: 320 px, may visit
pp_need_t pp_need(const pp_pet_t *p);
bool pp_napping(const pp_pet_t *p);
int pp_heart_size(const pp_pet_t *p);  // 1..3

/** A tap on the egg: PP_EV_CARE (one crack more) or PP_EV_HATCHED. */
pp_event_t pp_tap_egg(pp_save_t *s, pp_id_t id);
/** A touch: love + 1, joy + 5 %. Wakes a napping one (joy to at least 30 %). */
pp_event_t pp_touch(pp_save_t *s, pp_id_t id);
/** One grain: +10 % food and a care point; PP_EV_REFUSED when full. */
pp_event_t pp_feed(pp_save_t *s, pp_id_t id);
/** A shake: +25 % joy; a care point when it was not merry already (else PP_EV_DONE). */
pp_event_t pp_shake(pp_save_t *s, pp_id_t id);
/** Picked up after a sleep face down: energy full; 2 points when it was tired. */
pp_event_t pp_sleep(pp_save_t *s, pp_id_t id);
/** A round of catching stars: +30 % joy, 3 points. */
pp_event_t pp_game(pp_save_t *s, pp_id_t id);
/** A visit: a point each. Returns the host's event; *guest_ev the guest's. */
pp_event_t pp_visit(pp_save_t *s, pp_id_t host, pp_id_t guest, pp_event_t *guest_ev);

/** Care points straight (used by the ones above): PP_EV_CARE, PP_EV_GREW (next egg unlocked) or PP_EV_DRESSED. */
pp_event_t pp_add_care(pp_save_t *s, pp_id_t id, int n);

/** Towards the next stage, permille (egg: taps; dressed: 1000). */
int pp_progress(const pp_pet_t *p);
/** Points left to the next stage (0 for egg, locked and dressed). */
int pp_to_next(const pp_pet_t *p);
/** Another grown companion to visit `host`, or -1. `pick` chooses among several (any number). */
int pp_visitor(const pp_save_t *s, pp_id_t host, unsigned pick);
/** The egg that comes next after one grows up: Pip, Mote, Dial, Bloom, skipping the ones there; -1 when none. */
int pp_next_locked(const pp_save_t *s);

#ifdef __cplusplus
}
#endif
