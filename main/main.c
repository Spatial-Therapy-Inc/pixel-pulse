// Pixel Pulse (像素悸动): the four companions as pixel pets, raised from an egg (design v0.4). The start follows the
// template's order: NVS (never erased) -> yaykit_app -> board, clock, speaker -> side key, motion -> Yaykit Link ->
// launch parameters -> the screen -> console -> yk_app_ready(). The screen and the rules are in ui.c and pet.c.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cjk_font.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "pet.h"
#include "sound.h"
#include "ui.h"
#include "yaykit_app.h"
#include "yaykit_key.h"
#include "yaykit_link.h"
#include "yaykit_motion.h"
#include "yaykit_ui.h"
#include "yk_vc_view.h"

#define APP_SLUG "pixel-pulse"
static const char *TAG = "pixel-pulse";

/** Through yaykit_app: a short press is ours (the game, the status page, the screen); the hold is not, so the system
 *  asks 「回到 Pip 说？」; the double click is the app ring. */
static bool on_key(const yk_key_event_t *ev, void *ctx) {
    (void)ctx;
    if (yk_vc_on_key(ev)) return true;
    if (ev->type == YK_KEY_TAP) {
        ui_key_tap();
        return true;
    }
    return false;
}

static void on_motion(const yk_motion_event_t *ev, void *ctx) {
    (void)ctx;
    ui_motion(ev);
}

static void on_leave(void *ctx) {
    (void)ctx;
    sound_stop_all();
    ui_leave();
}

static esp_err_t on_command(const char *name, const cJSON *args, cJSON *reply, const char **code, void *ctx) {
    (void)ctx;
    yk_app_input();
    esp_err_t err = yk_app_command(name, args, reply, code);  // app.exit, app.recovery, app.status
    if (err != ESP_ERR_NOT_SUPPORTED) return err;
    if (!strcmp(name, "pixelpulse.status")) {
        board_lock(-1);
        ui_status(reply);
        board_unlock();
        return ESP_OK;
    }
#if CONFIG_YAY_DEV
    if (!strcmp(name, "pixelpulse.dev")) {
        const cJSON *what = cJSON_GetObjectItem(args, "what"), *n = cJSON_GetObjectItem(args, "n");
        if (!cJSON_IsString(what)) {
            *code = "BAD_ARGS";
            return ESP_ERR_INVALID_ARG;
        }
        board_lock(-1);
        err = ui_dev(what->valuestring, cJSON_IsNumber(n) ? n->valueint : 1, reply);
        board_unlock();
        if (err != ESP_OK) *code = "BAD_ARGS";
        return err;
    }
#endif
    *code = "UNKNOWN_COMMAND";
    return ESP_ERR_NOT_SUPPORTED;
}

static int heap_cmd(int argc, char **argv) {
    (void)argc, (void)argv;
    printf("HEAP internal=%u internal_min=%u psram=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static void start_console(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = APP_SLUG ">";
    cfg.task_stack_size = 8192;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &cfg, &repl));
    esp_console_register_help_command();
    ESP_ERROR_CHECK(yk_link_register_console());
    ESP_ERROR_CHECK(yk_app_register_console());
    yk_ui_register_snapshot();
    const esp_console_cmd_t heap = {.command = "heap", .help = "available memory", .func = heap_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

/** {"pet":"dial","action":"feed"}: 「看看我的 Dial」、「喂一下 Mote」、「陪 Pip 玩」. */
static void read_launch(ui_config_t *cfg) {
    yk_launch_t launch;
    if (yk_app_launch(&launch) != ESP_OK || !launch.present) return;
    printf("PIXELPULSE_LAUNCH intent=%s params=%s\n", launch.intent, launch.params);
    cJSON *doc = cJSON_Parse(launch.params);
    if (!doc) return;
    const cJSON *pet = cJSON_GetObjectItem(doc, "pet"), *action = cJSON_GetObjectItem(doc, "action");
    if (cJSON_IsString(pet)) cfg->launch_pet = pp_from_name(pet->valuestring);
    if (cJSON_IsString(action)) {
        const char *a = action->valuestring;
        cfg->launch_action = !strcmp(a, "feed") ? UI_ACT_FEED : !strcmp(a, "play") ? UI_ACT_PLAY : UI_ACT_SEE;
    }
    cJSON_Delete(doc);
}

void app_main(void) {
    // 1. NVS: never erase it (it is shared with every app and holds the Wi-Fi credentials).
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS unavailable (%s); running without saved settings", esp_err_to_name(err));
    // 2. yaykit_app first, so that a crash anywhere below counts towards the crash loop guard.
    ESP_ERROR_CHECK(yk_app_start(&(yk_app_config_t){.slug = APP_SLUG, .version = APP_VERSION}));
    yk_app_on_leave(on_leave, NULL);
    // 3. Board and screen (dark until the first frame), the clock (the meters go down by the hours), the speaker.
    if (!board_display_start_opts(&(board_display_opts_t){.start_dark = true})) {
        ESP_LOGE(TAG, "display failed");
        return;
    }
    board_rtc_init();
    sound_init();
    // delay_tap: the first press of a double click (the app ring) is not a tap.
    ESP_ERROR_CHECK(yk_key_init(&(yk_key_config_t){.delay_tap = true}));
    ESP_ERROR_CHECK(yk_app_on_key(on_key, NULL));
    // Motion: shake to play, tilt to walk, face down to sleep. A hand's shake is gentler than the dice's 0.8 g.
    static yk_motion_thresholds_t motion_th;
    yk_motion_default_thresholds(&motion_th);
    motion_th.shake_peak_g = 0.5f;
    if (yk_motion_start(&(yk_motion_config_t){.thresholds = &motion_th, .on_event = on_motion}) != ESP_OK)
        ESP_LOGW(TAG, "no motion sensor: shaking, tilting and face down do nothing");
    // 4. Yaykit Link: `yk info` and `yk cmd` over USB (BLE stays off until asked for).
    const yk_link_config_t link = {.board = "tikpal-r1", .app_slug = APP_SLUG, .app_version = APP_VERSION,
                                   .layout = "yaykit-v1", .idle_seconds = 90, .on_command = on_command};
    ESP_ERROR_CHECK(yk_link_init(&link));
    // 5. Launch parameters and the owner's language.
    char locale[16];
    yk_app_locale(locale, sizeof(locale));
    ui_config_t cfg = {.english = yk_app_english(), .launch_pet = -1, .launch_action = UI_ACT_NONE};
    read_launch(&cfg);

    // 6. The screen; then confirm the image (the bootloader would roll back an app that never gets here).
    board_lock(-1);
    if (locale[0]) cjk_font_set_language(locale);
    ESP_ERROR_CHECK(cjk_font_init());
    yk_ui_set_english(cfg.english);
    ui_start(&cfg);
    lv_refr_now(NULL);
    board_brightness_set(yk_app_brightness());
    board_unlock();
    // 7. The console, then yk_app_ready(): its banner tells tools the app is up, so the console must already listen.
    start_console();
    yk_app_ready();
}
