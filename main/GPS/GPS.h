/*
 * GPS.h -- BN-880 (or any standard NMEA GPS) UART receiver.
 *
 * Hardware on the Waveshare ESP32-S3-Touch-LCD-2.1:
 *   - JST UART connector breaks out UART0 (GPIO 43 = TXD, GPIO 44 = RXD)
 *   - Same UART0 lines are also routed to the on-board USB-to-UART
 *     bridge / secondary USB-C. Don't plug both at once -- bus contention.
 *   - Console / log output is redirected to USB-Serial-JTAG (primary
 *     USB-C) via sdkconfig so UART0 is fully ours.
 *
 * BN-880 wiring (6 wires, 2 are NC for the unused magnetometer):
 *   Red  -> JST 3V3
 *   Black-> JST GND
 *   Green-> JST RXD  (GPS-TX into ESP32-RX, NMEA stream)
 *   White-> JST TXD  (GPS-RX from ESP32-TX, only needed for config cmds)
 *
 * Behavior:
 *   - GPS_Init() spawns a background task that reads NMEA sentences from
 *     UART0 at 9600 baud, parses $GNRMC / $GPRMC for ground speed, and
 *     pushes results to the speed gauge via set_gauge_speed_mph() and
 *     set_gauge_speed_status().
 *   - Speed is in MPH (BN-880 reports knots; we convert at parse time).
 *   - "NO FIX" is signaled by set_gauge_speed_mph(-1.0f) so the speed
 *     gauge widget shows "---" instead of a stale value.
 */
#ifndef GPS_H
#define GPS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn the GPS reader task. Call once at boot, after LVGL_Init() so the
 * speed gauge widgets exist for set_gauge_speed_mph() to update. */
void GPS_Init(void);

/* Last parsed ground speed in MPH. Returns negative if no current fix. */
float GPS_GetSpeedMph(void);

/* True if the last $GNRMC/$GPRMC sentence reported a valid fix (status
 * field == 'A'). */
bool  GPS_HasFix(void);

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */
