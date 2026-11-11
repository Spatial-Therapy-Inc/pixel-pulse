#include "sprites.h"

#include <math.h>
#include <string.h>

#define U ((float)SP_G / 24.0f)
#define DEG (3.14159265f / 180.0f)

typedef enum { K_E, K_R, K_SMILE, K_HALF, K_LINE, K_ARC, K_ECLIP } kind_t;
typedef struct {
    char ch;
    kind_t k;
    float a[7];
} op_t;

// The shapes, in the design page's units (a 24 x 24 box)
#define E(ch, cx, cy, rx, ry, deg) {ch, K_E, {cx, cy, rx, ry, deg}}
#define R(ch, x0, y0, x1, y1, r) {ch, K_R, {x0, y0, x1, y1, r}}
#define SMILE(ch, cx, cy, r, th) {ch, K_SMILE, {cx, cy, r, th}}
#define HALF(ch, cx, cy, r) {ch, K_HALF, {cx, cy, r}}
#define LINE(ch, x0, y0, x1, y1, th) {ch, K_LINE, {x0, y0, x1, y1, th}}
#define ARC(ch, cx, cy, r, th, ymax) {ch, K_ARC, {cx, cy, r, th, ymax}}
#define ECLIP(ch, cx, cy, rx, ry, ymax) {ch, K_ECLIP, {cx, cy, rx, ry, 0, ymax}}

static bool hit(const op_t *o, float x, float y) {
    const float *a = o->a;
    switch (o->k) {
    case K_E:
    case K_ECLIP: {
        if (o->k == K_ECLIP && y > a[5]) return false;
        const float r = a[4] * DEG, dx = x - a[0], dy = y - a[1];
        const float u = dx * cosf(r) + dy * sinf(r), v = -dx * sinf(r) + dy * cosf(r);
        return (u * u) / (a[2] * a[2]) + (v * v) / (a[3] * a[3]) <= 1.0f;
    }
    case K_R: {
        if (x < a[0] || x > a[2] || y < a[1] || y > a[3]) return false;
        const float qx = fmaxf(fmaxf(a[0] + a[4] - x, 0), x - (a[2] - a[4]));
        const float qy = fmaxf(fmaxf(a[1] + a[4] - y, 0), y - (a[3] - a[4]));
        return qx * qx + qy * qy <= a[4] * a[4];
    }
    case K_SMILE: return y >= a[1] && fabsf(hypotf(x - a[0], y - a[1]) - a[2]) <= a[3];
    case K_HALF: return y >= a[1] - 0.05f && hypotf(x - a[0], y - a[1]) <= a[2];
    case K_LINE: {
        const float vx = a[2] - a[0], vy = a[3] - a[1];
        float t = ((x - a[0]) * vx + (y - a[1]) * vy) / (vx * vx + vy * vy);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        return hypotf(x - (a[0] + t * vx), y - (a[1] + t * vy)) <= a[4];
    }
    case K_ARC: return y <= a[4] && fabsf(hypotf(x - a[0], y - a[1]) - a[2]) <= a[3];
    }
    return false;
}

static void paint(char g[SP_G][SP_G], const op_t *ops, int n) {
    for (int i = 0; i < n; i++)
        for (int y = 0; y < SP_G; y++)
            for (int x = 0; x < SP_G; x++)
                if (hit(&ops[i], (x + 0.5f) / U, (y + 0.5f) / U)) g[y][x] = ops[i].ch;
}

