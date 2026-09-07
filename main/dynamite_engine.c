#include "dynamite_engine.h"
#include "mcp23s17.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lib_json_helper.h"
#include "cJSON.h"

static const char *TAG = "Dynamite32Prop";

#define DYN_CONFIG_FILE_PATH "/spiffs/config.json"
#define DYN_CONFIG_JSON_MAX 8192
#define DYN_EVENT_QUEUE_LEN 8
#define DYN_EVENT_JSON_MAX 192
#define MAGLOCK_GPIO GPIO_NUM_23
#define DOOR_GPIO GPIO_NUM_33
#define PRESSURE_PIN 8
#define CODE_SET_COUNT 8
#define CODE_MAX 16
#define VIRTUAL_KEY_HOLD_MS 2500

static const int k_rows[4] = {0, 1, 2, 3};
static const int k_cols[4] = {4, 5, 6, 7};
static const char *k_keys[16] = {
    "1", "2", "3", "A",
    "4", "5", "6", "B",
    "7", "8", "9", "C",
    "*", "0", "#", "D"
};
static const int k_default_grams[16] = {
    0, 150, 500, 200, 300, 250, 350, 250, 400, 450, 180, 220, 280, 320, 380, 550
};

typedef struct {
    char label[2];
    char code[CODE_MAX + 1];
} dyn_code_t;

typedef struct {
    char label[2];
    int grams;
} dyn_target_t;

typedef struct {
    char game_mode[16];
    char game_setting[2];
    char weight_setting[2];
    int keypad_scan_ms;
    int keypad_debounce;
    int reed_debounce_ms;
    int maglock_pulse_ms;
    int heartbeat_interval_ms;
    bool debug;
    dyn_code_t codes[CODE_SET_COUNT];
    int charge_grams[16];
    dyn_target_t targets[CODE_SET_COUNT];
} dyn_config_t;

typedef struct {
    dyn_config_t cfg;
    int ids[4];
    bool bits[4][4];
    bool reed_raw[4][4];
    bool reed_pressed[4][4];
    int64_t reed_edge_ms[4][4];
    bool pressure;
    bool pressure_raw;
    int64_t pressure_edge_ms;
    bool door_open;
    int maglock;
    int64_t maglock_until_ms;
    bool pulse_timeout_fired;
    int last_pulse_ms;
    char last_key[4];
    char entry_window[12];
    bool keys_down[16];
    int key_stable[16];
    int driven_row;
    int virtual_key_idx;
    int64_t virtual_key_until_ms;
    bool spi_ok;
    bool code_solved;
    bool code_solved_audio_played;
    char mqtt_last_in[96];
    int64_t mqtt_last_in_ts_ms;
    char mqtt_last_out[192];
    int64_t mqtt_last_out_ts_ms;
    bool wifi_connected;
    char wifi_ssid[33];
    int wifi_rssi;
    bool spiffs_ready;
    char event_queue[DYN_EVENT_QUEUE_LEN][DYN_EVENT_JSON_MAX];
    int event_head;
    int event_tail;
    SemaphoreHandle_t lock;
    esp_timer_handle_t maglock_timer;
} dyn_ctx_t;

static dyn_ctx_t s_ctx;

static const char *target_code_unlocked(void);

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void copy_bounded(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int clamp_int(int value, int min_v, int max_v)
{
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static bool dyn_lock(void)
{
    if (!s_ctx.lock) {
        return true;
    }
    return xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(2000)) == pdTRUE;
}

static void dyn_unlock(void)
{
    if (s_ctx.lock) {
        xSemaphoreGive(s_ctx.lock);
    }
}

static void set_default_config(dyn_config_t *cfg)
{
    static const char *k_codes[CODE_SET_COUNT] = {
        "A7D36#", "123456#", "B6540", "C9870", "", "", "", ""
    };
    static const int k_grams[CODE_SET_COUNT] = {1450, 1450, 1350, 1340, 0, 0, 0, 0};
    int i;

    memset(cfg, 0, sizeof(*cfg));
    copy_bounded(cfg->game_mode, sizeof(cfg->game_mode), "code_entry");
    cfg->game_setting[0] = 'B';
    cfg->weight_setting[0] = 'B';
    cfg->keypad_scan_ms = 5;
    cfg->keypad_debounce = 3;
    cfg->reed_debounce_ms = 25;
    cfg->maglock_pulse_ms = 250;
    cfg->heartbeat_interval_ms = 10000;
    cfg->debug = true;
    for (i = 0; i < CODE_SET_COUNT; ++i) {
        cfg->codes[i].label[0] = (char)('A' + i);
        copy_bounded(cfg->codes[i].code, sizeof(cfg->codes[i].code), k_codes[i]);
        cfg->targets[i].label[0] = (char)('A' + i);
        cfg->targets[i].grams = k_grams[i];
    }
    memcpy(cfg->charge_grams, k_default_grams, sizeof(k_default_grams));
}

static int maglock_pulse_ms_unlocked(void)
{
    return clamp_int(s_ctx.cfg.maglock_pulse_ms, 50, 400);
}

static void queue_event_unlocked(const char *json)
{
    int next = (s_ctx.event_head + 1) % DYN_EVENT_QUEUE_LEN;
    if (next == s_ctx.event_tail) {
        s_ctx.event_tail = (s_ctx.event_tail + 1) % DYN_EVENT_QUEUE_LEN;
    }
    copy_bounded(s_ctx.event_queue[s_ctx.event_head], DYN_EVENT_JSON_MAX, json);
    s_ctx.event_head = next;
}

static void record_mqtt_out_unlocked(const char *payload)
{
    copy_bounded(s_ctx.mqtt_last_out, sizeof(s_ctx.mqtt_last_out), payload);
    s_ctx.mqtt_last_out_ts_ms = now_ms();
}

static void record_mqtt_in_unlocked(const char *payload)
{
    copy_bounded(s_ctx.mqtt_last_in, sizeof(s_ctx.mqtt_last_in), payload);
    s_ctx.mqtt_last_in_ts_ms = now_ms();
}

static bool slot_occupied_unlocked(int slot)
{
    return s_ctx.ids[slot] > 0;
}

static bool all_connected_unlocked(void)
{
    int i;
    for (i = 0; i < 4; ++i) {
        if (!slot_occupied_unlocked(i)) {
            return false;
        }
    }
    return s_ctx.pressure;
}

static void publish_status_unlocked(void)
{
    char payload[DYN_EVENT_JSON_MAX];
    snprintf(payload,
             sizeof(payload),
             "{\"id0\":%d,\"id1\":%d,\"id2\":%d,\"id3\":%d,\"allConnected\":%s,\"doorOpen\":%s}",
             s_ctx.ids[0],
             s_ctx.ids[1],
             s_ctx.ids[2],
             s_ctx.ids[3],
             all_connected_unlocked() ? "true" : "false",
             s_ctx.door_open ? "true" : "false");
    record_mqtt_out_unlocked(payload);
    queue_event_unlocked(payload);
}

static void publish_keypress_unlocked(const char *key)
{
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"keypress\":\"%s\"}", key ? key : "");
    record_mqtt_out_unlocked(payload);
    queue_event_unlocked(payload);
}

