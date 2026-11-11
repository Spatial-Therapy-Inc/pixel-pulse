// Pixel Pulse host tests: the rules (design v0.4 "怎么养"), the pixel art and the drawing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet.h"
#include "render.h"
#include "sprites.h"

static int s_fail;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            s_fail++;                                                  \
        }                                                              \
    } while (0)

static void test_init_and_names(void) {
    pp_save_t s;
    pp_init(&s, PP_DIAL);
    CHECK(s.pet[PP_DIAL].stage == PP_EGG);
    CHECK(s.pet[PP_PIP].stage == PP_LOCKED && s.pet[PP_BLOOM].stage == PP_LOCKED);
    CHECK(pp_valid(&s));
    CHECK(pp_from_name("dial") == PP_DIAL && pp_from_name("BLOOM") == PP_BLOOM && pp_from_name("Wow") == PP_PIP);
    CHECK(pp_from_name(NULL) == PP_PIP && !strcmp(pp_name(PP_MOTE), "Mote"));
    s.version = 9;
    CHECK(!pp_valid(&s));
}

static void test_hatch_and_grow(void) {
    pp_save_t s;
    pp_init(&s, PP_DIAL);
    for (int i = 0; i < PP_EGG_TAPS - 1; i++) CHECK(pp_tap_egg(&s, PP_DIAL) == PP_EV_CARE);
    CHECK(pp_progress(&s.pet[PP_DIAL]) == 900);
    CHECK(pp_tap_egg(&s, PP_DIAL) == PP_EV_HATCHED);
    CHECK(s.pet[PP_DIAL].stage == PP_BABY && pp_to_next(&s.pet[PP_DIAL]) == PP_BABY_PTS);
    // a baby grows up at 40 points, and the next egg appears: Pip first (Pip, Mote, Dial, Bloom)
    CHECK(pp_add_care(&s, PP_DIAL, PP_BABY_PTS - 1) == PP_EV_CARE);
    CHECK(pp_next_locked(&s) == PP_PIP);
    CHECK(pp_add_care(&s, PP_DIAL, 1) == PP_EV_GREW);
    CHECK(s.pet[PP_DIAL].stage == PP_GROWN && s.pet[PP_PIP].stage == PP_EGG);
    CHECK(pp_to_next(&s.pet[PP_DIAL]) == PP_DRESS_PTS && pp_progress(&s.pet[PP_DIAL]) == 0);
    // 80 more and it dresses up; that is the last stage
    CHECK(pp_add_care(&s, PP_DIAL, PP_DRESS_PTS) == PP_EV_DRESSED);
    CHECK(s.pet[PP_DIAL].stage == PP_DRESSED && pp_progress(&s.pet[PP_DIAL]) == 1000 && pp_to_next(&s.pet[PP_DIAL]) == 0);
    CHECK(pp_add_care(&s, PP_DIAL, 5) == PP_EV_CARE && s.pet[PP_DIAL].stage == PP_DRESSED);
    // eggs and locked ones take no care points
    CHECK(pp_add_care(&s, PP_PIP, 5) == PP_EV_NONE && pp_add_care(&s, PP_MOTE, 5) == PP_EV_NONE);
}

static void hatch(pp_save_t *s, pp_id_t id) {
    s->pet[id].stage = PP_EGG;
    s->pet[id].taps = 0;
    while (pp_tap_egg(s, id) != PP_EV_HATCHED) {}
}