typedef struct {
    const op_t *body;
    int nbody;
    const char *shade;  // pairs: letter, its shade
    const op_t *feats;
    int nfeats;
    const op_t *dress;
    int ndress;
    uint8_t eyes[2][4];  // x, y, w, h on the 32 grid
    char base;
    const char *pal;     // pairs: letter, then 6 hex digits, ';' between (the companion's own colours)
    uint32_t egg;
} pet_art_t;

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const op_t PIP_BODY[] = {
    E('o', 15.0f, 5.0f, 4.8f, 1.9f, -53), E('o', 16.9f, 2.8f, 2.3f, 1.9f, -40), E('o', 8.4f, 6.6f, 4.4f, 1.9f, 30),
    E('o', 12, 8.8f, 1.4f, 1.4f, 0),     E('w', 12, 16.0f, 8.0f, 6.8f, 0),       E('w', 12, 11.6f, 5.2f, 4.6f, 0),
    R('w', 4.4f, 16, 19.6f, 22.4f, 3.2f),
};
static const op_t PIP_FEATS[] = {
    E('q', 16.6f, 2.4f, 1.4f, 0.6f, -40), E('h', 8.4f, 12.0f, 1.4f, 0.8f, -30), E('p', 7.4f, 15.0f, 1.1f, 0.55f, 0),
    E('p', 16.6f, 15.0f, 1.1f, 0.55f, 0), SMILE('m', 12, 15.0f, 0.9f, 0.4f),
};
static const op_t PIP_DRESS[] = {
    R('c', 4.6f, 17.4f, 19.4f, 18.9f, 0.7f), R('r', 4.6f, 18.4f, 19.4f, 18.9f, 0), R('c', 14.6f, 18.2f, 16.6f, 21.6f, 0.6f),
    R('r', 14.6f, 20.9f, 16.6f, 21.6f, 0),
};
static const op_t MOTE_BODY[] = {
    E('y', 12, 10.8f, 6.2f, 6.4f, 0), E('y', 12, 15.4f, 7.4f, 6.4f, 0),   E('y', 8.6f, 5.0f, 1.5f, 1.1f, -40),
    E('y', 4.4f, 15.6f, 1.1f, 1.4f, 0), E('y', 19.6f, 15.6f, 1.1f, 1.4f, 0), E('y', 9.2f, 21.6f, 1.9f, 1.1f, 0),
    E('y', 14.8f, 21.6f, 1.9f, 1.1f, 0),
};
static const op_t MOTE_FEATS[] = {
    E('h', 10.0f, 7.4f, 1.3f, 0.7f, -25), E('p', 8.0f, 10.6f, 0.9f, 0.5f, 0), E('p', 16.0f, 10.6f, 0.9f, 0.5f, 0),
    SMILE('m', 12, 11.0f, 0.9f, 0.4f),
};
static const op_t MOTE_DRESS[] = {
    ECLIP('b', 12, 7.6f, 6.6f, 4.0f, 7.4f), R('B', 5.4f, 6.4f, 18.6f, 8.0f, 0.8f), E('z', 12, 2.8f, 1.5f, 1.4f, 0),
};
static const op_t DIAL_BODY[] = {
    E('Y', 18.4f, 4.6f, 2.1f, 3.6f, 12),   R('Y', 3.6f, 6.4f, 20.4f, 19.4f, 3.4f), E('Y', 2.8f, 13.8f, 1.5f, 1.5f, 0),
    E('Y', 8.2f, 20.2f, 2.1f, 1.6f, 0),    E('Y', 15.8f, 20.2f, 2.1f, 1.6f, 0),
};
static const op_t DIAL_FEATS[] = {
    E('h', 6.8f, 7.6f, 1.3f, 0.45f, 0), R('s', 5.4f, 8.4f, 14.6f, 17.6f, 2.2f), E('w', 17.4f, 10.6f, 2.2f, 2.2f, 0),
    LINE('t', 18.5f, 9.5f, 16.3f, 11.7f, 0.42f), E('n', 17.4f, 14.8f, 0.75f, 0.75f, 0), HALF('m', 10, 13.9f, 1.35f),
};
static const op_t DIAL_DRESS[] = {
    ARC('H', 12, 11.4f, 10.2f, 0.6f, 9.4f), R('c', 0.9f, 8.8f, 3.6f, 14.4f, 1.0f), R('c', 20.4f, 8.8f, 23.1f, 14.4f, 1.0f),
    R('r', 0.9f, 13.6f, 3.6f, 14.4f, 0),    R('r', 20.4f, 13.6f, 23.1f, 14.4f, 0),
};
static const op_t BLOOM_BODY[] = {
    E('O', 11.8f, 14.2f, 7.4f, 6.6f, 0), E('O', 14.6f, 7.8f, 4.6f, 4.4f, 15), E('O', 8.4f, 9.8f, 4.2f, 3.8f, 0),
    E('O', 4.0f, 13.4f, 3.0f, 2.5f, -25), E('O', 20.0f, 12.0f, 2.9f, 2.3f, 30), E('O', 8.0f, 19.8f, 3.8f, 3.0f, 0),
    E('O', 15.6f, 20.0f, 3.8f, 3.0f, 0),
};
static const op_t BLOOM_FEATS[] = {
    E('h', 13.8f, 4.6f, 1.8f, 0.7f, 15), HALF('m', 12.2f, 12.8f, 1.5f), E('p', 12.2f, 13.8f, 0.8f, 0.45f, 0),
};
static const op_t BLOOM_DRESS[] = {
    E('c', 13.0f, 3.4f, 1.9f, 1.1f, -25), E('c', 17.2f, 3.0f, 1.9f, 1.1f, 20), E('r', 15.1f, 3.3f, 0.7f, 0.8f, 0),
};