static void maglock_drop_unlocked(bool timeout)
{
    gpio_set_level(MAGLOCK_GPIO, 0);
    s_ctx.maglock = 0;
    s_ctx.maglock_until_ms = 0;
    s_ctx.pulse_timeout_fired = timeout;
}

static void maglock_timer_cb(void *arg)
{
    (void)arg;
    if (!dyn_lock()) {
        gpio_set_level(MAGLOCK_GPIO, 0);
        return;
    }
    maglock_drop_unlocked(true);
    dyn_unlock();
}

static void maglock_pulse_unlocked(void)
{
    int ms = maglock_pulse_ms_unlocked();
    gpio_set_level(MAGLOCK_GPIO, 1);
    s_ctx.maglock = 1;
    s_ctx.last_pulse_ms = ms;
    s_ctx.pulse_timeout_fired = false;
    s_ctx.maglock_until_ms = now_ms() + ms;
    if (s_ctx.maglock_timer) {
        (void)esp_timer_stop(s_ctx.maglock_timer);
        (void)esp_timer_start_once(s_ctx.maglock_timer, (uint64_t)ms * 1000);
    }
}

static void expire_maglock_unlocked(void)
{
    if (s_ctx.maglock && s_ctx.maglock_until_ms && now_ms() >= s_ctx.maglock_until_ms) {
        maglock_drop_unlocked(true);
    }
}

static void debounce_bool(bool raw, bool *state, bool *raw_store, int64_t *edge_ms, int debounce_ms)
{
    if (raw != *raw_store) {
        *raw_store = raw;
        *edge_ms = now_ms();
    }
    if (raw != *state && (now_ms() - *edge_ms) >= debounce_ms) {
        *state = raw;
    }
}

static void scan_reeds_unlocked(void)
{
    int slot;
    int bit;
    int debounce = clamp_int(s_ctx.cfg.reed_debounce_ms, 5, 200);
    uint16_t gpio1 = mcp23s17_read_gpio(1);

    for (slot = 0; slot < 4; ++slot) {
        int id = 0;
        for (bit = 0; bit < 4; ++bit) {
            int logical = 16 + slot * 4 + bit;
            int port = logical % 16;
            bool raw_pressed = ((gpio1 & (1u << port)) == 0);
            debounce_bool(raw_pressed,
                          &s_ctx.reed_pressed[slot][bit],
                          &s_ctx.reed_raw[slot][bit],
                          &s_ctx.reed_edge_ms[slot][bit],
                          debounce);
            s_ctx.bits[slot][bit] = s_ctx.reed_pressed[slot][bit];
            if (s_ctx.reed_pressed[slot][bit]) {
                id += (1 << bit);
            }
        }
        s_ctx.ids[slot] = id;
    }
}

static void scan_pressure_unlocked(void)
{
    bool raw_pressed = mcp23s17_digital_read(PRESSURE_PIN) == 0;
    debounce_bool(raw_pressed,
                  &s_ctx.pressure,
                  &s_ctx.pressure_raw,
                  &s_ctx.pressure_edge_ms,
                  clamp_int(s_ctx.cfg.reed_debounce_ms, 5, 200));
}

static void append_entry_unlocked(const char *key)
{
    size_t len = strlen(s_ctx.entry_window);
    if (!key || !key[0]) {
        return;
    }
    if (len >= 10) {
        memmove(s_ctx.entry_window, s_ctx.entry_window + 1, 10);
        len = 9;
        s_ctx.entry_window[len] = '\0';
    }
    s_ctx.entry_window[len] = key[0];
    s_ctx.entry_window[len + 1] = '\0';
    {
        const char *code = target_code_unlocked();
        size_t n = code ? strlen(code) : 0;
        size_t elen = strlen(s_ctx.entry_window);
        if (n > 0 && elen >= n && strncmp(s_ctx.entry_window + (elen - n), code, n) == 0) {
            s_ctx.code_solved = true;
        }
    }
}

