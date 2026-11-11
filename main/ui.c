#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "pet.h"
#include "render.h"
#include "sound.h"
#include "sprites.h"
#include "store.h"
#include "yaykit_app.h"
#include "yaykit_ui.h"
#include "yk_hint.h"
#include "yk_rim.h"
#include "yk_round.h"
#include "lvgl_private.h"  // lv_image_cache_drop: an image redrawn in place

#define C YK_CX
#define GROUND (C + 160)   // the ground's top: feet stand here
#define TICK_MS 33
#define GAME_MS 20000
#define VISIT_MS 30000
#define VISIT_DELAY_MS 10000
#define SLEEP_MIN_MS 10000  // face down at least this long counts as a sleep
#define SAVE_EVERY_MS 120000  // review F-06: flash writes minutes apart
#define PARTS 12
#define ITEMS 8

// ---------------------------------------------------------------- state

static pp_save_t S;
static int s_cur;
static bool s_en;
static bool s_dirty;
static int64_t s_saved_ms;
static bool s_touched;  // anything happened this time: the buddy gets a sentence

static sp_grid_t s_grid[PP_PETS][2], s_egg;
static uint8_t s_crack[128];
static int s_crack_n;

typedef struct {
    lv_image_dsc_t dsc;
    uint8_t *buf;
    lv_obj_t *obj;
    struct {  // what was drawn last, to draw only on a change
        int id, cs, cracks;
        const sp_grid_t *g;
        rd_face_t face;
        uint32_t mono;
        bool egg;
    } last;
} pic_t;
static pic_t s_main, s_guest_pic, s_mini;

typedef enum { PK_HEART, PK_STAR, PK_PELLET, PK_NOTE, PK_Z } part_kind_t;
typedef struct {
    lv_obj_t *obj;
    bool on;
    part_kind_t k;
    float x, y, ty;
    int64_t t0;
} part_t;
static part_t s_parts[PARTS];

static lv_image_dsc_t s_heart[3], s_star3, s_star5, s_pellet, s_note, s_z, s_icon[3], s_big_heart;

static lv_obj_t *s_screen, *s_ground, *s_shadow, *s_bubble, *s_bubble_icon, *s_trail[3], *s_name, *s_score_label;
static lv_obj_t *s_dots[PP_PETS], *s_pulse;
static yk_rim_t *s_rim;

// the status page
static lv_obj_t *s_page, *s_page_title, *s_page_icon[3], *s_bar[3][10], *s_page_heart, *s_page_text;

// animation
static int64_t s_bounce_at = -100000, s_spin_at = -100000, s_refuse_at = -100000, s_chomp_at = -100000;
static int64_t s_wobble_at = -100000, s_switch_at = -100000, s_grow_at = -100000, s_look_until, s_label_until;
static int64_t s_ring_until, s_last_z;
static int s_look;
static float s_pet_x;
static char s_label[48];

// modes
static bool s_status, s_asleep, s_face_down;
static int64_t s_face_at, s_input_ms;
static bool s_game;
static int64_t s_game_t0, s_next_spawn;
static int s_score;
static struct {
    lv_obj_t *obj;
    bool on, heart;
    float x;
    int64_t t0;
} s_items[ITEMS];
static bool s_drag;
static float s_drag_x;
static bool s_visit;
static int s_guest;
static int64_t s_visit_t0, s_visit_at = -1, s_guest_bounce = -100000, s_guest_spin = -100000;
static bool s_visit_heart;

// touch
static struct {
    bool active, moved, dialing;
    int x0, y0;
    float last_deg, acc_deg;
} s_press;

// flags from other tasks
#define F_TAP 1u
#define F_SHAKE 2u
#define F_DOWN 4u
#define F_UP 8u
#define F_LIFT 16u
static volatile uint32_t s_flags;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static pp_pet_t *cur_pet(void) { return &S.pet[s_cur]; }
static const char *T(const char *zh, const char *en) { return s_en ? en : zh; }

static uint32_t mix(uint32_t a, uint32_t b, int pct) {  // pct of a, the rest b
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (uint32_t)((ca * pct + cb * (100 - pct)) / 100) << sh;
    }
    return out;
}

// ---------------------------------------------------------------- images

static void *ps_alloc(size_t n) {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
}

static void dsc_init(lv_image_dsc_t *d, uint8_t *buf, int w, int h) {
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565A8;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->header.stride = (uint32_t)w * 2;
    d->data_size = rd_bytes(w, h);
    d->data = buf;
}

static void make_mask(lv_image_dsc_t *d, const char *const *rows, int n, int cs, const char *letters,
                      const uint32_t *colors) {
    const int w = (int)strlen(rows[0]) * cs, h = n * cs;
    uint8_t *b = ps_alloc(rd_bytes(w, h));
    rd_mask(b, w, h, rows, n, cs, letters, colors);
    dsc_init(d, b, w, h);
}

static void make_art(void) {
    static const char *const heart[] = {".X.X.", "XXXXX", "XXXXX", ".XXX.", "..X.."};
    static const char *const star3[] = {".X.", "XXX", ".X."};
    static const char *const star5[] = {"..X..", ".XXX.", "XXXXX", ".XXX.", ".X.X."};
    static const char *const pellet[] = {"XX", "XX"};
    static const char *const note[] = {"...XX", "...XX", "...X.", "...X.", ".XXX.", "XXXX.", ".XX.."};
    static const char *const zz[] = {"XXX", "..X", ".X.", "X..", "XXX"};
    static const char *const food[] = {"..www..", ".wwwww.", "yyyyyyy", "yyyyyyy", ".yyyyy.", "..yyy.."};
    static const char *const play[] = {"..ooo..", ".ooooo.", "oowwooo", "ooowwoo", ".ooooo.", "..ooo.."};
    static const char *const sleep[] = {"..www..", ".ww....", "ww.....", "ww.....", ".ww....", "..www.."};
    static const char *const big[] = {"...XXX...XXX...", "..XHHXX.XXXXX..", ".XXHXXXXXXXXXX.", "XXXXXXXXXXXXXXX",
                                      "XXXXXXXXXXXXXXX", "XXXXXXXXXXXXXXX", ".XXXXXXXXXXXXX.", "..XXXXXXXXXXXT.",
                                      "...TXXXXXXXXT..", "....TTXXXXXT...", ".....TTXXTT....", "......TTT......",
                                      ".......T.......", };
    const uint32_t coral = YK_CORAL, cream = YK_CREAM, amber = YK_AMBER;
    for (int i = 0; i < 3; i++) make_mask(&s_heart[i], heart, 5, 6 + i * 2, "X", &coral);
    make_mask(&s_star3, star3, 3, 7, "X", &amber);
    make_mask(&s_star5, star5, 5, 6, "X", &amber);
    make_mask(&s_pellet, pellet, 2, 6, "X", &cream);
    make_mask(&s_note, note, 7, 5, "X", &cream);
    make_mask(&s_z, zz, 5, 5, "X", &cream);
    const uint32_t icon_cols[] = {cream, amber, coral};
    make_mask(&s_icon[PP_NEED_FOOD - 1], food, 6, 7, "wyo", icon_cols);
    make_mask(&s_icon[PP_NEED_PLAY - 1], play, 6, 7, "wyo", icon_cols);
    make_mask(&s_icon[PP_NEED_SLEEP - 1], sleep, 6, 7, "wyo", icon_cols);
    const uint32_t big_cols[] = {coral, cream, mix(YK_CORAL, YK_NIGHT, 78)};
    make_mask(&s_big_heart, big, 13, 24, "XHT", big_cols);
}

