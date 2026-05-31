/*
 * OTA.c -- over-the-air firmware update over local WiFi.
 *
 * Implementation built on ESP-IDF's esp_https_ota convenience library
 * (works fine for plain HTTP too). The flow:
 *
 *   1) Spawn a one-shot ota_task on core 1.
 *   2) Task connects WiFi via ota_wifi_connect_blocking() (15 s tmo).
 *   3) Task hands a URL + http config to esp_https_ota_begin().
 *   4) Task loops esp_https_ota_perform() to download chunks, calling
 *      the progress callback with bytes-read / total-image-len * 100.
 *   5) On clean download, esp_https_ota_finish() validates + commits.
 *   6) Task fires CB(state=REBOOTING) and calls esp_restart().
 *   7) On next boot, OTA_Init()'s self-check marks the new slot valid.
 *
 * If anything fails along the way, esp_https_ota_abort() rolls things
 * back (the inactive slot stays in its previous state), the task fires
 * CB(state=FAILED, msg=reason), tears down WiFi, and exits.
 *
 * Rollback safety: if the new firmware crashes 3 times before reaching
 * OTA_Init() -> esp_ota_mark_app_valid_cancel_rollback(), the
 * bootloader silently reverts to the previous slot.  Configured via
 * CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y in sdkconfig.
 */
#include "OTA.h"
#include "ota_config.h"
#include "wifi_sta.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_app_format.h"

static const char *TAG = "OTA";

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static volatile bool        s_running   = false;
static volatile bool        s_cancel    = false;
static ota_progress_cb_t    s_cb        = NULL;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void fire(ota_state_t st, int percent, const char *msg)
{
    if (s_cb) s_cb(st, percent, msg);
}

/* ------------------------------------------------------------------ */
/* OTA task                                                            */
/* ------------------------------------------------------------------ */
static void ota_task(void *arg)
{
    (void)arg;

    /* --- Step 1: connect WiFi --- */
    fire(OTA_STATE_CONNECTING_WIFI, -1, "Connecting to WiFi");
    if (!ota_wifi_connect_blocking()) {
        ESP_LOGE(TAG, "WiFi connect failed");
        fire(OTA_STATE_FAILED, -1, "WiFi timeout");
        goto cleanup;
    }

    if (s_cancel) { fire(OTA_STATE_FAILED, -1, "Cancelled"); goto cleanup; }

    /* --- Step 2: open the HTTPS/HTTP connection --- */
    fire(OTA_STATE_DOWNLOADING, 0, "Connecting to server");
    esp_http_client_config_t http_cfg = {
        .url               = OTA_FIRMWARE_URL,
        .timeout_ms        = OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK || handle == NULL) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        fire(OTA_STATE_FAILED, -1, "Server unreachable");
        goto cleanup;
    }

    /* Image header check ensures the binary is a valid ESP32 app and
     * the chip ID matches.  Cheap protection against accidentally
     * pointing at the wrong file on the server. */
    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_img_desc failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        fire(OTA_STATE_FAILED, -1, "Invalid image");
        goto cleanup;
    }
    ESP_LOGI(TAG, "remote firmware: project='%s' version='%s' time=%s",
             app_desc.project_name, app_desc.version, app_desc.time);

    int total_size = esp_https_ota_get_image_size(handle);
    ESP_LOGI(TAG, "downloading %d bytes...", total_size);

    /* --- Step 3: chunked download loop --- */
    int last_pct = -1;
    while (1) {
        if (s_cancel) {
            esp_https_ota_abort(handle);
            fire(OTA_STATE_FAILED, -1, "Cancelled");
            goto cleanup;
        }
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int read = esp_https_ota_get_image_len_read(handle);
        if (total_size > 0) {
            int pct = (int)((int64_t)read * 100 / total_size);
            if (pct != last_pct) {
                fire(OTA_STATE_DOWNLOADING, pct, NULL);
                last_pct = pct;
            }
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        fire(OTA_STATE_FAILED, -1, "Download failed");
        goto cleanup;
    }

    /* --- Step 4: validate + commit --- */
    fire(OTA_STATE_INSTALLING, 100, "Installing");
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
        fire(OTA_STATE_FAILED, -1, "Install failed");
        goto cleanup;
    }
    ESP_LOGI(TAG, "OTA install OK, rebooting in 2 s");
    fire(OTA_STATE_REBOOTING, 100, "Rebooting");

    /* Small delay so the UI has a chance to render the REBOOTING
     * status before the chip resets. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    /* NOTREACHED */

cleanup:
    ota_wifi_disconnect();
    s_running = false;
    s_cancel = false;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */
void OTA_Init(void)
{
    /* On boot, if we just ran a freshly-installed OTA, the bootloader
     * has us in PENDING_VERIFY state.  If we don't call
     * esp_ota_mark_app_valid_cancel_rollback() before the next reset,
     * the bootloader assumes we crashed and rolls back to the previous
     * slot.  Calling it here means "we got far enough to run our init
     * code, so this firmware is good." */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK) {
        if (st == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "First boot of new firmware -- marking valid.");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    ESP_LOGI(TAG, "OTA module ready.  Running from partition '%s' @ 0x%lx",
             running->label, (unsigned long)running->address);
}

void OTA_CheckForUpdate(ota_progress_cb_t cb)
{
    if (s_running) {
        ESP_LOGW(TAG, "OTA already running, ignoring trigger");
        return;
    }
    s_running = true;
    s_cancel = false;
    s_cb = cb;
    xTaskCreatePinnedToCore(ota_task, "ota", 8192, NULL, 5, NULL, 1);
}

bool OTA_IsRunning(void) { return s_running; }

void OTA_Cancel(void)
{
    if (s_running) s_cancel = true;
}