static int key_index_for(const char *key)
{
    int i;
    if (!key || !key[0]) {
        return -1;
    }
    for (i = 0; i < 16; ++i) {
        if (k_keys[i][0] == key[0] && k_keys[i][1] == '\0') {
            return i;
        }
    }
    return -1;
}

static bool sanitize_keypress(const char *in, char *out, size_t out_size)
{
    char c;
    if (!in || !out || out_size < 2) {
        return false;
    }
    c = in[0];
    if (c >= 'a' && c <= 'd') {
        c = (char)(c - 'a' + 'A');
    }
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'D') || c == '*' || c == '#') {
        out[0] = c;
        out[1] = '\0';
        return true;
    }
    return false;
}

static void expire_virtual_key_unlocked(void)
{
    if (s_ctx.virtual_key_idx < 0) {
        return;
    }
    if (now_ms() < s_ctx.virtual_key_until_ms) {
        return;
    }
    s_ctx.keys_down[s_ctx.virtual_key_idx] = false;
    s_ctx.virtual_key_idx = -1;
    s_ctx.virtual_key_until_ms = 0;
}

static bool virtual_key_holds_idx(int idx)
{
    return s_ctx.virtual_key_idx == idx && now_ms() < s_ctx.virtual_key_until_ms;
}

static void apply_virtual_key_unlocked(const char *key)
{
    int idx = key_index_for(key);
    if (idx < 0) {
        return;
    }
    if (s_ctx.virtual_key_idx >= 0 && s_ctx.virtual_key_idx != idx) {
        s_ctx.keys_down[s_ctx.virtual_key_idx] = false;
    }
    s_ctx.virtual_key_idx = idx;
    s_ctx.virtual_key_until_ms = now_ms() + VIRTUAL_KEY_HOLD_MS;
    s_ctx.keys_down[idx] = true;
    s_ctx.driven_row = idx / 4;
    copy_bounded(s_ctx.last_key, sizeof(s_ctx.last_key), k_keys[idx]);
    append_entry_unlocked(k_keys[idx]);
    publish_keypress_unlocked(k_keys[idx]);
}

static void scan_keypad_row_unlocked(int row)
{
    int col;
    int debounce = clamp_int(s_ctx.cfg.keypad_debounce, 1, 20);

    if (s_ctx.virtual_key_idx < 0) {
        s_ctx.driven_row = row;
    }
    mcp23s17_digital_write(k_rows[row], 0);
    {
        TickType_t hold = pdMS_TO_TICKS(clamp_int(s_ctx.cfg.keypad_scan_ms, 1, 50));
        vTaskDelay(hold > 0 ? hold : 1);
    }
    for (col = 0; col < 4; ++col) {
        int idx = row * 4 + col;
        bool down;
        if (virtual_key_holds_idx(idx)) {
            continue;
        }
        down = mcp23s17_digital_read(k_cols[col]) == 0;
        if (down) {
            if (s_ctx.key_stable[idx] < debounce) {
                s_ctx.key_stable[idx]++;
            }
            if (s_ctx.key_stable[idx] >= debounce && !s_ctx.keys_down[idx]) {
                s_ctx.keys_down[idx] = true;
                copy_bounded(s_ctx.last_key, sizeof(s_ctx.last_key), k_keys[idx]);
                append_entry_unlocked(k_keys[idx]);
                publish_keypress_unlocked(k_keys[idx]);
            }
        } else {
            s_ctx.key_stable[idx] = 0;
            s_ctx.keys_down[idx] = false;
        }
    }
    mcp23s17_digital_write(k_rows[row], 1);
}

static void scan_door_unlocked(void)
{
    s_ctx.door_open = gpio_get_level(DOOR_GPIO) != 0;
}

static void dyn_loop_task(void *arg)
{
    int row = 0;
    int prev_ids[4] = {0, 0, 0, 0};
    bool prev_pressure = false;
    bool prev_door = false;
    bool prev_all = false;

    (void)arg;
    while (true) {
        if (dyn_lock()) {
            expire_maglock_unlocked();
            expire_virtual_key_unlocked();
            if (s_ctx.spi_ok) {
                scan_reeds_unlocked();
                scan_pressure_unlocked();
                scan_keypad_row_unlocked(row);
            }
            scan_door_unlocked();
            row = (row + 1) & 3;
            if (s_ctx.ids[0] != prev_ids[0] || s_ctx.ids[1] != prev_ids[1] ||
                s_ctx.ids[2] != prev_ids[2] || s_ctx.ids[3] != prev_ids[3] ||
                s_ctx.pressure != prev_pressure || s_ctx.door_open != prev_door ||
                all_connected_unlocked() != prev_all) {
                publish_status_unlocked();
                prev_ids[0] = s_ctx.ids[0];
                prev_ids[1] = s_ctx.ids[1];
                prev_ids[2] = s_ctx.ids[2];
                prev_ids[3] = s_ctx.ids[3];
                prev_pressure = s_ctx.pressure;
                prev_door = s_ctx.door_open;
                prev_all = all_connected_unlocked();
            }
            dyn_unlock();
        }
        /* 100 Hz tick: pdMS_TO_TICKS(5) is 0 and starves IDLE / the WDT. */
        vTaskDelay(1);
    }
}