static void pic_init(pic_t *p, lv_obj_t *parent, int max_cs) {
    const int w = SP_G * max_cs;
    p->buf = ps_alloc(rd_bytes(w, w));
    dsc_init(&p->dsc, p->buf, w, w);
    p->obj = lv_image_create(parent);
    lv_obj_set_clickable(p->obj, false);
    p->last.cs = -1;
}

/** Draws the grid when anything about it changed; returns the image's side. */
static int pic_draw(pic_t *p, int id, const sp_grid_t *g, int cs, const rd_look_t *lk, const rd_face_t *f) {
    const int w = SP_G * cs;
    const rd_face_t face = f ? *f : (rd_face_t){0};
    if (p->last.cs == cs && p->last.id == id && p->last.g == g && p->last.mono == lk->mono && p->last.egg == lk->egg &&
        p->last.cracks == lk->cracks && !memcmp(&p->last.face, &face, sizeof(face)))
        return w;
    rd_grid(p->buf, w, w, g, cs, lk, &face);
    dsc_init(&p->dsc, p->buf, w, w);
    lv_image_cache_drop(&p->dsc);
    lv_image_set_src(p->obj, &p->dsc);
    lv_obj_invalidate(p->obj);
    p->last.cs = cs, p->last.id = id, p->last.g = g, p->last.mono = lk->mono, p->last.egg = lk->egg;
    p->last.cracks = lk->cracks, p->last.face = face;
    return w;
}

// ---------------------------------------------------------------- what the companion looks like now

static int cs_of(const pp_pet_t *p) { return p->stage == PP_BABY ? 6 : pp_big(p) ? 10 : 7; }
static const sp_grid_t *grid_of(int id) {
    const pp_pet_t *p = &S.pet[id];
    if (p->stage == PP_EGG || p->stage == PP_LOCKED) return &s_egg;
    return &s_grid[id][p->stage == PP_DRESSED];
}
/** The image's centre y that puts the feet on the ground. */
static int foot_cy(const sp_grid_t *g, int cs) { return GROUND - (g->bottom + 1 - SP_G / 2) * cs; }
static int head_y(const sp_grid_t *g, int cs, int cy) { return cy - (SP_G / 2 - g->top) * cs; }
static int side_of(int id) { return id == PP_MOTE ? 1 : -1; }  // where the bubble goes: away from leaves and tails

static rd_look_t look_of(int id) {
    const pp_pet_t *p = &S.pet[id];
    rd_look_t lk = {.id = (pp_id_t)id};
    if (p->stage == PP_LOCKED) {
        lk.mono = YK_PANEL;
        lk.egg = true;
    } else if (p->stage == PP_EGG) {
        lk.egg = true;
        lk.crack = s_crack;
        lk.cracks = p->taps * s_crack_n / PP_EGG_TAPS;
    }
    return lk;
}

// ---------------------------------------------------------------- particles

static void part_add(part_kind_t k, float x, float y, int64_t t0, float ty) {
    part_t *slot = &s_parts[0];
    for (int i = 0; i < PARTS; i++) {
        if (!s_parts[i].on) {
            slot = &s_parts[i];
            break;
        }
        if (s_parts[i].t0 < slot->t0) slot = &s_parts[i];  // full: the oldest gives way
    }
    *slot = (part_t){.obj = slot->obj, .on = true, .k = k, .x = x, .y = y, .ty = ty, .t0 = t0};
    const lv_image_dsc_t *d = k == PK_STAR ? &s_star3 : k == PK_PELLET ? &s_pellet : k == PK_NOTE ? &s_note
                            : k == PK_Z    ? &s_z
                                           : &s_heart[pp_heart_size(cur_pet()) - 1];
    lv_image_set_src(slot->obj, d);
    lv_obj_set_hidden(slot->obj, true);
}

static void parts_tick(int64_t now) {
    for (int i = 0; i < PARTS; i++) {
        part_t *q = &s_parts[i];
        if (!q->on) continue;
        const float e = (float)(now - q->t0);
        if (e < 0) continue;
        const int life = q->k == PK_PELLET ? 450 : q->k == PK_Z ? 2000 : 1100;
        if (e >= life) {
            q->on = false;
            lv_obj_set_hidden(q->obj, true);
            continue;
        }
        float x = q->x, y = q->y;
        switch (q->k) {
        case PK_HEART: y -= e * 0.09f; break;
        case PK_STAR: y -= e * 0.02f; break;
        case PK_NOTE: y -= e * 0.07f, x += sinf(e * 0.008f) * 6; break;
        case PK_Z: y -= e * 0.03f, x += e * 0.015f; break;
        case PK_PELLET: {
            const float f = e / 450.0f;
            y = q->y + (q->ty - q->y) * f * f;
            break;
        }
        }
        const lv_image_dsc_t *d = lv_image_get_src(q->obj);
        lv_obj_set_pos(q->obj, (int)x - (int)d->header.w / 2, (int)y - (int)d->header.h / 2);
        lv_obj_set_style_opa(q->obj, q->k == PK_PELLET ? LV_OPA_COVER : (lv_opa_t)(255 * (1 - e / life)), 0);
        lv_obj_set_hidden(q->obj, false);
    }
}

// ---------------------------------------------------------------- ground, rim, labels

static void draw_ground(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    static const int pct[3][2] = {{38, 30}, {24, 18}, {12, 8}};  // the panel colour into the paper, row by row
    for (int i = 0; i < 3; i++) {
        const int y = GROUND + i * 10;
        const float dy = (float)(y + 5 - C);
        const float half = sqrtf(fmaxf(0, 226.0f * 226.0f - dy * dy));
        for (int x = C - (int)(half / 10) * 10; x < C + half - 10; x += 10) {
            d.bg_color = lv_color_hex(mix(YK_PANEL, YK_PAPER, pct[i][((x / 10) + i) & 1]));
            const lv_area_t a = {x + 1, y + 1, x + 9, y + 9};
            lv_draw_rect(layer, &d, &a);
        }
    }
}

static void rim_progress(int permille) {
    yk_rim_set(s_rim, YK_RIM_PROGRESS, NULL);
    yk_rim_value(s_rim, permille);
    s_ring_until = now_ms() + 2300;
}

static void show_label(const char *text, int ms) {
    snprintf(s_label, sizeof(s_label), "%s", text);
    lv_label_set_text(s_name, s_label);
    s_label_until = now_ms() + ms;
}

