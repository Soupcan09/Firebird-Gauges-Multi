/*
 * wifi_sta.c -- on-demand WiFi STA connect/disconnect.
 *
 * Wireless.c already initializes the WiFi driver and sets it to STA
 * mode at boot, but doesn't try to associate with any specific AP.
 * This module fills that gap: when the user taps "CHECK UPDATE" in
 * settings we configure the credentials, call esp_wifi_connect(), and
 * block on an IP_EVENT_STA_GOT_IP event up to a timeout.
 *
 * Event handler is registered once on first use and kept around -- it
 * just toggles flags that the wait loop polls.  Keeps things simple,
 * avoids a queue/semaphore allocation that lives forever in PSRAM.
 */
#include "wifi_sta.h"
#include "ota_config.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "WIFI_STA";

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static volatile bool s_got_ip          = false;
static volatile bool s_disconnected    = false;
static volatile bool s_handlers_added  = false;

/* ------------------------------------------------------------------ */
/* Event handlers                                                      */
/* ------------------------------------------------------------------ */
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t event_id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START -> esp_wifi_connect()");
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        s_disconnected = true;
        s_got_ip = false;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t event_id, void *data)
{
    (void)arg; (void)base;
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *got = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: " IPSTR,
                 IP2STR(&got->ip_info.ip));
        s_got_ip = true;
        s_disconnected = false;
    }
}

static void ensure_handlers_registered(void)
{
    if (s_handlers_added) return;
    /* Wireless.c already called esp_event_loop_create_default() so we
     * just register callbacks on the existing loop. */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               on_ip_event, NULL);
    s_handlers_added = true;
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */
bool ota_wifi_connect_blocking(void)
{
    ensure_handlers_registered();
    s_got_ip = false;
    s_disconnected = false;

    /* Apply credentials. esp_wifi_set_config() requires WiFi to be in
     * STA mode (Wireless.c already does this) and that we re-call
     * esp_wifi_connect() after the config change.  We don't stop the
     * radio between calls -- the existing scanning code shares it. */
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid,     OTA_WIFI_SSID,     sizeof(cfg.sta.ssid)     - 1);
    strncpy((char *)cfg.sta.password, OTA_WIFI_PASSWORD, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "associating with SSID '%s'", OTA_WIFI_SSID);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();

    /* Poll-wait for the IP_EVENT_STA_GOT_IP flag.  100 ms ticks keeps
     * the lvgl task on the other core happy. */
    const uint32_t step_ms = 100;
    uint32_t waited_ms = 0;
    while (!s_got_ip && waited_ms < OTA_WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited_ms += step_ms;
    }

    if (s_got_ip) {
        ESP_LOGI(TAG, "associated in %u ms", (unsigned)waited_ms);
        return true;
    }
    ESP_LOGW(TAG, "association timed out after %u ms", (unsigned)waited_ms);
    return false;
}

void ota_wifi_disconnect(void)
{
    ESP_LOGI(TAG, "disconnecting STA");
    esp_wifi_disconnect();
    s_got_ip = false;
}

bool ota_wifi_is_connected(void)
{
    return s_got_ip;
}