static int json_append(char *out, size_t out_size, int pos, const char *fmt, ...)
{
    va_list args;
    int n;

    if (!out || out_size == 0 || pos < 0 || (size_t)pos >= out_size) {
        return (pos < 0) ? 0 : pos;
    }
    va_start(args, fmt);
    n = vsnprintf(out + pos, out_size - (size_t)pos, fmt, args);
    va_end(args);
    if (n < 0) {
        out[out_size - 1] = '\0';
        return (int)(out_size - 1);
    }
    if ((size_t)n >= out_size - (size_t)pos) {
        return (int)(out_size - 1);
    }
    return pos + n;
}

static void sanitize_code(char *dst, size_t dst_size, const char *src)
{
    size_t o = 0;
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    for (; *src && o + 1 < dst_size; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'D') || c == '*' || c == '#') {
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
}

static void fill_config_json(char *out, size_t out_size, const dyn_config_t *cfg)
{
    int pos = 0;
    int i;

    pos = json_append(out, out_size, pos,
                      "{\"gameMode\":\"%s\",\"gameSetting\":\"%s\",\"weightSetting\":\"%s\","
                      "\"keypadScanMs\":%d,\"keypadDebounce\":%d,\"reedDebounceMs\":%d,"
                      "\"maglockPulseMs\":%d,\"heartbeatInterval\":%d,\"debug\":%s,\"codes\":[",
                      cfg->game_mode,
                      cfg->game_setting,
                      cfg->weight_setting,
                      cfg->keypad_scan_ms,
                      cfg->keypad_debounce,
                      cfg->reed_debounce_ms,
                      cfg->maglock_pulse_ms,
                      cfg->heartbeat_interval_ms,
                      cfg->debug ? "true" : "false");
    for (i = 0; i < CODE_SET_COUNT; ++i) {
        char esc[48];
        lib_json_escape_string(cfg->codes[i].code, esc, sizeof(esc));
        pos = json_append(out, out_size, pos, "%s{\"label\":\"%s\",\"code\":\"%s\"}",
                          i ? "," : "", cfg->codes[i].label, esc);
    }
    pos = json_append(out, out_size, pos, "],\"chargeWeights\":{");
    for (i = 1; i <= 15; ++i) {
        pos = json_append(out, out_size, pos, "%s\"%d\":%d", i == 1 ? "" : ",", i, cfg->charge_grams[i]);
    }
    pos = json_append(out, out_size, pos, "},\"targetWeights\":[");
    for (i = 0; i < CODE_SET_COUNT; ++i) {
        pos = json_append(out, out_size, pos, "%s{\"label\":\"%s\",\"grams\":%d}",
                          i ? "," : "", cfg->targets[i].label, cfg->targets[i].grams);
    }
    (void)json_append(out, out_size, pos, "]}");
}