static void show_dots(void) {
    for (int i = 0; i < PP_PETS; i++) {
        lv_obj_t *d = s_dots[i];
        const bool locked = S.pet[i].stage == PP_LOCKED;
        lv_obj_set_style_bg_opa(d, locked ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(i == s_cur ? YK_INK2 : YK_PANEL), 0);
        lv_obj_set_style_border_width(d, locked ? 1 : 0, 0);
    }
}

static void hide_bubble(void) {
    lv_obj_set_hidden(s_bubble, true);
    for (int i = 0; i < 3; i++) lv_obj_set_hidden(s_trail[i], true);
}

static const char *stage_name(int st) {
    static const char *const zh[] = {"还没轮到", "蛋", "宝宝", "长大", "装扮"};
    static const char *const en[] = {"Not yet", "Egg", "Baby", "Grown", "Dressed"};
    return (s_en ? en : zh)[st];
}

static void to_next_text(const pp_pet_t *p, char *out, size_t n) {
    switch (p->stage) {
    case PP_BABY: snprintf(out, n, s_en ? "%d more to grow up" : "再 %d 点长大", pp_to_next(p)); break;
    case PP_GROWN: snprintf(out, n, s_en ? "%d more to dress up" : "再 %d 点换装扮", pp_to_next(p)); break;
    case PP_DRESSED: snprintf(out, n, "%s", T("打扮好了", "All dressed up")); break;
    case PP_EGG: snprintf(out, n, s_en ? "%d more taps" : "还要点 %d 下", PP_EGG_TAPS - p->taps); break;
    default: snprintf(out, n, "%s", T("先养大前一只", "Raise the one before")); break;
    }
}

// ---------------------------------------------------------------- the status page (swipe down from the middle)

static void page_build(void) {
    s_page = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_page, lv_color_hex(YK_PAPER), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_page, false);
    lv_obj_set_hidden(s_page, true);
    s_page_title = yk_ui_label(s_page, "", C - 160, C - 166, 320, YK_SIZE_HEADING, YK_INK);
    pic_init(&s_mini, s_page, 3);
    for (int i = 0; i < 3; i++) {
        s_page_icon[i] = lv_image_create(s_page);
        lv_image_set_src(s_page_icon[i], &s_icon[i]);
        lv_obj_set_pos(s_page_icon[i], C - 150 - 21, C - 22 + i * 52 - 21);
        for (int j = 0; j < 10; j++) {
            lv_obj_t *b = lv_obj_create(s_page);
            lv_obj_remove_style_all(b);
            lv_obj_set_size(b, 22, 22);
            lv_obj_set_pos(b, C - 112 + j * 26, C - 22 + i * 52 - 11);
            lv_obj_set_style_radius(b, 2, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            s_bar[i][j] = b;
        }
    }
    s_page_heart = lv_image_create(s_page);
    s_page_text = yk_ui_label(s_page, "", C - 66, C + 140, 220, YK_SIZE_STATUS, YK_INK2);
    lv_obj_set_style_text_align(s_page_text, LV_TEXT_ALIGN_LEFT, 0);
}