static void test_only_when_needed(void) {
    pp_save_t s;
    pp_init(&s, PP_PIP);
    hatch(&s, PP_PIP);
    pp_pet_t *p = &s.pet[PP_PIP];
    // feeding: a point a grain until full, then refused (no farming)
    p->full = 700;
    int points = 0;
    for (int i = 0; i < 10; i++) {
        const pp_event_t ev = pp_feed(&s, PP_PIP);
        if (ev == PP_EV_CARE) points++;
        else CHECK(ev == PP_EV_REFUSED);
    }
    CHECK(points == 3 && p->full == 1000 && p->care == 3);
    // shaking: a point while it is not merry, nothing after
    p->joy = 700;
    CHECK(pp_shake(&s, PP_PIP) == PP_EV_CARE && pp_shake(&s, PP_PIP) == PP_EV_CARE);
    CHECK(p->joy == 1000 && pp_shake(&s, PP_PIP) == PP_EV_DONE);
    CHECK(p->care == 5);
    // sleeping: 2 points when tired, nothing when rested
    p->energy = 400;
    CHECK(pp_sleep(&s, PP_PIP) == PP_EV_CARE && p->energy == 1000 && p->care == 7);
    CHECK(pp_sleep(&s, PP_PIP) == PP_EV_DONE && p->care == 7);
    // a game: 3 points and joy
    p->joy = 0;
    CHECK(pp_game(&s, PP_PIP) == PP_EV_CARE && p->care == 10 && p->joy == 300);
}

static void test_decay_need_nap(void) {
    pp_save_t s;
    pp_init(&s, PP_MOTE);
    hatch(&s, PP_MOTE);
    pp_pet_t *p = &s.pet[PP_MOTE];
    p->full = p->joy = p->energy = 1000;
    CHECK(pp_need(p) == PP_NEED_NONE);
    pp_decay(&s, 6 * 3600);  // half a day's half
    CHECK(p->full == 500 && p->joy == 500 && p->energy > 500);
    pp_decay(&s, 3 * 3600);
    CHECK(p->full == 250 && pp_need(p) == PP_NEED_FOOD);
    pp_decay(&s, 2 * 3600);
    CHECK(pp_napping(p));
    // a touch wakes it, nothing is lost
    const uint16_t care = p->care;
    CHECK(pp_touch(&s, PP_MOTE) == PP_EV_DONE && p->joy >= 300 && !pp_napping(p) && p->care == care);
    // days away: empty, never below
    pp_decay(&s, 40LL * 24 * 3600);
    CHECK(p->full == 0 && p->joy == 0 && p->energy == 0);
    // eggs do not get hungry
    pp_init(&s, PP_MOTE);
    pp_decay(&s, 24 * 3600);
    CHECK(s.pet[PP_MOTE].full == 1000);
}

static void test_clock(void) {
    pp_save_t s;
    pp_init(&s, PP_PIP);
    hatch(&s, PP_PIP);
    s.pet[PP_PIP].full = 1000;
    pp_decay_to(&s, 1000000);  // first reading: only sets it
    CHECK(s.pet[PP_PIP].full == 1000 && s.clock_s == 1000000);
    for (int i = 1; i <= 60; i++) pp_decay_to(&s, 1000000 + i * 60);  // an hour, a minute at a time: nothing lost
    CHECK(s.pet[PP_PIP].full == 1000 - 3600 * 1000 / PP_EMPTY_S);
    pp_decay_to(&s, 500);  // the clock went back: only reset
    CHECK(s.clock_s == 500);
}

static void test_visit(void) {
    pp_save_t s;
    pp_init(&s, PP_PIP);
    CHECK(pp_visitor(&s, PP_PIP, 0) == -1);
    hatch(&s, PP_PIP);
    pp_add_care(&s, PP_PIP, PP_BABY_PTS);  // Pip grows up, Mote's egg appears
    CHECK(s.pet[PP_MOTE].stage == PP_EGG && pp_visitor(&s, PP_PIP, 0) == -1);
    hatch(&s, PP_MOTE);
    pp_add_care(&s, PP_MOTE, PP_BABY_PTS);
    CHECK(pp_visitor(&s, PP_PIP, 7) == PP_MOTE && pp_visitor(&s, PP_MOTE, 3) == PP_PIP);
    pp_event_t g;
    CHECK(pp_visit(&s, PP_PIP, PP_MOTE, &g) == PP_EV_CARE && g == PP_EV_CARE);
    CHECK(s.pet[PP_PIP].care == PP_BABY_PTS + 1);
}