static const pet_art_t ART[PP_PETS] = {
    [PP_PIP] = {PIP_BODY, N(PIP_BODY), "wgot", PIP_FEATS, N(PIP_FEATS), PIP_DRESS, N(PIP_DRESS), {{11, 17, 2, 3}, {18, 17, 2, 3}},
                'w', "wFBF1DE;gDCCDB4;hFFFFFF;oEDB54A;tC98E2E;qF6D184", 0xEDB54A},
    [PP_MOTE] = {MOTE_BODY, N(MOTE_BODY), "yd", MOTE_FEATS, N(MOTE_FEATS), MOTE_DRESS, N(MOTE_DRESS), {{12, 11, 2, 3}, {18, 11, 2, 3}},
                 'y', "yEDC96E;dC9A04A;hF8E2A6", 0xEDC96E},
    [PP_DIAL] = {DIAL_BODY, N(DIAL_BODY), "YD", DIAL_FEATS, N(DIAL_FEATS), DIAL_DRESS, N(DIAL_DRESS), {{10, 14, 2, 3}, {14, 14, 2, 3}},
                 'Y', "YE6AD45;DB98238;hF3CB7A;sF8E3CC;wF6EEDD;tC9A06A;n7A3B22", 0xE6AD45},
    [PP_BLOOM] = {BLOOM_BODY, N(BLOOM_BODY), "OT", BLOOM_FEATS, N(BLOOM_FEATS), BLOOM_DRESS, N(BLOOM_DRESS), {{12, 13, 2, 3}, {18, 12, 2, 3}},
                  'O', "OF2A93B;TC9802E;hF8CB7A", 0xF2A93B},
};
// Shared by all four: the face and the outfits
static const char FACE[] = "k2A2320;eFFFFFF;m3A2620;pEE9A80;cDF795D;rB7654A;b9FB38C;B7F9470;zF3E6C8;H5A3A2A";
static const char EGG_PAL[] = "wFBF1DE;gDCCDB4;hFFFFFF";

static const op_t EGG_BODY[] = {E('w', 12, 13.2f, 6.6f, 8.6f, 0)};
static const op_t EGG_FEATS[] = {
    E('h', 9, 7.6f, 1.4f, 0.9f, -30), E('c', 9.4f, 10.4f, 1.5f, 1.0f, 0), E('c', 14.6f, 14.2f, 1.7f, 1.15f, 0),
    E('c', 9.8f, 18.2f, 1.3f, 0.85f, 0), E('c', 15, 8.4f, 0.8f, 0.6f, 0),
};