static void page_show(void) {
    const pp_pet_t *p = cur_pet();
    char title[48], next[48];
    if (p->stage == PP_LOCKED) snprintf(title, sizeof(title), "%s", stage_name(PP_LOCKED));
    else snprintf(title, sizeof(title), "%s · %s", pp_name((pp_id_t)s_cur), stage_name(p->stage));
    lv_label_set_text(s_page_title, title);
    const rd_look_t lk = look_of(s_cur);
    const int w = pic_draw(&s_mini, s_cur, grid_of(s_cur), 3, &lk, NULL);
    lv_obj_set_pos(s_mini.obj, C - w / 2, C - 96 - w / 2);
    const bool alive = pp_alive(p);
    const int vals[3] = {p->full, p->joy, p->energy};
    const uint32_t cols[3] = {YK_AMBER, YK_CORAL, YK_CREAM};
    for (int i = 0; i < 3; i++) {
        lv_obj_set_hidden(s_page_icon[i], !alive);
        const int n = (vals[i] + 50) / 100;
        for (int j = 0; j < 10; j++) {
            lv_obj_set_style_bg_color(s_bar[i][j], lv_color_hex(j < n ? cols[i] : YK_PANEL), 0);
            lv_obj_set_hidden(s_bar[i][j], !alive);
        }
    }
    const lv_image_dsc_t *hd = &s_heart[pp_heart_size(p) - 1];
    lv_image_set_src(s_page_heart, hd);
    lv_obj_set_pos(s_page_heart, C - 96 - (int)hd->header.w / 2, C + 150 - (int)hd->header.h / 2);
    lv_obj_set_hidden(s_page_heart, !alive);
    to_next_text(p, next, sizeof(next));
    lv_label_set_text(s_page_text, next);
    lv_obj_set_pos(s_page_text, alive ? C - 66 : C - 110, C + 140);
    lv_obj_set_style_text_align(s_page_text, alive ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_hidden(s_page, false);
}

static void page_close(void) {
    s_status = false;
    lv_obj_set_hidden(s_page, true);
}

// ---------------------------------------------------------------- what happens

static void changed(void) {
    s_dirty = true;
    s_touched = true;
}

static void save_now(void) {
    store_save(&S);
    s_dirty = false;
    s_saved_ms = now_ms();
}

/** After an action: the rim, the sound, the big heart when it hatched, grew up or dressed. */
static void on_event(pp_event_t ev, int id) {
    const int64_t now = now_ms();
    switch (ev) {
    case PP_EV_HATCHED:
    case PP_EV_GREW:
    case PP_EV_DRESSED:
        if (id == s_cur) {
            s_grow_at = now;
            yk_rim_set(s_rim, YK_RIM_DONE, NULL);
        }
        sound_play(SOUND_FANFARE, (pp_id_t)id);
        printf("PIXELPULSE_GROW %s stage=%d\n", pp_name((pp_id_t)id), S.pet[id].stage);
        show_dots();
        break;
    case PP_EV_CARE:
        if (id == s_cur) rim_progress(pp_progress(&S.pet[id]));
        break;
    case PP_EV_REFUSED:
        s_refuse_at = now;
        sound_play(SOUND_REFUSE, (pp_id_t)id);
        break;
    default: break;
    }
    if (ev != PP_EV_NONE) changed();
}

static void set_pet(int id) {
    if (id == s_cur) return;
    s_cur = id;
    s_switch_at = now_ms();
    s_look_until = 0;
    s_visit = false;
    lv_obj_set_hidden(s_guest_pic.obj, true);
    for (int i = 0; i < PARTS; i++) s_parts[i].on = false, lv_obj_set_hidden(s_parts[i].obj, true);
    const pp_pet_t *p = cur_pet();
    show_label(p->stage == PP_LOCKED ? stage_name(PP_LOCKED) : pp_name((pp_id_t)id), 1200);
    show_dots();
    printf("PIXELPULSE_PET %s stage=%d\n", pp_name((pp_id_t)id), p->stage);
}

static bool on_pet(int x, int y) {
    const pp_pet_t *p = cur_pet();
    const int cs = cs_of(p);
    const sp_grid_t *g = grid_of(s_cur);
    const int cy = foot_cy(g, cs);
    return fabsf((float)x - (C + s_pet_x)) < SP_G * cs * 0.4f && y > head_y(g, cs, cy) - 10 && y < GROUND + 10;
}

static void tap(int x, int y) {
    const int64_t now = now_ms();
    pp_pet_t *p = cur_pet();
    if (p->stage == PP_LOCKED) {
        s_wobble_at = now;
        show_label(T("先养大前一只", "Raise one first"), 1500);
        return;
    }
    if (s_visit) {
        s_bounce_at = s_guest_bounce = now;
        pp_touch(&S, (pp_id_t)s_cur);
        pp_touch(&S, (pp_id_t)s_guest);
        part_add(PK_HEART, C, GROUND - 150, now, 0);
        sound_play(SOUND_CHIRP, (pp_id_t)s_guest);
        changed();
        return;
    }
    if (!on_pet(x, y)) {
        if (pp_alive(p)) s_look = x < C + s_pet_x ? -1 : 1, s_look_until = now + 1500;
        return;
    }
    if (p->stage == PP_EGG) {
        s_wobble_at = now;
        sound_play(SOUND_CRACK, (pp_id_t)s_cur);
        const pp_event_t ev = pp_tap_egg(&S, (pp_id_t)s_cur);
        if (ev == PP_EV_CARE) rim_progress(pp_progress(p));
        on_event(ev, s_cur);
        changed();
        return;
    }
    const bool napping = pp_napping(p);
    pp_touch(&S, (pp_id_t)s_cur);
    s_bounce_at = now;
    sound_play(SOUND_CHIRP, (pp_id_t)s_cur);
    if (!napping) {
        const int cs = cs_of(p), hy = head_y(grid_of(s_cur), cs, foot_cy(grid_of(s_cur), cs));
        for (int i = 0; i < pp_heart_size(p); i++) part_add(PK_HEART, C + s_pet_x + (i - 1) * 30, hy + 10, now + i * 120, 0);
    }
    changed();
}

static void feed_one(void) {
    const int64_t now = now_ms();
    pp_pet_t *p = cur_pet();
    if (!pp_alive(p) || s_game || s_status) return;
    sound_play(SOUND_TICK, (pp_id_t)s_cur);
    const pp_event_t ev = pp_feed(&S, (pp_id_t)s_cur);
    if (ev != PP_EV_REFUSED) {
        const int cs = cs_of(p), hy = head_y(grid_of(s_cur), cs, foot_cy(grid_of(s_cur), cs));
        part_add(PK_PELLET, C + s_pet_x + (int)(esp_random() % 17) - 8, C - 200, now, hy + SP_G * cs * 0.35f);
        s_chomp_at = now + 450;
    }
    on_event(ev, s_cur);
}

static void shake(void) {
    const int64_t now = now_ms();
    pp_pet_t *p = cur_pet();
    if (s_status || s_game) return;
    if (p->stage == PP_EGG) {
        s_wobble_at = now;
        return;
    }
    if (!pp_alive(p)) return;
    s_spin_at = now;
    if (s_visit) s_guest_spin = now;
    const int cs = cs_of(p), cy = foot_cy(grid_of(s_cur), cs);
    const float r = SP_G * cs * 0.55f;
    for (int i = 0; i < 6; i++) {
        const float a = i / 6.0f * 6.2831853f - 1.5707963f;
        part_add(PK_STAR, C + s_pet_x + cosf(a) * r, cy + sinf(a) * r * 0.9f, now + i * 50, 0);
    }
    sound_play(SOUND_CHIRP, (pp_id_t)s_cur);
    on_event(pp_shake(&S, (pp_id_t)s_cur), s_cur);
    changed();
}

// ---------------------------------------------------------------- the screen: asleep and awake

static void screen_sleep(void) {
    if (s_asleep) return;
    if (s_dirty) save_now();
    board_display_sleep();
    sound_hold(false);
    s_asleep = true;
    printf("PIXELPULSE_SCREEN off\n");
}

static void screen_wake(void) {
    if (!s_asleep) return;
    board_display_wake(yk_app_brightness());
    s_asleep = false;
    s_input_ms = now_ms();
    printf("PIXELPULSE_SCREEN on\n");
}

static void input(void) {
    s_input_ms = now_ms();
    yk_app_input();
}

// ---------------------------------------------------------------- catching stars (swipe up)

static void game_end(void) {
    if (!s_game) return;
    s_game = false;
    for (int i = 0; i < ITEMS; i++) s_items[i].on = false, lv_obj_set_hidden(s_items[i].obj, true);
    lv_obj_set_hidden(s_score_label, true);
    yk_rim_set(s_rim, YK_RIM_OFF, NULL);
    yk_app_keep_screen_on(false);
    sound_hold(false);
    char text[32];
    snprintf(text, sizeof(text), s_en ? "Caught %d" : "接到 %d 颗", s_score);
    show_label(text, 1800);
    s_bounce_at = now_ms();
    printf("PIXELPULSE_GAME end score=%d\n", s_score);
    on_event(pp_game(&S, (pp_id_t)s_cur), s_cur);
    changed();
}

static void game_start(void) {
    if (!pp_alive(cur_pet())) {
        show_label(T("先把它孵出来", "Hatch it first"), 1500);
        return;
    }
    s_visit = false;
    lv_obj_set_hidden(s_guest_pic.obj, true);
    s_game = true;
    s_score = 0;
    s_game_t0 = now_ms();
    s_next_spawn = s_game_t0 + 600;
    s_drag = false;
    yk_app_keep_screen_on(true);
    sound_hold(true);
    yk_rim_set(s_rim, YK_RIM_COUNTDOWN, &(yk_rim_opts_t){.color = YK_AMBER});
    yk_rim_value(s_rim, 1000);
    lv_label_set_text(s_score_label, "0");
    lv_obj_set_hidden(s_score_label, false);
    hide_bubble();
    printf("PIXELPULSE_GAME start\n");
}

static float tilt_target(float range) {
    yk_motion_state_t m;
    if (!yk_motion_running()) return 0;
    yk_motion_state(&m);
    float gx = m.now.gravity[0];  // the lower edge pulls it: gravity's x points towards it
    if (fabsf(gx) < 0.08f) return 0;
    gx = gx > 0 ? gx - 0.08f : gx + 0.08f;
    const float v = gx * range * 3.0f;
    return v > range ? range : v < -range ? -range : v;
}

static void game_tick(int64_t now) {
    const int64_t left = GAME_MS - (now - s_game_t0);
    if (left <= 0) {
        game_end();
        return;
    }
    while (now >= s_next_spawn) {
        for (int i = 0; i < ITEMS; i++) {
            if (s_items[i].on) continue;
            s_items[i].on = true;
            s_items[i].heart = esp_random() % 100 < 15;
            s_items[i].x = C + (float)((int)(esp_random() % 301) - 150);
            s_items[i].t0 = s_next_spawn;
            lv_image_set_src(s_items[i].obj, s_items[i].heart ? &s_heart[1] : &s_star5);
            break;
        }
        s_next_spawn += 700;
    }
    const float target = s_drag ? s_drag_x : tilt_target(150);
    s_pet_x += ((target > 150 ? 150 : target < -150 ? -150 : target) - s_pet_x) * 0.18f;
    const int cs = 5;
    const sp_grid_t *g = grid_of(s_cur);
    const int64_t bt = now - s_bounce_at;
    const int bounce = bt >= 0 && bt < 450 ? (int)(sinf(bt / 450.0f * 3.14159f) * cs * 2) : 0;
    const rd_look_t lk = look_of(s_cur);
    const rd_face_t face = {.blink = (now % 5000) < 130, .look = fabsf(target - s_pet_x) > 8 ? (target > s_pet_x ? 1 : -1) : 0};
    const int w = pic_draw(&s_main, s_cur, g, cs, &lk, &face);
    const int cy = foot_cy(g, cs) - bounce, px = C + (int)s_pet_x;
    lv_obj_set_pos(s_main.obj, px - w / 2, cy - w / 2);
    lv_obj_set_pos(s_shadow, px - SP_G * cs * 3 / 10, GROUND);
    lv_obj_set_width(s_shadow, SP_G * cs * 6 / 10);
    const int head = head_y(g, cs, cy);
    for (int i = 0; i < ITEMS; i++) {
        if (!s_items[i].on) continue;
        const float y = C - 150 + (now - s_items[i].t0) * 0.17f;  // from under the score
        if (y >= head && y <= GROUND && fabsf(s_items[i].x - px) < SP_G * cs * 0.45f) {
            s_score += s_items[i].heart ? 3 : 1;
            sound_play(s_items[i].heart ? SOUND_HEART : SOUND_STAR, (pp_id_t)s_cur);
            s_bounce_at = now;
            part_add(PK_STAR, s_items[i].x, y, now, 0);
            s_items[i].on = false;
            lv_obj_set_hidden(s_items[i].obj, true);
            char t[8];
            snprintf(t, sizeof(t), "%d", s_score);
            lv_label_set_text(s_score_label, t);
            continue;
        }
        if (y > GROUND + 10) {
            s_items[i].on = false;
            lv_obj_set_hidden(s_items[i].obj, true);
            continue;
        }
        const lv_image_dsc_t *d = lv_image_get_src(s_items[i].obj);
        lv_obj_set_pos(s_items[i].obj, (int)s_items[i].x - (int)d->header.w / 2, (int)y - (int)d->header.h / 2);
        lv_obj_set_hidden(s_items[i].obj, false);
    }
    yk_rim_value(s_rim, (int)(left * 1000 / GAME_MS));
}

// ---------------------------------------------------------------- a visit (two grown companions)

static void visit_start(void) {
    const int g = pp_visitor(&S, (pp_id_t)s_cur, esp_random());
    if (g < 0 || !pp_big(cur_pet()) || s_game || s_status || s_asleep) return;
    s_guest = g;
    s_visit = true;
    s_visit_heart = false;
    s_visit_t0 = now_ms();
    hide_bubble();
    pp_event_t guest_ev;
    on_event(pp_visit(&S, (pp_id_t)s_cur, (pp_id_t)g, &guest_ev), s_cur);
    on_event(guest_ev, g);
    sound_play(SOUND_CHIRP, (pp_id_t)g);
    printf("PIXELPULSE_VISIT %s\n", pp_name((pp_id_t)g));
}

static void visit_tick(int64_t now) {
    const int64_t e = now - s_visit_t0, left = VISIT_MS - e;
    if (left <= 0) {
        s_visit = false;
        lv_obj_set_hidden(s_guest_pic.obj, true);
        return;
    }
    const float walk = e < 1200 ? 1 - e / 1200.0f : left < 1200 ? 1 - left / 1200.0f : 0;
    const int cs = 6;
    const int host_x = C - 95, guest_x = C + 95 + (int)(walk * 220);
    // the host
    {
        const sp_grid_t *g = grid_of(s_cur);
        const int64_t bt = now - s_bounce_at, sp = now - s_spin_at;
        const int bounce = bt >= 0 && bt < 450 ? (int)(sinf(bt / 450.0f * 3.14159f) * cs * 2) : 0;
        const rd_look_t lk = look_of(s_cur);
        const rd_face_t f = {.blink = (now % 5000) < 130, .flip = sp >= 0 && sp < 700 && (sp / 100) % 2 == 1};
        const int w = pic_draw(&s_main, s_cur, g, cs, &lk, &f);
        lv_obj_set_pos(s_main.obj, host_x - w / 2, foot_cy(g, cs) - bounce - w / 2);
        lv_obj_set_pos(s_shadow, host_x - SP_G * cs * 3 / 10, GROUND);
        lv_obj_set_width(s_shadow, SP_G * cs * 6 / 10);
    }
    // the guest, facing the host
    {
        const sp_grid_t *g = grid_of(s_guest);
        const int64_t bt = now - s_guest_bounce, sp = now - s_guest_spin;
        int bounce = bt >= 0 && bt < 450 ? (int)(sinf(bt / 450.0f * 3.14159f) * cs * 2) : 0;
        if (walk > 0) bounce += (int)((now / 166) % 2) * 3;
        const rd_look_t lk = look_of(s_guest);
        const bool spin = sp >= 0 && sp < 700 && (sp / 100) % 2 == 1;
        const rd_face_t f = {.blink = ((now + 2100) % 5000) < 130, .flip = !spin};
        const int w = pic_draw(&s_guest_pic, s_guest, g, cs, &lk, &f);
        lv_obj_set_pos(s_guest_pic.obj, guest_x - w / 2, foot_cy(g, cs) - bounce - w / 2);
        lv_obj_set_hidden(s_guest_pic.obj, false);
    }
    if (!s_visit_heart && e > 1200) {
        s_visit_heart = true;
        part_add(PK_HEART, C, GROUND - 140, now, 0);
    }
}

// ---------------------------------------------------------------- the companion alone

static void pet_tick(int64_t now) {
    pp_pet_t *p = cur_pet();
    const int cs = cs_of(p);
    const sp_grid_t *g = grid_of(s_cur);
    const float range = p->stage == PP_BABY ? 110 : pp_big(p) ? 50 : 90;
    s_pet_x += (tilt_target(range) - s_pet_x) * 0.08f;
    const bool nap = pp_napping(p);
    const int64_t bt = now - s_bounce_at, sp = now - s_spin_at, rf = now - s_refuse_at, wb = now - s_wobble_at;
    int bounce = bt >= 0 && bt < 450 ? (int)(sinf(bt / 450.0f * 3.14159f) * cs * 2) : 0;
    const int breathe = pp_alive(p) && (now / 1000) % 2 ? 2 : 0;
    int off = rf >= 0 && rf < 600 ? (int)lroundf(sinf(rf * 0.025f) * 1.4f) * cs : 0;
    if (!pp_alive(p) && wb >= 0 && wb < 500) off = (int)lroundf(sinf(wb * 0.03f) * (1 - wb / 500.0f) * 1.5f) * cs;
    const bool chomp = now > s_chomp_at && now < s_chomp_at + 300 && ((now - s_chomp_at) / 83) % 2 == 0;
    rd_face_t face = {0};
    if (pp_alive(p)) {
        face.blink = nap || (now % 5000) < 130;
        face.look = (!face.blink && now < s_look_until) ? s_look : 0;
        face.shut = chomp;
        face.flip = sp >= 0 && sp < 700 && (sp / 100) % 2 == 1;
    }
    const rd_look_t lk = look_of(s_cur);
    const int w = pic_draw(&s_main, s_cur, g, cs, &lk, &face);
    const int cx = C + (int)s_pet_x + off, cy = foot_cy(g, cs) - bounce + breathe;
    lv_obj_set_pos(s_main.obj, cx - w / 2, cy - w / 2);
    lv_obj_set_pos(s_shadow, cx - SP_G * cs * 3 / 10, GROUND);
    lv_obj_set_width(s_shadow, SP_G * cs * 6 / 10);
    // the bubble: what it needs, over its head away from its leaves and tail
    const pp_need_t need = nap ? PP_NEED_NONE : pp_need(p);
    if (need != PP_NEED_NONE) {
        const int side = side_of(s_cur), head = head_y(g, cs, cy);
        const int bx = cx + side * (pp_big(p) ? 128 : 104), by = pp_big(p) ? C - 124 : head - 30;
        lv_obj_set_pos(s_bubble, bx - 36, by - 32);
        lv_image_set_src(s_bubble_icon, &s_icon[need - 1]);
        for (int i = 0; i < 3; i++) {
            lv_obj_set_pos(s_trail[i], bx - side * (i * 8 + 10) - 3, by + 30 + i * 7);
            lv_obj_set_hidden(s_trail[i], false);
        }
        lv_obj_set_hidden(s_bubble, false);
    } else {
        hide_bubble();
    }
    if (nap && now - s_last_z > 1000) {
        s_last_z = now;
        part_add(PK_Z, cx - side_of(s_cur) * 80, head_y(g, cs, cy) + 20, now, 0);
    }
}

// ---------------------------------------------------------------- touch

static float angle_of(int x, int y) { return atan2f((float)(x - C), (float)(C - y)) * 57.29578f; }

static void on_touch(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST)
        return;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t pt = {0};
    if (indev) lv_indev_get_point(indev, &pt);
    if (code == LV_EVENT_PRESSED) {
        input();
        yk_hint_hide();
        s_press = (typeof(s_press)){.active = true, .x0 = pt.x, .y0 = pt.y};
        if (s_game) s_drag = true, s_drag_x = pt.x - C;
        return;
    }
    if (!s_press.active) return;
    if (code == LV_EVENT_PRESSING) {
        if (s_game) {
            s_drag_x = pt.x - C;
            return;
        }
        const int dx = pt.x - s_press.x0, dy = pt.y - s_press.y0;
        if (!s_press.moved && dx * dx + dy * dy > 12 * 12) {
            s_press.moved = true;
            // from the rim band, moving along it turns the dial: feeding
            const float rx = (float)(s_press.x0 - C), ry = (float)(s_press.y0 - C), r = hypotf(rx, ry);
            if (r > YK_R_EDGE - YK_DIAL_TOUCH && !s_status && pp_alive(cur_pet())) {
                const float radial = fabsf((dx * rx + dy * ry) / r), along = fabsf((dx * -ry + dy * rx) / r);
                if (along > radial) {
                    s_press.dialing = true;
                    s_press.last_deg = angle_of(pt.x, pt.y);
                    s_press.acc_deg = 0;
                    yk_rim_set(s_rim, YK_RIM_DIAL, &(yk_rim_opts_t){.ticks = 8});
                }
            }
        }
        if (s_press.dialing) {
            const float a = angle_of(pt.x, pt.y);
            float d = a - s_press.last_deg;
            if (d > 180) d -= 360;
            if (d < -180) d += 360;
            s_press.last_deg = a;
            s_press.acc_deg += d;
            yk_rim_value(s_rim, ((int)((a < 0 ? a + 360 : a) / 45)) % 8 + 1);
            s_ring_until = now_ms() + 1200;
            while (fabsf(s_press.acc_deg) >= 45) {
                s_press.acc_deg -= s_press.acc_deg > 0 ? 45 : -45;
                feed_one();
            }
        }
        return;
    }
    // released
    s_press.active = false;
    if (s_game) {
        s_drag = false;
        return;
    }
    if (s_press.dialing || code == LV_EVENT_PRESS_LOST) return;
    const int dx = pt.x - s_press.x0, dy = pt.y - s_press.y0;
    if (abs(dx) > 50 && abs(dx) > abs(dy)) {
        if (s_status) page_close();
        set_pet((s_cur + (dx < 0 ? 1 : PP_PETS - 1)) % PP_PETS);
        return;
    }
    if (dy > 50 && s_press.y0 > 90) {  // the top strip is the system's pull-down
        if (!s_status && !s_visit) {
            s_status = true;
            page_show();
            printf("PIXELPULSE_STATUS open\n");
        }
        return;
    }
    if (dy < -50) {
        if (s_status) page_close();
        else if (!s_visit) game_start();
        return;
    }
    if (!s_press.moved && !s_status) tap(pt.x, pt.y);
}

