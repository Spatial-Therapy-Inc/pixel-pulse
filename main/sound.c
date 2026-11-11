#include "sound.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "yaykit_speak.h"

static const char *TAG = "pixel-pulse";

typedef enum { W_SINE, W_SQUARE, W_NOISE } wave_t;
typedef struct {
    float f0, f1;  // Hz, swept
    int ms;
    wave_t wave;
    float gain;
} note_t;

#define NOTES 4
#define VOICES 6
typedef struct {
    note_t notes[NOTES];
    int count;
    int note, pos;  // the note playing and the sample in it
    float phase;
    uint32_t noise;
    volatile bool busy;
} voice_t;

static voice_t s_voices[VOICES];
static bool s_ready;

static bool render(float *out, size_t n, const yk_sfx_block_t *blk, void *ctx) {
    voice_t *v = ctx;
    const float rate = (float)blk->rate;
    for (size_t i = 0; i < n; i++) {
        if (v->note >= v->count) {
            out[i] = 0;
            continue;
        }
        const note_t *nt = &v->notes[v->note];
        const int len = nt->ms * (int)blk->rate / 1000;
        const float t = (float)v->pos / (float)(len > 0 ? len : 1);
        const float f = nt->f0 + (nt->f1 - nt->f0) * t;
        v->phase += f / rate;
        if (v->phase >= 1) v->phase -= floorf(v->phase);
        float s;
        switch (nt->wave) {
        case W_SQUARE: s = v->phase < 0.5f ? 0.6f : -0.6f; break;
        case W_NOISE:
            v->noise = v->noise * 1664525u + 1013904223u;
            s = ((float)(v->noise >> 9) / 4194304.0f - 1.0f);
            break;
        default: s = sinf(v->phase * 6.2831853f);
        }
        // a quick attack and a decay to nothing: no clicks
        const float att = t < 0.08f ? t / 0.08f : 1.0f;
        const float env = att * (1.0f - t) * (1.0f - t);
        out[i] = s * env * nt->gain;
        if (++v->pos >= len) v->note++, v->pos = 0;
    }
    if (v->note >= v->count) {
        v->busy = false;
        return false;
    }
    return true;
}

void sound_init(void) {
    const esp_err_t err = yk_speak_init_cfg(&(yk_speak_config_t){0});
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no speaker (%s): silent", esp_err_to_name(err));
        return;
    }
    s_ready = true;
}

static void play(const note_t *notes, int count) {
    if (!s_ready) return;
    for (int i = 0; i < VOICES; i++) {
        voice_t *v = &s_voices[i];
        if (v->busy) continue;
        memset(v, 0, sizeof(*v));
        memcpy(v->notes, notes, sizeof(note_t) * (size_t)count);
        v->count = count;
        v->noise = 0x1234567u + (uint32_t)i;
        v->busy = true;
        const yk_sfx_opts_t opts = {.gain = 0.7f, .no_duck = true};
        if (yk_sfx_play_synth(render, v, &opts) <= 0) v->busy = false;
        return;
    }
}

void sound_play(sound_t s, pp_id_t who) {
    switch (s) {
    case SOUND_CHIRP:
        switch (who) {
        case PP_PIP: {  // a clear 啾
            const note_t n[] = {{1900, 2600, 90, W_SINE, 0.8f}, {2300, 2900, 70, W_SINE, 0.6f}};
            play(n, 2);
            break;
        }
        case PP_MOTE: {  // a soft 噗
            const note_t n[] = {{520, 330, 150, W_SINE, 0.9f}};
            play(n, 1);
            break;
        }
        case PP_DIAL: {  // a radio's 嘀
            const note_t n[] = {{1250, 1250, 70, W_SQUARE, 0.35f}, {1650, 1650, 80, W_SQUARE, 0.35f}};
            play(n, 2);
            break;
        }
        default: {  // Bloom's bouncy 叮
            const note_t n[] = {{1320, 1500, 60, W_SINE, 0.8f}, {1760, 2000, 110, W_SINE, 0.8f}};
            play(n, 2);
        }
        }
        break;
    case SOUND_TICK: {
        const note_t n[] = {{2400, 1800, 28, W_SINE, 0.7f}};
        play(n, 1);
        break;
    }
    case SOUND_CRACK: {
        const note_t n[] = {{0, 0, 45, W_NOISE, 0.45f}};
        play(n, 1);
        break;
    }
    case SOUND_STAR: {
        const note_t n[] = {{1760, 2350, 70, W_SINE, 0.7f}};
        play(n, 1);
        break;
    }
    case SOUND_HEART: {
        const note_t n[] = {{1320, 1320, 60, W_SINE, 0.7f}, {1760, 1760, 60, W_SINE, 0.7f}, {2350, 2350, 90, W_SINE, 0.7f}};
        play(n, 3);
        break;
    }
    case SOUND_FANFARE: {
        const note_t n[] = {{784, 784, 120, W_SINE, 0.8f}, {988, 988, 120, W_SINE, 0.8f}, {1175, 1175, 120, W_SINE, 0.8f},
                            {1568, 1568, 260, W_SINE, 0.8f}};
        play(n, 4);
        break;
    }
    case SOUND_REFUSE: {
        const note_t n[] = {{700, 520, 120, W_SINE, 0.6f}};
        play(n, 1);
        break;
    }
    case SOUND_YAWN: {
        const note_t n[] = {{600, 900, 220, W_SINE, 0.5f}, {900, 500, 260, W_SINE, 0.5f}};
        play(n, 2);
        break;
    }
    }
}

void sound_hold(bool on) {
    if (s_ready) yk_speak_hold_amp(on);
}

void sound_stop_all(void) {
    if (!s_ready) return;
    yk_sfx_stop_all(30);  // returns once no callback runs: the voices are free again
    for (int i = 0; i < VOICES; i++) s_voices[i].busy = false;
}