static void finish(sp_grid_t *out, char g[SP_G][SP_G], const char *shade, const op_t *feats, int nfeats,
                   const op_t *dress, int ndress, const uint8_t (*eyes)[4], int neyes) {
    // the shade: a body cell with empty space below, below-right or right takes its darker letter
    memcpy(out->cell, g, sizeof(out->cell));
    for (int y = 0; y < SP_G; y++)
        for (int x = 0; x < SP_G; x++) {
            for (const char *s = shade; s[0] && s[1]; s += 2) {
                if (g[y][x] != s[0]) continue;
                const bool open = y + 1 >= SP_G || x + 1 >= SP_G || g[y + 1][x] == '.' || g[y + 1][x + 1] == '.' ||
                                  g[y][x + 1] == '.';
                if (open) out->cell[y][x] = s[1];
            }
        }
    paint(out->cell, feats, nfeats);
    if (dress) paint(out->cell, dress, ndress);
    for (int i = 0; i < neyes; i++) {
        const uint8_t *e = eyes[i];
        for (int y = e[1]; y < e[1] + e[3]; y++)
            for (int x = e[0]; x < e[0] + e[2]; x++) out->cell[y][x] = (x == e[0] && y == e[1]) ? 'e' : 'k';
    }
    out->top = SP_G, out->bottom = 0;
    for (int y = 0; y < SP_G; y++)
        for (int x = 0; x < SP_G; x++)
            if (out->cell[y][x] != '.') {
                if (y < out->top) out->top = y;
                if (y > out->bottom) out->bottom = y;
            }
}

void sp_build(pp_id_t id, bool dressed, sp_grid_t *out) {
    const pet_art_t *a = &ART[id < PP_PETS ? id : PP_PIP];
    char g[SP_G][SP_G];
    memset(g, '.', sizeof(g));
    paint(g, a->body, a->nbody);
    finish(out, g, a->shade, a->feats, a->nfeats, dressed ? a->dress : NULL, a->ndress, a->eyes, 2);
    out->base = a->base;
}

void sp_build_egg(sp_grid_t *out) {
    char g[SP_G][SP_G];
    memset(g, '.', sizeof(g));
    paint(g, EGG_BODY, N(EGG_BODY));
    finish(out, g, "wg", EGG_FEATS, N(EGG_FEATS), NULL, 0, NULL, 0);
    out->base = 'w';
}

static uint32_t lookup(const char *pal, char ch) {
    for (const char *p = pal; *p;) {
        if (p[0] == ch) {
            uint32_t v = 0;
            for (int i = 1; i <= 6; i++) {
                const char c = p[i];
                v = v * 16 + (uint32_t)(c >= 'A' ? c - 'A' + 10 : c - '0');
            }
            return v;
        }
        p += 7;
        if (*p == ';') p++;
    }
    return SP_NONE;
}

uint32_t sp_color(pp_id_t id, char ch, bool egg) {
    if (ch == '.') return SP_NONE;
    const pet_art_t *a = &ART[id < PP_PETS ? id : PP_PIP];
    if (egg) return ch == 'c' ? a->egg : lookup(EGG_PAL, ch);
    const uint32_t own = lookup(a->pal, ch);
    return own != SP_NONE ? own : lookup(FACE, ch);
}

int sp_crack(uint8_t *xy, int max) {
    static const float pts[][2] = {{12, 5.4f}, {11, 7.6f}, {13, 9.2f}, {11.2f, 11}, {13.4f, 12.6f}, {12.2f, 14}};
    int n = 0;
    for (int i = 0; i < N(pts) - 1; i++)
        for (int s = 0; s <= 20; s++) {
            const float t = s / 20.0f;
            const int x = (int)floorf((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t) * U);
            const int y = (int)floorf((pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t) * U);
            bool seen = false;
            for (int j = 0; j < n; j++)
                if (xy[2 * j] == x && xy[2 * j + 1] == y) seen = true;
            if (!seen && n < max) xy[2 * n] = (uint8_t)x, xy[2 * n + 1] = (uint8_t)y, n++;
        }
    return n;
}

const char *sp_outfit(pp_id_t id, bool english) {
    static const char *const zh[PP_PETS] = {"围脖", "毛线帽", "耳机", "蝴蝶结"};
    static const char *const en[PP_PETS] = {"scarf", "woolly hat", "headphones", "bow"};
    return (english ? en : zh)[id < PP_PETS ? id : 0];
}