// ---------------------------------------------------------------- the side key and the motion sensor (other tasks)

void ui_key_tap(void) { __atomic_fetch_or(&s_flags, F_TAP, __ATOMIC_RELAXED); }

void ui_motion(const yk_motion_event_t *ev) {
    uint32_t f = 0;
    switch (ev->kind) {
    case YK_MOTION_SHAKE_START: f = F_SHAKE; break;
    case YK_MOTION_FACE_DOWN: f = F_DOWN; break;
    case YK_MOTION_FACE_UP: f = F_UP; break;
    case YK_MOTION_LIFT: f = F_LIFT; break;
    default: return;
    }
    __atomic_fetch_or(&s_flags, f, __ATOMIC_RELAXED);
}

static void handle_flags(int64_t now) {
    const uint32_t f = __atomic_exchange_n(&s_flags, 0, __ATOMIC_RELAXED);
    if (!f) return;
    if (f & F_DOWN) {
        // face down: under the blanket, asleep (the screen off)
        game_end();
        if (s_status) page_close();
        s_face_down = true;
        s_face_at = now;
        screen_sleep();
        printf("PIXELPULSE_SLEEP\n");
    }
    if ((f & (F_UP | F_LIFT)) && s_face_down) {
        s_face_down = false;
        screen_wake();
        if (now - s_face_at >= SLEEP_MIN_MS && pp_alive(cur_pet())) {
            s_bounce_at = now;
            sound_play(SOUND_YAWN, (pp_id_t)s_cur);
            on_event(pp_sleep(&S, (pp_id_t)s_cur), s_cur);
            changed();
        }
        printf("PIXELPULSE_WAKE after=%lld ms\n", (long long)(now - s_face_at));
    } else if ((f & F_LIFT) && s_asleep && !s_face_down) {
        screen_wake();  // picked up: the screen comes back
    }
    if (f & F_TAP) {
        input();
        if (s_asleep) screen_wake();
        else if (s_game) game_end();
        else if (s_status) page_close();
        else screen_sleep();
    }
    if ((f & F_SHAKE) && !s_asleep) {
        input();
        shake();
    }
}

