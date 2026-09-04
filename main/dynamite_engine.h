#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    DYNAMITE_LED_HINT_OFF = 0,
} dynamite_led_hint_t;

typedef struct {
    int wire_count;
    int battery_adc_raw;
    int battery_adc_at_0v;
    int battery_adc_at_15v;
    char battery_profile[24];
} dynamite_battery_snapshot_t;

esp_err_t dynamite_engine_init(void);

#define DYN_STATE_JSON_MAX 8192

void dynamite_engine_get_state_json(char *out, size_t out_size);
void dynamite_engine_get_mqtt_status_json(char *out, size_t out_size);
void dynamite_engine_get_config_json(char *out, size_t out_size);
void dynamite_engine_get_default_config_json(char *out, size_t out_size);

esp_err_t dynamite_engine_handle_command_json(const char *json, char *response, size_t response_size);
esp_err_t dynamite_engine_apply_config_json(const char *json, bool persist, char *response, size_t response_size);
esp_err_t dynamite_engine_restore_defaults(bool persist, char *response, size_t response_size);

bool dynamite_engine_pop_event_json(char *out, size_t out_size);
bool dynamite_engine_pop_warning_message(char *out, size_t out_size);

void dynamite_engine_notify_wifi_connected(int rssi);
void dynamite_engine_notify_wifi_disconnected(void);
void dynamite_engine_set_wifi_ssid(const char *ssid);

dynamite_led_hint_t dynamite_engine_get_led_hint(void);
void dynamite_engine_get_battery_snapshot(dynamite_battery_snapshot_t *out);
