/*
 * OTA.h -- over-the-air firmware update over local WiFi.
 *
 * Workflow from the user's perspective:
 *   1) Tap CHECK UPDATE in settings.
 *   2) Gauge connects to home WiFi (credentials in ota_config.h).
 *   3) Gauge downloads firmware from your PC's http.server.
 *   4) Gauge installs into the inactive OTA slot, reboots, runs from
 *      the new slot.  If the new firmware crashes 3 times in a row the
 *      bootloader rolls back to the previous slot automatically.
 *
 * Callback fires from the OTA task so the UI can update progress.
 * Calls happen from outside the LVGL task -- use lv_async_call() or
 * a flag the LVGL task polls to safely update widgets.
 */
#ifndef OTA_H
#define OTA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CONNECTING_WIFI,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_INSTALLING,
    OTA_STATE_REBOOTING,     /* about to reboot into the new slot      */
    OTA_STATE_SUCCESS,       /* (set on the new boot after self-check) */
    OTA_STATE_FAILED,
} ota_state_t;

typedef void (*ota_progress_cb_t)(ota_state_t state,
                                  int percent,        /* 0-100; -1 if unknown */
                                  const char *msg);   /* short status string  */

/* One-time module init. Call once at app_main() after LVGL is up so
 * the rollback self-check can mark the running firmware as valid. */
void OTA_Init(void);

/* Kick off an update check.  Runs on a background task -- returns
 * immediately.  `cb` is invoked at each state transition (and during
 * download for percent progress).  Pass NULL if you don't care. */
void OTA_CheckForUpdate(ota_progress_cb_t cb);

/* True if an OTA is currently in progress.  Disable the CHECK UPDATE
 * button while this is true to avoid double-tap launches. */
bool OTA_IsRunning(void);

/* Cancel the in-flight update.  Best-effort -- the HTTP download will
 * abort at the next chunk read.  Safe to call from the UI thread. */
void OTA_Cancel(void);

/* Start the background auto-poll task.  Every interval_ms milliseconds
 * the task connects WiFi, opens an HTTP connection to the firmware
 * server, reads just the embedded esp_app_desc header, and compares
 * its compile-time string against the running firmware.  If they
 * differ -> install + reboot.  If they match -> abort + disconnect
 * WiFi + sleep until the next interval.
 *
 * Designed for "Jeff just rebuilt and ran the server, gauges pick up
 * the new firmware within ~30 s automatically" workflow.  Skips the
 * manual button-press loop.  The manual OTA_CheckForUpdate() button
 * still works for forcing an update RIGHT NOW.
 *
 * Auto-poll is silently a no-op while a manual update is in flight
 * (OTA_IsRunning() == true) so there's never a collision. */
void OTA_StartAutoPoll(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */
