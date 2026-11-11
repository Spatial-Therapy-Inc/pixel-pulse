#include "store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "pixel-pulse";
#define NS "yk_pixelpulse"
#define KEY "save"

bool store_load(pp_save_t *s, pp_id_t first) {
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(*s);
        ok = nvs_get_blob(h, KEY, s, &len) == ESP_OK && len == sizeof(*s) && pp_valid(s);
        nvs_close(h);
    }
    if (!ok) pp_init(s, first);
    return ok;
}

void store_save(const pp_save_t *s) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY, s, sizeof(*s));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "not saved (%s)", esp_err_to_name(err));
}
