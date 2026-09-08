#include "px_system.h"
#include "dynamite_engine.h"
#include "web_ui.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "px-dynamite-v1";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    ESP_LOGI(TAG, "Starting px-dynamite-v1");
    ESP_LOGI(TAG, "Build info: id=%s date=%s time=%s", app->version, app->date, app->time);

    ESP_ERROR_CHECK(px_system_init());
    {
        esp_err_t err = dynamite_engine_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "dynamite engine init failed (%s) — continuing so Wi-Fi/UI still come up",
                     esp_err_to_name(err));
        }
    }
    /* SoftAP/STA before any MCP I/O so a wedged SPI bus cannot hide the console. */
    ESP_ERROR_CHECK(web_ui_start());
    {
        esp_err_t err = dynamite_engine_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "dynamite I/O loop failed to start (%s)", esp_err_to_name(err));
        }
    }

    /* Confirm OTA image so a later crash does not roll back to a bad slot. */
    esp_ota_mark_app_valid_cancel_rollback();
}