// ---------------------------------------------------------------- every frame

static void tick(lv_timer_t *timer) {
    (void)timer;
    const int64_t now = now_ms();
    handle_flags(now);
    // The app ring or 「回到 Yay 说？」 opened over us (a double or a hold on the key): it must be seen, even when
    // our screen was out; and the idle time does not run out under it.
    if (yk_apps_is_open() || yk_apps_ask_is_open()) {
        screen_wake();
        s_input_ms = now;
    }
    if (s_asleep) return;
    // the clock: the meters go down as the hours pass (only with a real time)
    static int64_t last_decay;
    if (now - last_decay > 60000) {
        last_decay = now;
        const time_t t = time(NULL);
        if (t > 1700000000) pp_decay_to(&S, (int64_t)t);  // not saved for this: the next start works it out again from the saved clock
    }
    // a visit, if one is due
    if (s_visit_at > 0 && now >= s_visit_at) {
        s_visit_at = -1;
        visit_start();
    }
    // hatched, grown up, dressed: the big pixel heart beats twice, then the new look
    const int64_t g = now - s_grow_at;
    if (g >= 0 && g < 900) {
        const float ph = g / 900.0f;
        const float amp = fmaxf(expf(-powf((ph - 0.15f) / 0.08f, 2)), expf(-powf((ph - 0.55f) / 0.08f, 2)));
        lv_image_set_scale(s_pulse, (uint32_t)(256 * (1.0f + 0.16f * amp)));
        lv_obj_set_hidden(s_pulse, false);
        lv_obj_set_hidden(s_main.obj, true);
        hide_bubble();
        return;
    }
    if (!lv_obj_is_hidden(s_pulse)) {
        lv_obj_set_hidden(s_pulse, true);
        lv_obj_set_hidden(s_main.obj, false);
    }
    if (!s_status) {
        if (s_game) game_tick(now);
        else if (s_visit) {
            hide_bubble();
            visit_tick(now);
        } else pet_tick(now);
        parts_tick(now);
    }
    // the rim: progress and the dial fade back to nothing
    if (!s_game && s_ring_until && now > s_ring_until) {
        s_ring_until = 0;
        if (yk_rim_mode(s_rim) != YK_RIM_DONE) yk_rim_set(s_rim, YK_RIM_OFF, NULL);
    }
    // the line under it: a message, else the egg's hint
    if (s_label_until && now > s_label_until) {
        s_label_until = 0;
        lv_label_set_text(s_name, "");
    }
    if (!s_label_until && !s_game) {
        const pp_pet_t *p = cur_pet();
        const char *want = p->stage == PP_EGG && p->taps == 0 ? T("点它 10 下", "Tap it 10 times") : "";
        if (strcmp(lv_label_get_text(s_name), want)) lv_label_set_text(s_name, want);
    }
    // the screen goes off at the device's time; never in a game
    const int off_s = yk_app_screen_off_s();
    if (!s_game && off_s > 0 && now - s_input_ms > (int64_t)off_s * 1000) screen_sleep();
    if (s_dirty && now - s_saved_ms > SAVE_EVERY_MS) save_now();
}

