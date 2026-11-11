// The screen (design v0.4): the companion standing on a strip of pixel ground, the rim, the bubble, particles, the
// status page, catching stars and the visits. Everything runs in one LVGL timer; the side key and the motion
// sensor only set flags for it.
#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "sdkconfig.h"
#include "yaykit_motion.h"

typedef enum { UI_ACT_NONE = 0, UI_ACT_SEE, UI_ACT_FEED, UI_ACT_PLAY } ui_action_t;

typedef struct {
    bool english;
    int launch_pet;  // pp_id_t from the launch parameters, -1 none
    ui_action_t launch_action;
} ui_config_t;

/** Builds the screen and loads the save. Under board_lock(). */
void ui_start(const ui_config_t *cfg);
/** From the key task: a short press. */
void ui_key_tap(void);
/** From the motion task. */
void ui_motion(const yk_motion_event_t *ev);
/** yk_app_on_leave: saves and writes the sentence the buddy says. */
void ui_leave(void);
/** `yk cmd pixelpulse.status`. */
void ui_status(cJSON *reply);

#if CONFIG_YAY_DEV
#include "esp_err.h"
/** Development builds: `yk cmd pixelpulse.dev {"what":"hours|care|taps|visit|game|pet|reset","n":N}`. Under board_lock(). */
esp_err_t ui_dev(const char *what, int n, cJSON *reply);
#endif