static void apply_config_fields_unlocked(const char *json)
{
    cJSON *root;
    int i_val;
    bool b_val;
    char buf[16];

    if (!json) {
        return;
    }
    if (lib_json_extract_int(json, "keypadScanMs", &i_val)) {
        s_ctx.cfg.keypad_scan_ms = clamp_int(i_val, 1, 50);
    }
    if (lib_json_extract_int(json, "keypadDebounce", &i_val)) {
        s_ctx.cfg.keypad_debounce = clamp_int(i_val, 1, 20);
    }
    if (lib_json_extract_int(json, "reedDebounceMs", &i_val)) {
        s_ctx.cfg.reed_debounce_ms = clamp_int(i_val, 5, 200);
    }
    if (lib_json_extract_int(json, "maglockPulseMs", &i_val)) {
        s_ctx.cfg.maglock_pulse_ms = clamp_int(i_val, 50, 400);
    }
    if (lib_json_extract_int(json, "heartbeatInterval", &i_val)) {
        s_ctx.cfg.heartbeat_interval_ms = clamp_int(i_val, 1000, 120000);
    }
    if (lib_json_extract_bool(json, "debug", &b_val)) {
        s_ctx.cfg.debug = b_val;
    }
    if (lib_json_extract_string(json, "gameMode", buf, sizeof(buf))) {
        copy_bounded(s_ctx.cfg.game_mode, sizeof(s_ctx.cfg.game_mode),
                     strcmp(buf, "code_entry") == 0 ? "code_entry" : "io");
    }
    if (lib_json_extract_string(json, "gameSetting", buf, sizeof(buf))) {
        s_ctx.cfg.game_setting[0] = buf[0] ? buf[0] : 'B';
        s_ctx.cfg.game_setting[1] = '\0';
    }
    if (lib_json_extract_string(json, "weightSetting", buf, sizeof(buf))) {
        s_ctx.cfg.weight_setting[0] = buf[0] ? buf[0] : 'B';
        s_ctx.cfg.weight_setting[1] = '\0';
    }

    root = lib_json_parse(json);
    if (!root) {
        return;
    }
    {
        cJSON *codes = cJSON_GetObjectItemCaseSensitive(root, "codes");
        if (cJSON_IsArray(codes)) {
            int i;
            for (i = 0; i < CODE_SET_COUNT && i < cJSON_GetArraySize(codes); ++i) {
                cJSON *item = cJSON_GetArrayItem(codes, i);
                char code[CODE_MAX + 1] = "";
                (void)lib_json_get_string(item, "code", code, sizeof(code));
                sanitize_code(s_ctx.cfg.codes[i].code, sizeof(s_ctx.cfg.codes[i].code), code);
            }
        }
        {
            cJSON *weights = cJSON_GetObjectItemCaseSensitive(root, "chargeWeights");
            if (cJSON_IsObject(weights)) {
                int id;
                for (id = 1; id <= 15; ++id) {
                    char key[8];
                    snprintf(key, sizeof(key), "%d", id);
                    cJSON *val = cJSON_GetObjectItemCaseSensitive(weights, key);
                    if (cJSON_IsNumber(val)) {
                        s_ctx.cfg.charge_grams[id] = clamp_int(val->valueint, 1, 2000);
                    }
                }
            }
        }
        {
            cJSON *targets = cJSON_GetObjectItemCaseSensitive(root, "targetWeights");
            if (cJSON_IsArray(targets)) {
                int i;
                for (i = 0; i < CODE_SET_COUNT && i < cJSON_GetArraySize(targets); ++i) {
                    cJSON *item = cJSON_GetArrayItem(targets, i);
                    cJSON *grams = cJSON_GetObjectItemCaseSensitive(item, "grams");
                    if (cJSON_IsNumber(grams)) {
                        s_ctx.cfg.targets[i].grams = clamp_int(grams->valueint, 0, 5000);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}

static esp_err_t save_config_file_unlocked(void)
{
    FILE *f;
    char *json;

    if (!s_ctx.spiffs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    json = (char *)malloc(DYN_CONFIG_JSON_MAX);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    fill_config_json(json, DYN_CONFIG_JSON_MAX, &s_ctx.cfg);
    f = fopen(DYN_CONFIG_FILE_PATH, "w");
    if (!f) {
        free(json);
        return ESP_FAIL;
    }
    fputs(json, f);
    fclose(f);
    free(json);
    return ESP_OK;
}

static void load_config_file_if_present(void)
{
    FILE *f;
    char *buf;
    size_t n;

    if (!s_ctx.spiffs_ready) {
        return;
    }
    f = fopen(DYN_CONFIG_FILE_PATH, "r");
    if (!f) {
        return;
    }
    buf = (char *)malloc(DYN_CONFIG_JSON_MAX);
    if (!buf) {
        fclose(f);
        return;
    }
    n = fread(buf, 1, DYN_CONFIG_JSON_MAX - 1, f);
    fclose(f);
    buf[n] = '\0';
    apply_config_fields_unlocked(buf);
    free(buf);
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 6,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        return err;
    }
    s_ctx.spiffs_ready = true;
    return ESP_OK;
}

static int grams_for_id_unlocked(int id)
{
    if (id < 1 || id > 15) {
        return 0;
    }
    return s_ctx.cfg.charge_grams[id];
}

static int target_grams_unlocked(void)
{
    int i;
    char pick = s_ctx.cfg.weight_setting[0] ? s_ctx.cfg.weight_setting[0] : 'B';
    for (i = 0; i < CODE_SET_COUNT; ++i) {
        if (s_ctx.cfg.targets[i].label[0] == pick) {
            return s_ctx.cfg.targets[i].grams;
        }
    }
    return 0;
}

static const char *target_code_unlocked(void)
{
    int i;
    for (i = 0; i < CODE_SET_COUNT; ++i) {
        if (s_ctx.cfg.codes[i].label[0] &&
            s_ctx.cfg.codes[i].label[0] == s_ctx.cfg.game_setting[0] &&
            s_ctx.cfg.codes[i].code[0]) {
            return s_ctx.cfg.codes[i].code;
        }
    }
    for (i = 0; i < CODE_SET_COUNT; ++i) {
        if (s_ctx.cfg.codes[i].code[0]) {
            return s_ctx.cfg.codes[i].code;
        }
    }
    return "A7D36#";
}

static void gm_publish_code_keys_unlocked(const char *code)
{
    char key[2] = {0};
    if (!code) {
        return;
    }
    for (; *code; ++code) {
        key[0] = *code;
        if (key[0] >= 'a' && key[0] <= 'd') {
            key[0] = (char)(key[0] - 'a' + 'A');
        }
        if ((key[0] >= '0' && key[0] <= '9') || (key[0] >= 'A' && key[0] <= 'D') ||
            key[0] == '*' || key[0] == '#') {
            append_entry_unlocked(key);
            publish_keypress_unlocked(key);
            copy_bounded(s_ctx.last_key, sizeof(s_ctx.last_key), key);
        }
    }
}

static void gm_solve_code_unlocked(char *response, size_t response_size)
{
    const char *code = target_code_unlocked();
    bool first = !s_ctx.code_solved;

    s_ctx.code_solved = true;
    s_ctx.entry_window[0] = '\0';

    if (first) {
        gm_publish_code_keys_unlocked(code);
        s_ctx.code_solved_audio_played = true;
        queue_event_unlocked("{\"event\":\"codeSolved\",\"audio\":true}");
        record_mqtt_out_unlocked("{\"event\":\"codeSolved\",\"audio\":true}");
        snprintf(response, response_size,
                 "{\"ok\":true,\"Command\":\"solveCode\",\"codeSolved\":true,\"audio\":true}");
    } else {
        copy_bounded(s_ctx.entry_window, sizeof(s_ctx.entry_window), code);
        queue_event_unlocked("{\"event\":\"codeSolved\",\"audio\":false}");
        record_mqtt_out_unlocked("{\"event\":\"codeSolved\",\"audio\":false}");
        snprintf(response, response_size,
                 "{\"ok\":true,\"Command\":\"solveCode\",\"codeSolved\":true,\"audio\":false}");
    }
}

static void gm_send_charges_unlocked(char *response, size_t response_size)
{
    if (!s_ctx.code_solved) {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"code_not_solved\"}");
        return;
    }
    append_entry_unlocked("*");
    publish_keypress_unlocked("*");
    copy_bounded(s_ctx.last_key, sizeof(s_ctx.last_key), "*");
    queue_event_unlocked("{\"event\":\"sendCharges\"}");
    record_mqtt_out_unlocked("{\"event\":\"sendCharges\"}");
    snprintf(response, response_size, "{\"ok\":true,\"Command\":\"sendCharges\",\"keypress\":\"*\"}");
}

static void gm_reset_unlocked(void)
{
    s_ctx.code_solved = false;
    s_ctx.code_solved_audio_played = false;
    s_ctx.entry_window[0] = '\0';
    s_ctx.last_key[0] = '\0';
}

esp_err_t dynamite_engine_init(void)
{
    int i;
    const esp_timer_create_args_t timer_args = {
        .callback = maglock_timer_cb,
        .name = "maglock",
    };

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.virtual_key_idx = -1;
    s_ctx.lock = xSemaphoreCreateMutex();
    if (!s_ctx.lock) {
        return ESP_ERR_NO_MEM;
    }
    set_default_config(&s_ctx.cfg);
    (void)init_spiffs();
    load_config_file_if_present();

    gpio_reset_pin(MAGLOCK_GPIO);
    gpio_set_direction(MAGLOCK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(MAGLOCK_GPIO, 0);

    gpio_config_t door_cfg = {
        .pin_bit_mask = 1ULL << DOOR_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&door_cfg);

    if (esp_timer_create(&timer_args, &s_ctx.maglock_timer) != ESP_OK) {
        ESP_LOGE(TAG, "maglock timer create failed");
        return ESP_ERR_NO_MEM;
    }

    if (mcp23s17_init() != ESP_OK) {
        ESP_LOGE(TAG, "MCP23S17 init failed");
        s_ctx.spi_ok = false;
    } else {
        s_ctx.spi_ok = true;
        for (i = 0; i < 4; ++i) {
            mcp23s17_pin_output(k_rows[i]);
            mcp23s17_digital_write(k_rows[i], 1);
            mcp23s17_pin_input_pullup(k_cols[i]);
        }
        mcp23s17_pin_input_pullup(PRESSURE_PIN);
        for (i = 16; i < 32; ++i) {
            mcp23s17_pin_input_pullup(i);
        }
        s_ctx.spi_ok = mcp23s17_ok();
    }

    if (xTaskCreate(dyn_loop_task, "dyn_loop", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Dynamite engine initialized");
    return ESP_OK;
}

void dynamite_engine_get_mqtt_status_json(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (!dyn_lock()) {
        out[0] = '\0';
        return;
    }
    snprintf(out,
             out_size,
             "{\"id0\":%d,\"id1\":%d,\"id2\":%d,\"id3\":%d,\"allConnected\":%s,\"doorOpen\":%s}",
             s_ctx.ids[0],
             s_ctx.ids[1],
             s_ctx.ids[2],
             s_ctx.ids[3],
             all_connected_unlocked() ? "true" : "false",
             s_ctx.door_open ? "true" : "false");
    dyn_unlock();
}

void dynamite_engine_get_state_json(char *out, size_t out_size)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char esc_mqtt_in[192];
    char esc_mqtt_out[192];
    char esc_ssid[72];
    char esc_key[16];
    char esc_win[32];
    int pos = 0;
    int slot;
    int occupied = 0;
    int current = 0;
    int target;
    int i;

    if (!out || out_size < 32) {
        return;
    }
    if (!dyn_lock()) {
        out[0] = '\0';
        return;
    }
    expire_maglock_unlocked();
    lib_json_escape_string(s_ctx.mqtt_last_in, esc_mqtt_in, sizeof(esc_mqtt_in));
    lib_json_escape_string(s_ctx.mqtt_last_out, esc_mqtt_out, sizeof(esc_mqtt_out));
    lib_json_escape_string(s_ctx.wifi_ssid, esc_ssid, sizeof(esc_ssid));
    lib_json_escape_string(s_ctx.last_key, esc_key, sizeof(esc_key));
    lib_json_escape_string(s_ctx.entry_window, esc_win, sizeof(esc_win));
    for (slot = 0; slot < 4; ++slot) {
        if (s_ctx.ids[slot] > 0) {
            occupied++;
            current += grams_for_id_unlocked(s_ctx.ids[slot]);
        }
    }
    target = target_grams_unlocked();

    pos = json_append(out, out_size, pos,
                      "{\"scenario\":\"live\",\"id\":\"Dynamite32Prop\",\"version\":\"%s\","
                      "\"id0\":%d,\"id1\":%d,\"id2\":%d,\"id3\":%d,"
                      "\"pressure\":%s,\"storagePresent\":%s,\"allConnected\":%s,"
                      "\"doorOpen\":%s,\"maglock\":%d,\"lastKey\":\"%s\",\"entryWindow\":\"%s\","
                      "\"drivenRow\":%d,\"lastPulseMs\":%d,\"pulseTimeoutFired\":%s,\"spiOk\":%s,"
                      "\"codeSolved\":%s,\"codeSolvedAudioPlayed\":%s,"
                      "\"gameMode\":\"%s\",\"gameSetting\":\"%s\",\"weightSetting\":\"%s\","
                      "\"targetWeight\":%d,\"currentWeight\":%d,\"occupiedCount\":%d,"
                      "\"weightMatch\":%s,\"wifiConnected\":%s,\"wifiSsid\":\"%s\",\"wifiRssi\":%d,"
                      "\"lastMqttIn\":\"%s\",\"lastMqttOut\":\"%s\",\"lastMqttAt\":%lld,"
                      "\"slots\":[",
                      app ? app->version : "0.01",
                      s_ctx.ids[0], s_ctx.ids[1], s_ctx.ids[2], s_ctx.ids[3],
                      s_ctx.pressure ? "true" : "false",
                      s_ctx.pressure ? "true" : "false",
                      all_connected_unlocked() ? "true" : "false",
                      s_ctx.door_open ? "true" : "false",
                      s_ctx.maglock,
                      esc_key,
                      esc_win,
                      s_ctx.driven_row,
                      s_ctx.last_pulse_ms,
                      s_ctx.pulse_timeout_fired ? "true" : "false",
                      s_ctx.spi_ok ? "true" : "false",
                      s_ctx.code_solved ? "true" : "false",
                      s_ctx.code_solved_audio_played ? "true" : "false",
                      s_ctx.cfg.game_mode,
                      s_ctx.cfg.game_setting,
                      s_ctx.cfg.weight_setting,
                      target,
                      current,
                      occupied,
                      (occupied == 4 && target > 0 && current == target) ? "true" : "false",
                      s_ctx.wifi_connected ? "true" : "false",
                      esc_ssid,
                      s_ctx.wifi_rssi,
                      esc_mqtt_in,
                      esc_mqtt_out,
                      (long long)s_ctx.mqtt_last_out_ts_ms);

    for (slot = 0; slot < 4; ++slot) {
        pos = json_append(out, out_size, pos,
                          "%s{\"id\":%d,\"occupied\":%s,\"weight\":%d,\"bits\":[%s,%s,%s,%s]}",
                          slot ? "," : "",
                          s_ctx.ids[slot],
                          s_ctx.ids[slot] > 0 ? "true" : "false",
                          grams_for_id_unlocked(s_ctx.ids[slot]),
                          s_ctx.bits[slot][0] ? "true" : "false",
                          s_ctx.bits[slot][1] ? "true" : "false",
                          s_ctx.bits[slot][2] ? "true" : "false",
                          s_ctx.bits[slot][3] ? "true" : "false");
    }
    pos = json_append(out, out_size, pos, "],\"reeds\":[");
    for (slot = 0; slot < 4; ++slot) {
        int bit;
        for (bit = 0; bit < 4; ++bit) {
            int idx = slot * 4 + bit;
            pos = json_append(out, out_size, pos,
                              "%s{\"slot\":%d,\"bit\":%d,\"pin\":%d,\"low\":%s}",
                              idx ? "," : "",
                              slot,
                              bit,
                              16 + idx,
                              s_ctx.bits[slot][bit] ? "true" : "false");
        }
    }
    pos = json_append(out, out_size, pos, "],\"keypadDown\":[");
    for (i = 0; i < 16; ++i) {
        if ((i % 4) == 0) {
            pos = json_append(out, out_size, pos, "%s[", i ? "," : "");
        }
        pos = json_append(out, out_size, pos, "%s%s", (i % 4) ? "," : "", s_ctx.keys_down[i] ? "true" : "false");
        if ((i % 4) == 3) {
            pos = json_append(out, out_size, pos, "]");
        }
    }
    pos = json_append(out, out_size, pos, "],\"codes\":[");
    for (i = 0; i < CODE_SET_COUNT; ++i) {
        char esc[48];
        lib_json_escape_string(s_ctx.cfg.codes[i].code, esc, sizeof(esc));
        pos = json_append(out, out_size, pos, "%s{\"label\":\"%s\",\"code\":\"%s\"}",
                          i ? "," : "", s_ctx.cfg.codes[i].label, esc);
    }
    {
        char esc_target[48];
        const char *target_code = "";
        for (i = 0; i < CODE_SET_COUNT; ++i) {
            if (s_ctx.cfg.codes[i].label[0] &&
                s_ctx.cfg.codes[i].label[0] == s_ctx.cfg.game_setting[0]) {
                target_code = s_ctx.cfg.codes[i].code;
                break;
            }
        }
        lib_json_escape_string(target_code, esc_target, sizeof(esc_target));
        (void)json_append(out, out_size, pos, "],\"targetCode\":\"%s\"}", esc_target);
    }
    dyn_unlock();
}

void dynamite_engine_get_config_json(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (!dyn_lock()) {
        out[0] = '\0';
        return;
    }
    fill_config_json(out, out_size, &s_ctx.cfg);
    dyn_unlock();
}

void dynamite_engine_get_default_config_json(char *out, size_t out_size)
{
    dyn_config_t defaults;
    set_default_config(&defaults);
    if (!out || out_size == 0) {
        return;
    }
    fill_config_json(out, out_size, &defaults);
}

esp_err_t dynamite_engine_handle_command_json(const char *json, char *response, size_t response_size)
{
    cJSON *root;
    char command[32] = "";
    char keypress_raw[8] = "";
    char keypress[4] = "";
    int maglock = -1;

    if (!json || !response || response_size < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dyn_lock()) {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }

    root = lib_json_parse(json);
    if (root) {
        cJSON *ml = cJSON_GetObjectItemCaseSensitive(root, "magLock");
        if (!ml) {
            ml = cJSON_GetObjectItemCaseSensitive(root, "MagLock");
        }
        if (cJSON_IsNumber(ml)) {
            maglock = ml->valueint;
        } else if (cJSON_IsBool(ml)) {
            maglock = cJSON_IsTrue(ml) ? 1 : 0;
        }
        (void)lib_json_get_string(root, "command", command, sizeof(command));
        if (command[0] == '\0') {
            (void)lib_json_get_string(root, "Command", command, sizeof(command));
        }
        if (!lib_json_get_string(root, "keypress", keypress_raw, sizeof(keypress_raw))) {
            (void)lib_json_get_string(root, "Keypress", keypress_raw, sizeof(keypress_raw));
        }
        cJSON_Delete(root);
    }

    record_mqtt_in_unlocked(json);

    if (maglock == 1 || strcmp(command, "unlockCabinet") == 0 ||
        strcmp(command, "openDoor") == 0 || strcmp(command, "openCabinet") == 0) {
        maglock_pulse_unlocked();
        snprintf(response, response_size, "{\"ok\":true,\"magLock\":1,\"pulseMs\":%d,\"Command\":\"openCabinet\"}",
                 maglock_pulse_ms_unlocked());
        dyn_unlock();
        return ESP_OK;
    }
    if (maglock == 0) {
        maglock_drop_unlocked(false);
        snprintf(response, response_size, "{\"ok\":true,\"magLock\":0}");
        dyn_unlock();
        return ESP_OK;
    }
    if (strcmp(command, "reportState") == 0) {
        publish_status_unlocked();
        snprintf(response, response_size, "{\"ok\":true,\"command\":\"reportState\"}");
        dyn_unlock();
        return ESP_OK;
    }
    if (strcmp(command, "solveCode") == 0) {
        gm_solve_code_unlocked(response, response_size);
        dyn_unlock();
        return ESP_OK;
    }
    if (strcmp(command, "sendCharges") == 0) {
        gm_send_charges_unlocked(response, response_size);
        dyn_unlock();
        return ESP_OK;
    }
    if (strcmp(command, "reset") == 0) {
        gm_reset_unlocked();
        snprintf(response, response_size, "{\"ok\":true,\"Command\":\"reset\"}");
        dyn_unlock();
        return ESP_OK;
    }
    if (sanitize_keypress(keypress_raw, keypress, sizeof(keypress))) {
        apply_virtual_key_unlocked(keypress);
        /* Physical/virtual * after a GM (or player) code solve still counts. */
        if (keypress[0] == '*' && s_ctx.code_solved) {
            queue_event_unlocked("{\"event\":\"sendCharges\"}");
        }
        snprintf(response, response_size, "{\"ok\":true,\"keypress\":\"%s\"}", keypress);
        dyn_unlock();
        return ESP_OK;
    }

    snprintf(response, response_size, "{\"ok\":false,\"error\":\"invalid_command\"}");
    dyn_unlock();
    return ESP_ERR_INVALID_ARG;
}

esp_err_t dynamite_engine_apply_config_json(const char *json, bool persist, char *response, size_t response_size)
{
    esp_err_t err = ESP_OK;

    if (!json || !response || response_size < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dyn_lock()) {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }
    apply_config_fields_unlocked(json);
    if (persist) {
        err = save_config_file_unlocked();
    }
    snprintf(response,
             response_size,
             "{\"ok\":%s,\"persisted\":%s,\"spiffs\":%s}",
             err == ESP_OK ? "true" : "false",
             persist ? "true" : "false",
             s_ctx.spiffs_ready ? "true" : "false");
    dyn_unlock();
    return ESP_OK;
}

esp_err_t dynamite_engine_restore_defaults(bool persist, char *response, size_t response_size)
{
    esp_err_t err = ESP_OK;

    if (!response || response_size < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dyn_lock()) {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }
    set_default_config(&s_ctx.cfg);
    if (persist) {
        err = save_config_file_unlocked();
    }
    snprintf(response,
             response_size,
             "{\"ok\":%s,\"restored\":true,\"persisted\":%s,\"spiffs\":%s}",
             err == ESP_OK ? "true" : "false",
             persist ? "true" : "false",
             s_ctx.spiffs_ready ? "true" : "false");
    dyn_unlock();
    return ESP_OK;
}

bool dynamite_engine_pop_event_json(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    if (!dyn_lock()) {
        return false;
    }
    if (s_ctx.event_tail == s_ctx.event_head) {
        dyn_unlock();
        return false;
    }
    copy_bounded(out, out_size, s_ctx.event_queue[s_ctx.event_tail]);
    s_ctx.event_tail = (s_ctx.event_tail + 1) % DYN_EVENT_QUEUE_LEN;
    dyn_unlock();
    return true;
}

bool dynamite_engine_pop_warning_message(char *out, size_t out_size)
{
    (void)out;
    (void)out_size;
    return false;
}

void dynamite_engine_notify_wifi_connected(int rssi)
{
    if (!dyn_lock()) {
        return;
    }
    s_ctx.wifi_connected = true;
    s_ctx.wifi_rssi = rssi;
    dyn_unlock();
}

void dynamite_engine_notify_wifi_disconnected(void)
{
    if (!dyn_lock()) {
        return;
    }
    s_ctx.wifi_connected = false;
    s_ctx.wifi_rssi = 0;
    s_ctx.wifi_ssid[0] = '\0';
    dyn_unlock();
}

void dynamite_engine_set_wifi_ssid(const char *ssid)
{
    if (!dyn_lock()) {
        return;
    }
    copy_bounded(s_ctx.wifi_ssid, sizeof(s_ctx.wifi_ssid), ssid ? ssid : "");
    dyn_unlock();
}

dynamite_led_hint_t dynamite_engine_get_led_hint(void)
{
    return DYNAMITE_LED_HINT_OFF;
}

void dynamite_engine_get_battery_snapshot(dynamite_battery_snapshot_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    copy_bounded(out->battery_profile, sizeof(out->battery_profile), "none");
}