static void test_sprites(void) {
    sp_grid_t g;
    for (int id = 0; id < PP_PETS; id++)
        for (int d = 0; d < 2; d++) {
            sp_build((pp_id_t)id, d, &g);
            int cells = 0, eyes = 0, unknown = 0;
            for (int y = 0; y < SP_G; y++)
                for (int x = 0; x < SP_G; x++) {
                    const char c = g.cell[y][x];
                    if (c == '.') continue;
                    cells++;
                    if (c == 'k' || c == 'e') eyes++;
                    if (sp_color((pp_id_t)id, c, false) == SP_NONE) unknown++;
                }
            CHECK(cells > 250 && eyes == 12 && unknown == 0);  // two 2 x 3 eyes, every letter coloured
            CHECK(g.top >= 0 && g.bottom < SP_G && g.bottom > 25);
        }
    // the outfit adds cells (or changes them) but keeps the body
    sp_grid_t plain, dressed;
    sp_build(PP_DIAL, false, &plain);
    sp_build(PP_DIAL, true, &dressed);
    CHECK(memcmp(&plain.cell, &dressed.cell, sizeof(plain.cell)) != 0 && plain.bottom == dressed.bottom);
    sp_build_egg(&g);
    CHECK(sp_color(PP_BLOOM, 'c', true) == 0xF2A93B && sp_color(PP_PIP, 'w', true) == 0xFBF1DE);
    uint8_t crack[128];
    const int n = sp_crack(crack, 64);
    CHECK(n > 8 && n <= 64);
    for (int i = 0; i < n; i++) CHECK(g.cell[crack[2 * i + 1]][crack[2 * i]] != '.');  // the crack is on the egg
    CHECK(!strcmp(sp_outfit(PP_DIAL, false), "耳机") && !strcmp(sp_outfit(PP_BLOOM, true), "bow"));
}

static void test_render(void) {
    sp_grid_t g;
    sp_build(PP_PIP, false, &g);
    const int cs = 6, w = SP_G * cs;
    uint8_t *img = malloc(rd_bytes(w, w));
    const rd_look_t lk = {.id = PP_PIP};
    rd_grid(img, w, w, &g, cs, &lk, NULL);
    const uint16_t *px = (const uint16_t *)img;
    const uint8_t *alpha = img + w * w * 2;
    // the eye's catch-light (11, 17) is white, the cell under it dark, the corner transparent
    CHECK(alpha[0] == 0);
    CHECK(alpha[(17 * cs + 2) * w + 11 * cs + 2] == 0xFF && px[(17 * cs + 2) * w + 11 * cs + 2] == rd_565(0xFFFFFF));
    CHECK(px[(18 * cs + 2) * w + 11 * cs + 2] == rd_565(0x2A2320));
    // blinking: the top two eye rows take the body's colour, the lowest stays dark
    rd_grid(img, w, w, &g, cs, &lk, &(rd_face_t){.blink = true});
    CHECK(px[(17 * cs + 2) * w + 11 * cs + 2] == rd_565(0xFBF1DE));
    CHECK(px[(19 * cs + 2) * w + 11 * cs + 2] == rd_565(0x2A2320));
    // a silhouette is one colour
    rd_grid(img, w, w, &g, cs, &(rd_look_t){.id = PP_PIP, .mono = 0x30372F}, NULL);
    CHECK(px[(17 * cs + 2) * w + 11 * cs + 2] == rd_565(0x30372F));
    // a 1 px gap between tiles from 8 px a cell up
    const int big = 10, wb = SP_G * big;
    uint8_t *img2 = malloc(rd_bytes(wb, wb));
    rd_grid(img2, wb, wb, &g, big, &lk, NULL);
    const uint8_t *a2 = img2 + wb * wb * 2;
    CHECK(a2[(17 * big + 2) * wb + 11 * big + 2] == 0xFF && a2[(17 * big + 9) * wb + 11 * big + 2] == 0);
    free(img);
    free(img2);
}

int main(void) {
    test_init_and_names();
    test_hatch_and_grow();
    test_only_when_needed();
    test_decay_need_nap();
    test_clock();
    test_visit();
    test_sprites();
    test_render();
    if (s_fail) {
        printf("pixel-pulse host tests: %d failed\n", s_fail);
        return 1;
    }
    printf("pixel-pulse host tests: all passed\n");
    return 0;
}
