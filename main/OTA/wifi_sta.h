/*
 * wifi_sta.h -- on-demand WiFi STA association.
 *
 * The existing Wireless.c module starts WiFi at boot and uses it for
 * scanning. ota_wifi_connect_blocking() takes over from that and
 * actually associates with the AP named in ota_config.h, returning
 * once the gauge has an IP address (or after the timeout).
 *
 * Called by OTA_CheckForUpdate() right before kicking off the HTTPS
 * download. We don't keep WiFi associated all the time -- a parked
 * car wastes power, and the user is not expected to do anything that
 * needs connectivity except OTA. After the update completes (or
 * fails) we disconnect again.
 */
#ifndef WIFI_STA_H
#define WIFI_STA_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Associate with the AP specified in ota_config.h.  Blocks (with a
 * timeout from ota_config.h) until either:
 *   - we have an IPv4 address from DHCP  -> returns true
 *   - the timeout elapses                -> returns false
 *   - WiFi reports a permanent error      -> returns false
 */
bool ota_wifi_connect_blocking(void);

/* Tear down the STA association.  Safe to call even if not connected.
 * Leaves WiFi initialized so the existing scanning code in Wireless.c
 * still works. */
void ota_wifi_disconnect(void);

/* Helper for UI status: returns true if currently associated and has
 * an IP.  Reads cached state, doesn't probe the radio. */
bool ota_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STA_H */