// ---------------------------------------------------------------- start

static lv_obj_t *box(lv_obj_t *parent, int w, int h, uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

void ui_start(const ui_config_t *cfg) {
    s_en = cfg->english;
    const pp_id_t first = pp_from_name(yk_ui_buddy_name());
    const bool had = store_load(&S, first);
    printf("PIXELPULSE_LOAD %s first=%s\n", had ? "save" : "new", pp_name((pp_id_t)S.first));
    const time_t t = time(NULL);
    if (t > 1700000000) pp_decay_to(&S, (int64_t)t);
    for (int i = 0; i < PP_PETS; i++) {
        sp_build((pp_id_t)i, false, &s_grid[i][0]);
        sp_build((pp_id_t)i, true, &s_grid[i][1]);
    }
    sp_build_egg(&s_egg);
    s_crack_n = sp_crack(s_crack, (int)sizeof(s_crack) / 2);
    make_art();

    // the companion to show: the launch's, else the first one still growing, else the first
    s_cur = S.first;
    for (int i = 0; i < PP_PETS; i++)
        if (S.pet[i].stage == PP_EGG || S.pet[i].stage == PP_BABY) {
            s_cur = i;
            break;
        }
    if (cfg->launch_pet >= 0 && cfg->launch_pet < PP_PETS) s_cur = cfg->launch_pet;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_scrollable(s_screen, false);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(YK_PAPER), 0);
    s_rim = yk_rim_create(s_screen);
    s_ground = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_ground);
    lv_obj_set_size(s_ground, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_clickable(s_ground, false);
    lv_obj_add_event_cb(s_ground, draw_ground, LV_EVENT_DRAW_MAIN, NULL);
    s_shadow = box(s_screen, 100, 4, mix(YK_NIGHT, YK_PAPER, 45), 0);
    pic_init(&s_main, s_screen, 10);
    pic_init(&s_guest_pic, s_screen, 6);
    lv_obj_set_hidden(s_guest_pic.obj, true);
    s_bubble = box(s_screen, 72, 64, YK_PANEL, 16);
    s_bubble_icon = lv_image_create(s_bubble);
    lv_image_set_src(s_bubble_icon, &s_icon[0]);
    lv_obj_center(s_bubble_icon);
    for (int i = 0; i < 3; i++) s_trail[i] = box(s_bubble, 6 - i * 2, 6 - i * 2, YK_PANEL, 1);
    // the trail sits outside the bubble: on the screen instead
    for (int i = 0; i < 3; i++) lv_obj_set_parent(s_trail[i], s_screen), lv_obj_set_hidden(s_trail[i], true);
    lv_obj_set_hidden(s_bubble, true);
    for (int i = 0; i < PARTS; i++) {
        s_parts[i].obj = lv_image_create(s_screen);
        lv_obj_set_clickable(s_parts[i].obj, false);
        lv_obj_set_hidden(s_parts[i].obj, true);
    }
    for (int i = 0; i < ITEMS; i++) {
        s_items[i].obj = lv_image_create(s_screen);
        lv_obj_set_clickable(s_items[i].obj, false);
        lv_obj_set_hidden(s_items[i].obj, true);
    }
    s_pulse = lv_image_create(s_screen);
    lv_image_set_src(s_pulse, &s_big_heart);
    lv_obj_center(s_pulse);
    lv_obj_set_clickable(s_pulse, false);
    lv_obj_set_hidden(s_pulse, true);
    s_name = yk_ui_label(s_screen, "", C - 80, C + 176, 160, YK_SIZE_STATUS, YK_INK2);
    s_score_label = yk_ui_label(s_screen, "0", C - 60, C - 186, 120, YK_SIZE_HEADING, YK_INK);
    lv_obj_set_hidden(s_score_label, true);
    for (int i = 0; i < PP_PETS; i++) {
        s_dots[i] = box(s_screen, 8, 8, YK_PANEL, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_color(s_dots[i], lv_color_hex(YK_INK3), 0);
        lv_obj_set_pos(s_dots[i], C - 24 + i * 16 - 4, C + 208 - 4);
    }
    page_build();
    lv_obj_add_event_cb(s_screen, on_touch, LV_EVENT_ALL, NULL);
    show_dots();
    s_input_ms = s_saved_ms = now_ms();
    pet_tick(s_input_ms);
    lv_screen_load(s_screen);
    if (yk_hint_should(YK_HINT_APPS)) yk_hint_show(s_screen, YK_HINT_APPS, NULL);

    // what the launch asked for
    if (cfg->launch_action == UI_ACT_FEED && pp_alive(cur_pet())) {
        yk_rim_set(s_rim, YK_RIM_DIAL, &(yk_rim_opts_t){.ticks = 8});
        yk_rim_value(s_rim, 1);
        s_ring_until = now_ms() + 4000;
    } else if (cfg->launch_action == UI_ACT_PLAY) {
        game_start();
    }
    // a visit now and then: about one opening in three, ten seconds in
    if (pp_big(cur_pet()) && pp_visitor(&S, (pp_id_t)s_cur, 0) >= 0 && esp_random() % 3 == 0)
        s_visit_at = now_ms() + VISIT_DELAY_MS;
    lv_timer_create(tick, TICK_MS, NULL);
}

// ---------------------------------------------------------------- leaving, status

void ui_leave(void) {
    save_now();
    if (!s_touched) return;  // nothing happened: the main firmware just comes back
    const pp_pet_t *p = cur_pet();
    const char *name = pp_name((pp_id_t)s_cur);
    char sentence[96];
    const bool full = pp_alive(p) && p->full >= 800;
    switch (p->stage) {
    case PP_EGG:
        snprintf(sentence, sizeof(sentence), s_en ? "%s's egg needs %d more taps" : "%s 的蛋还要点 %d 下", name,
                 PP_EGG_TAPS - p->taps);
        break;
    case PP_BABY:
        snprintf(sentence, sizeof(sentence), s_en ? "%s%s %d more to grow up" : "%s%s再攒 %d 点就长大", name,
                 full ? (s_en ? " is full." : "吃饱了，") : (s_en ? ":" : "，"), pp_to_next(p));
        break;
    case PP_GROWN:
        snprintf(sentence, sizeof(sentence), s_en ? "%s%s %d more to dress up" : "%s%s再攒 %d 点换装扮", name,
                 full ? (s_en ? " is full." : "吃饱了，") : (s_en ? ":" : "，"), pp_to_next(p));
        break;
    case PP_DRESSED:
        snprintf(sentence, sizeof(sentence), s_en ? "%s is wearing its %s" : "%s戴着%s，在等你", name,
                 sp_outfit((pp_id_t)s_cur, s_en));
        break;
    default: return;
    }
    yk_summary_t summary;
    yk_summary_init(&summary);
    char value[12];
    if (pp_alive(p)) {
        snprintf(value, sizeof(value), "%d", p->care);
        yk_summary_stat(&summary, value, s_en ? "care" : "照顾点");
        if (p->love > 0) {  // a 0 on the main screen's ring reads as a failure
            snprintf(value, sizeof(value), "%d", p->love);
            yk_summary_stat(&summary, value, s_en ? "love" : "亲近");
        }
    }
    yk_app_set_result_summary(sentence, &summary, NULL);
}

void ui_status(cJSON *reply) {
    cJSON_AddStringToObject(reply, "pet", pp_name((pp_id_t)s_cur));
    cJSON *pets = cJSON_AddArrayToObject(reply, "pets");
    for (int i = 0; i < PP_PETS; i++) {
        const pp_pet_t *p = &S.pet[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", pp_name((pp_id_t)i));
        cJSON_AddNumberToObject(o, "stage", p->stage);
        cJSON_AddNumberToObject(o, "taps", p->taps);
        cJSON_AddNumberToObject(o, "care", p->care);
        cJSON_AddNumberToObject(o, "love", p->love);
        cJSON_AddNumberToObject(o, "full", p->full);
        cJSON_AddNumberToObject(o, "joy", p->joy);
        cJSON_AddNumberToObject(o, "energy", p->energy);
        cJSON_AddItemToArray(pets, o);
    }
    cJSON_AddBoolToObject(reply, "game", s_game);
    cJSON_AddBoolToObject(reply, "visit", s_visit);
}

#if CONFIG_YAY_DEV
// Development builds only: move time and points along to try every stage without waiting days.
esp_err_t ui_dev(const char *what, int n, cJSON *reply) {
    const int64_t now = now_ms();
    // like a person at the screen: it wakes first (the screen may have gone off on its own)
    if (strcmp(what, "down")) screen_wake();
    input();
    if (!strcmp(what, "tap")) {  // 0: on the companion, 1: on the empty left
        tap(n ? C - 180 : C + (int)s_pet_x, n ? C : GROUND - 40);
    } else if (!strcmp(what, "feed")) {
        for (int i = 0; i < n; i++) feed_one();
    } else if (!strcmp(what, "shake")) {
        shake();
    } else if (!strcmp(what, "down")) {
        __atomic_fetch_or(&s_flags, F_DOWN, __ATOMIC_RELAXED);
    } else if (!strcmp(what, "up")) {
        __atomic_fetch_or(&s_flags, F_UP, __ATOMIC_RELAXED);
    } else if (!strcmp(what, "key")) {
        ui_key_tap();
    } else if (!strcmp(what, "swipe")) {  // 0 left, 1 right, 2 down, 3 up: as the release of a swipe does
        if (n <= 1) {
            if (s_status) page_close();
            set_pet((s_cur + (n == 0 ? 1 : PP_PETS - 1)) % PP_PETS);
        } else if (n == 2) {
            if (!s_status && !s_visit) s_status = true, page_show();
        } else if (s_status) {
            page_close();
        } else if (!s_visit) {
            game_start();
        }
    } else if (!strcmp(what, "hours")) {
        pp_decay(&S, (int64_t)n * 3600);
    } else if (!strcmp(what, "care")) {
        on_event(pp_add_care(&S, (pp_id_t)s_cur, n), s_cur);
    } else if (!strcmp(what, "taps")) {
        for (int i = 0; i < n; i++) on_event(pp_tap_egg(&S, (pp_id_t)s_cur), s_cur);
    } else if (!strcmp(what, "visit")) {
        visit_start();
    } else if (!strcmp(what, "game")) {
        game_start();
    } else if (!strcmp(what, "pet")) {
        if (n >= 0 && n < PP_PETS) set_pet(n);
    } else if (!strcmp(what, "reset")) {
        pp_init(&S, (pp_id_t)(n >= 0 && n < PP_PETS ? n : S.first));
        s_cur = S.first;
        s_switch_at = now;
        show_dots();
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    changed();
    save_now();
    ui_status(reply);
    return ESP_OK;
}
#endif
