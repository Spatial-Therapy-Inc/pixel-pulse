#include "pet.h"

#include <ctype.h>
#include <string.h>

static const char *const NAMES[PP_PETS] = {"Pip", "Mote", "Dial", "Bloom"};

const char *pp_name(pp_id_t id) { return id < PP_PETS ? NAMES[id] : NAMES[0]; }

pp_id_t pp_from_name(const char *name) {
    if (!name) return PP_PIP;
    for (int i = 0; i < PP_PETS; i++) {
        const char *a = name, *b = NAMES[i];
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
        if (!*a && !*b) return (pp_id_t)i;
    }
    return PP_PIP;
}

static void fresh(pp_pet_t *p, pp_stage_t stage) {
    memset(p, 0, sizeof(*p));
    p->stage = (uint8_t)stage;
    p->full = p->joy = p->energy = PP_FULL;
}

void pp_init(pp_save_t *s, pp_id_t first) {
    memset(s, 0, sizeof(*s));
    s->version = PP_SAVE_VERSION;
    s->first = (uint8_t)(first < PP_PETS ? first : PP_PIP);
    for (int i = 0; i < PP_PETS; i++) fresh(&s->pet[i], i == s->first ? PP_EGG : PP_LOCKED);
}

static uint16_t clamp_m(int v) { return (uint16_t)(v < 0 ? 0 : v > PP_FULL ? PP_FULL : v); }

bool pp_valid(pp_save_t *s) {
    if (s->version != PP_SAVE_VERSION || s->first >= PP_PETS) return false;
    bool any = false;
    for (int i = 0; i < PP_PETS; i++) {
        pp_pet_t *p = &s->pet[i];
        if (p->stage > PP_DRESSED) return false;
        if (p->stage != PP_LOCKED) any = true;
        if (p->taps > PP_EGG_TAPS) p->taps = PP_EGG_TAPS;
        p->full = clamp_m(p->full), p->joy = clamp_m(p->joy), p->energy = clamp_m(p->energy);
    }
    return any;
}

bool pp_alive(const pp_pet_t *p) { return p->stage >= PP_BABY; }
bool pp_big(const pp_pet_t *p) { return p->stage >= PP_GROWN; }

static void drop(pp_save_t *s, int food, int tired) {
    for (int i = 0; i < PP_PETS; i++) {
        pp_pet_t *p = &s->pet[i];
        if (!pp_alive(p)) continue;
        p->full = clamp_m(p->full - food);
        p->joy = clamp_m(p->joy - food);
        p->energy = clamp_m(p->energy - tired);
    }
}

void pp_decay(pp_save_t *s, int64_t seconds) {
    if (seconds <= 0) return;
    if (seconds > 30LL * 24 * 3600) seconds = 30LL * 24 * 3600;
    drop(s, (int)(seconds * PP_FULL / PP_EMPTY_S), (int)(seconds * PP_FULL / PP_TIRED_S));
}

void pp_decay_to(pp_save_t *s, int64_t now_s) {
    if (s->clock_s > 0 && now_s > s->clock_s) {
        // whole permille on the absolute clock, so readings every few seconds add up exactly
        int64_t from = s->clock_s;
        if (now_s - from > 30LL * 24 * 3600) from = now_s - 30LL * 24 * 3600;
        const int food = (int)(now_s * PP_FULL / PP_EMPTY_S - from * PP_FULL / PP_EMPTY_S);
        const int tired = (int)(now_s * PP_FULL / PP_TIRED_S - from * PP_FULL / PP_TIRED_S);
        drop(s, food, tired);
    }
    s->clock_s = now_s;  // also a first reading, or a clock that went back
}

pp_need_t pp_need(const pp_pet_t *p) {
    if (!pp_alive(p)) return PP_NEED_NONE;
    pp_need_t n = PP_NEED_FOOD;
    int v = p->full;
    if (p->joy < v) n = PP_NEED_PLAY, v = p->joy;
    if (p->energy < v) n = PP_NEED_SLEEP, v = p->energy;
    return v < PP_NEED_BELOW ? n : PP_NEED_NONE;
}

bool pp_napping(const pp_pet_t *p) { return pp_alive(p) && p->full < PP_NAP_BELOW && p->joy < PP_NAP_BELOW; }

int pp_heart_size(const pp_pet_t *p) {
    const int n = 1 + p->love / PP_LOVE_STEP;
    return n > 3 ? 3 : n;
}

int pp_next_locked(const pp_save_t *s) {
    for (int i = 0; i < PP_PETS; i++)
        if (s->pet[i].stage == PP_LOCKED) return i;
    return -1;
}

pp_event_t pp_add_care(pp_save_t *s, pp_id_t id, int n) {
    pp_pet_t *p = &s->pet[id];
    if (!pp_alive(p) || n <= 0) return PP_EV_NONE;
    p->care = (uint16_t)(p->care + n > 60000 ? 60000 : p->care + n);
    if (p->stage == PP_BABY && p->care >= PP_BABY_PTS) {
        p->stage = PP_GROWN;
        const int next = pp_next_locked(s);
        if (next >= 0) {
            fresh(&s->pet[next], PP_EGG);
        }
        return PP_EV_GREW;
    }
    if (p->stage == PP_GROWN && p->care >= PP_BABY_PTS + PP_DRESS_PTS) {
        p->stage = PP_DRESSED;
        return PP_EV_DRESSED;
    }
    return PP_EV_CARE;
}

pp_event_t pp_tap_egg(pp_save_t *s, pp_id_t id) {
    pp_pet_t *p = &s->pet[id];
    if (p->stage != PP_EGG) return PP_EV_NONE;
    if (++p->taps < PP_EGG_TAPS) return PP_EV_CARE;
    p->stage = PP_BABY;
    p->care = 0;
    p->full = p->joy = p->energy = PP_FULL * 8 / 10;
    return PP_EV_HATCHED;
}

pp_event_t pp_touch(pp_save_t *s, pp_id_t id) {
    pp_pet_t *p = &s->pet[id];
    if (!pp_alive(p)) return PP_EV_NONE;
    if (pp_napping(p)) {
        if (p->joy < 300) p->joy = 300;
        return PP_EV_DONE;
    }
    if (p->love < 60000) p->love++;
    p->joy = clamp_m(p->joy + 50);
    return PP_EV_DONE;
}

pp_event_t pp_feed(pp_save_t *s, pp_id_t id) {
    pp_pet_t *p = &s->pet[id];
    if (!pp_alive(p)) return PP_EV_NONE;
    if (p->full >= PP_FULL - 20) return PP_EV_REFUSED;
    p->full = clamp_m(p->full + 100);
    return pp_add_care(s, id, 1);
}

pp_event_t pp_shake(pp_save_t *s, pp_id_t id) {
    pp_pet_t *p = &s->pet[id];
    if (!pp_alive(p)) return PP_EV_NONE;
    const bool needed = p->joy < PP_FULL - 10;
    p->joy = clamp_m(p->joy + 250);
    return needed ? pp_add_care(s, id, 1) : PP_EV_DONE;
}

pp_event_t pp_sleep(pp_save_t *s, pp_id_t id) {
    pp_pet_t *p = &s->pet[id];
    if (!pp_alive(p)) return PP_EV_NONE;
    const bool needed = p->energy < PP_FULL - 10;
    p->energy = PP_FULL;
    return needed ? pp_add_care(s, id, 2) : PP_EV_DONE;
}

pp_event_t pp_game(pp_save_t *s, pp_id_t id) {
    pp_pet_t *p = &s->pet[id];
    if (!pp_alive(p)) return PP_EV_NONE;
    p->joy = clamp_m(p->joy + 300);
    return pp_add_care(s, id, 3);
}

pp_event_t pp_visit(pp_save_t *s, pp_id_t host, pp_id_t guest, pp_event_t *guest_ev) {
    s->pet[host].joy = clamp_m(s->pet[host].joy + 100);
    s->pet[guest].joy = clamp_m(s->pet[guest].joy + 100);
    const pp_event_t g = pp_add_care(s, guest, 1);
    if (guest_ev) *guest_ev = g;
    return pp_add_care(s, host, 1);
}

int pp_progress(const pp_pet_t *p) {
    switch (p->stage) {
    case PP_EGG: return p->taps * PP_FULL / PP_EGG_TAPS;
    case PP_BABY: return p->care >= PP_BABY_PTS ? PP_FULL : p->care * PP_FULL / PP_BABY_PTS;
    case PP_GROWN: {
        const int c = p->care - PP_BABY_PTS;
        return c <= 0 ? 0 : c >= PP_DRESS_PTS ? PP_FULL : c * PP_FULL / PP_DRESS_PTS;
    }
    case PP_DRESSED: return PP_FULL;
    default: return 0;
    }
}

int pp_to_next(const pp_pet_t *p) {
    if (p->stage == PP_BABY) return p->care >= PP_BABY_PTS ? 0 : PP_BABY_PTS - p->care;
    if (p->stage == PP_GROWN) {
        const int left = PP_BABY_PTS + PP_DRESS_PTS - p->care;
        return left < 0 ? 0 : left;
    }
    return 0;
}

int pp_visitor(const pp_save_t *s, pp_id_t host, unsigned pick) {
    int ids[PP_PETS], n = 0;
    for (int i = 0; i < PP_PETS; i++)
        if (i != (int)host && pp_big(&s->pet[i])) ids[n++] = i;
    return n ? ids[pick % (unsigned)n] : -1;
}
