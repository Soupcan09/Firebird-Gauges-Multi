/*
 * GPS.c -- BN-880 NMEA receiver and parser.
 *
 * UART driver runs at 9600 baud on UART0 (GPIO 43 TX, GPIO 44 RX), the
 * same pins as the JST UART connector and the on-board USB-to-UART
 * bridge. Console output is redirected to USB-Serial-JTAG via
 * sdkconfig so we can take ownership of UART0.
 *
 * NMEA parsing strategy:
 *   - Drain bytes off the UART and accumulate into a line buffer until
 *     we hit '\n'.
 *   - On end-of-line, check the sentence type. Only $GNRMC and $GPRMC
 *     are interesting (they carry speed-over-ground + fix status).
 *   - Verify the trailing *XX checksum so a torn / electrically-noisy
 *     line gets rejected instead of producing a wrong speed.
 *   - Pull field 7 (speed in knots) and field 2 (fix valid 'A' / 'V').
 *   - Convert knots -> MPH (knots * 1.15078) and push to the speed
 *     gauge via set_gauge_speed_mph() + set_gauge_speed_status().
 *
 * BN-880 default config: 9600 baud, 1 Hz update rate, all NMEA talkers
 * enabled. We only listen -- no UBX configuration commands sent, so the
 * GPS-RX (white) wire is harmless even though our UART0 TX line spits
 * occasional bytes (the BN-880 ignores anything that isn't a valid
 * command sequence).
 */
#include "GPS.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "speed_gauge.h"

static const char *TAG = "GPS";

/* ------------------------------------------------------------------ */
/* Hardware config                                                     */
/* ------------------------------------------------------------------ */
/* We use UART1 (not UART0) and route it via the GPIO matrix to the
 * same physical JST pins (GPIO 43/44).  Originally tried UART0 because
 * it has IOMUX direct access to those pins, but even after disabling
 * the console on UART0 in sdkconfig the peripheral wasn't producing
 * any received bytes (verified by directly grounding the chip's RX
 * line to no effect).  UART1 is a separate peripheral with no console
 * history and the GPIO matrix can route it to any GPIO -- a tiny
 * latency penalty at 9600 baud, irrelevant in practice. */
#define GPS_UART_NUM        UART_NUM_1
#define GPS_UART_TX_PIN     43            /* JST TXD pin (chip TX)      */
#define GPS_UART_RX_PIN     44            /* JST RXD pin (chip RX)      */
#define GPS_UART_BAUD       9600          /* BN-880 default             */
#define GPS_RX_BUF_SIZE     1024          /* UART driver ring buffer    */
#define GPS_LINE_BUF_SIZE   128           /* one NMEA sentence max ~82  */

#define KNOTS_TO_MPH        1.15077945f

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static float        s_speed_mph   = -1.0f; /* negative = no fix        */
static bool         s_has_fix     = false;
static int          s_sats_used   = 0;     /* sats USED in current fix */
static TaskHandle_t s_task        = NULL;

/* ------------------------------------------------------------------ */
/* Public getters                                                      */
/* ------------------------------------------------------------------ */
float GPS_GetSpeedMph(void) { return s_speed_mph; }
bool  GPS_HasFix(void)      { return s_has_fix; }

/* ------------------------------------------------------------------ */
/* NMEA helpers                                                        */
/* ------------------------------------------------------------------ */
/* Verify the *XX checksum at the end of an NMEA sentence.
 * NMEA checksum is the XOR of every byte between '$' and '*',
 * exclusive of both. Returns true on a valid sentence with a matching
 * checksum, false on malformed input.
 *
 * `line` should be the full sentence INCLUDING the leading '$' and
 * the trailing '*XX', NOT including the CR/LF. */
static bool nmea_checksum_ok(const char *line, size_t len)
{
    if (len < 4) return false;            /* "$X*XX" minimum            */
    if (line[0] != '$') return false;
    /* Find the '*' that separates payload from checksum. */
    const char *star = memchr(line, '*', len);
    if (!star) return false;
    size_t payload_len = (size_t)(star - line) - 1;   /* skip '$'       */
    if (star + 3 > line + len) return false;          /* need 2 hex     */

    uint8_t calc = 0;
    for (size_t i = 1; i < 1 + payload_len; ++i) {
        calc ^= (uint8_t)line[i];
    }

    /* Two ASCII hex digits after '*'. */
    char hex[3] = { star[1], star[2], 0 };
    char *endp = NULL;
    unsigned long given = strtoul(hex, &endp, 16);
    if (endp != hex + 2) return false;
    return (uint8_t)given == calc;
}

/* Split an NMEA sentence into its comma-separated fields IN PLACE.
 * Writes nul terminators at every comma + at the trailing '*' so the
 * checksum is also clipped off. `out_fields` gets pointers to the start
 * of each field (including empty fields between adjacent commas).
 * Returns the count of fields written, or 0 on error.
 *
 * We do this in one pass instead of multiple nmea_field(line, n) calls
 * because the first call mutates the string and breaks subsequent
 * lookups -- early bug fix. */
#define NMEA_MAX_FIELDS  20
static int nmea_split(char *line, char **out_fields, int max)
{
    if (!line || max <= 0) return 0;
    int n = 0;
    out_fields[n++] = line;
    for (char *p = line; *p; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (n < max) out_fields[n++] = p + 1;
        } else if (*p == '*') {
            *p = '\0';
            break;
        }
    }
    return n;
}

/* Format the status footer text and push it to the speed gauge. The
 * three states the user sees:
 *
 *   "NO FIX"       -- haven't even seen a satellite yet (boot, indoors,
 *                     or behind a roof)
 *   "TRACKING N"   -- GGA reports N satellites visible but RMC still
 *                     says 'V' (no usable fix yet -- GPS is hunting)
 *   "FIX N SATS"   -- RMC says 'A' and N satellites are contributing
 *                     to the fix; speed digit above is now live
 *
 * Showing the satellite count even before fix lets the user see that
 * the GPS is alive and progressing, instead of staring at a flat
 * "NO FIX" wondering whether it's broken.
 */
static void publish_status(void)
{
    char buf[32];
    if (s_has_fix) {
        snprintf(buf, sizeof(buf), "FIX %d SATS", s_sats_used);
    } else if (s_sats_used > 0) {
        snprintf(buf, sizeof(buf), "TRACKING %d", s_sats_used);
    } else {
        snprintf(buf, sizeof(buf), "NO FIX");
    }
    set_gauge_speed_status(buf);
}

/* Parse a $GxRMC sentence -- gives us speed + fix status. */
static void parse_rmc(char **fields, int nfields)
{
    if (nfields < 8) return;
    /* RMC field layout:
     *   2: status A=active V=void
     *   7: speed over ground, knots
     */
    bool fix = (fields[2] && fields[2][0] == 'A');

    if (fix != s_has_fix) {
        ESP_LOGI(TAG, "fix %s", fix ? "ACQUIRED" : "LOST");
    }
    s_has_fix = fix;

    if (fix && fields[7] && fields[7][0] != '\0') {
        float knots = strtof(fields[7], NULL);
        s_speed_mph = knots * KNOTS_TO_MPH;
        set_gauge_speed_mph(s_speed_mph);
    } else {
        s_speed_mph = -1.0f;
        set_gauge_speed_mph(-1.0f);
    }
}

/* Parse a $GxGGA sentence -- gives us satellite count + fix quality. */
static void parse_gga(char **fields, int nfields)
{
    if (nfields < 8) return;
    /* GGA field layout:
     *   6: fix quality (0=invalid, 1=GPS, 2=DGPS, etc.)
     *   7: number of satellites used in the fix
     *   8: HDOP
     */
    if (fields[7] && fields[7][0] != '\0') {
        s_sats_used = atoi(fields[7]);
    }
}

/* Parse a single NMEA line and update GPS state. */
static void parse_nmea_line(char *line, size_t len)
{
    /* Strip trailing CR if present (we split on LF). */
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
        line[--len] = '\0';
    }
    if (len < 6) return;                  /* "$xxxxx" min               */
    if (line[0] != '$') return;

    /* Detect sentence type. NMEA "talker" prefix varies: GP (GPS only),
     * GN (GNSS multi-system, what BN-880 emits with GLONASS+BeiDou
     * enabled), GL (GLONASS), etc. We accept either GP or GN for the
     * sentences we care about. */
    bool is_rmc = (strncmp(line + 1, "GPRMC", 5) == 0 ||
                   strncmp(line + 1, "GNRMC", 5) == 0);
    bool is_gga = (strncmp(line + 1, "GPGGA", 5) == 0 ||
                   strncmp(line + 1, "GNGGA", 5) == 0);
    if (!is_rmc && !is_gga) return;

    if (!nmea_checksum_ok(line, len)) {
        ESP_LOGD(TAG, "bad checksum: %.*s", (int)len, line);
        return;
    }

    /* Working copy because nmea_split() inserts nul terminators. */
    char buf[GPS_LINE_BUF_SIZE];
    if (len >= sizeof(buf)) return;
    memcpy(buf, line, len);
    buf[len] = '\0';

    char *fields[NMEA_MAX_FIELDS];
    int   nfields = nmea_split(buf, fields, NMEA_MAX_FIELDS);

    if (is_rmc)      parse_rmc(fields, nfields);
    else if (is_gga) parse_gga(fields, nfields);

    /* Refresh the gauge footer after every successfully-parsed sentence
     * so the user sees the satellite count update at ~5 Hz (the BN-880
     * emits all sentence types together once per second, but each comes
     * in independently here so we update on each). */
    publish_status();
}

/* ------------------------------------------------------------------ */
/* RX task                                                             */
/* ------------------------------------------------------------------ */
static void gps_task(void *arg)
{
    (void)arg;
    static char    line_buf[GPS_LINE_BUF_SIZE];
    static size_t  line_len = 0;
    uint8_t        chunk[64];

    /* Diagnostic counters retained so a future "GPS dropped out, why?"
     * debug session has something to grep for in monitor logs. Counters
     * print at LOGD (off by default in production). Set the GPS log
     * level to DEBUG via menuconfig (or esp_log_level_set("GPS",
     * ESP_LOG_DEBUG)) to re-enable the verbose stream. */
    uint32_t bytes_total      = 0;
    uint32_t lines_total      = 0;
    uint32_t last_report_ms   = 0;

    ESP_LOGI(TAG, "GPS task running, UART%d @ %d baud", GPS_UART_NUM, GPS_UART_BAUD);

    while (1) {
        int n = uart_read_bytes(GPS_UART_NUM, chunk, sizeof(chunk),
                                pdMS_TO_TICKS(100));

        /* Periodic stats at LOGD only -- no impact on production
         * monitor unless the user explicitly raises the GPS log level. */
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now_ms - last_report_ms >= 5000) {
            ESP_LOGD(TAG, "rx stats: %u bytes, %u lines so far",
                     (unsigned)bytes_total, (unsigned)lines_total);
            last_report_ms = now_ms;
        }

        if (n <= 0) continue;
        bytes_total += (uint32_t)n;

        for (int i = 0; i < n; ++i) {
            uint8_t b = chunk[i];
            if (b == '\n') {
                /* End of line -- parse what we have, then reset. */
                if (line_len > 0) {
                    line_buf[line_len] = '\0';
                    /* Per-line NMEA echoes are LOGD only -- they fired
                     * 5+ times per second during bring-up which would
                     * drown out everything else in the monitor. The
                     * parser still updates the gauge silently. */
                    ESP_LOGD(TAG, "NMEA: %.*s", (int)line_len, line_buf);
                    parse_nmea_line(line_buf, line_len);
                    lines_total++;
                }
                line_len = 0;
            } else if (b == '\r') {
                /* Drop CR; LF is the real terminator. */
            } else if (line_len < sizeof(line_buf) - 1) {
                line_buf[line_len++] = (char)b;
            } else {
                /* Overflow -- garbage data, drop the line. */
                line_len = 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
void GPS_Init(void)
{
    ESP_LOGI(TAG, "GPS init: UART%d, RX=GPIO%d, TX=GPIO%d, baud=%d",
             GPS_UART_NUM, GPS_UART_RX_PIN, GPS_UART_TX_PIN, GPS_UART_BAUD);

    const uart_config_t cfg = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Reset GPIO 43/44 state before assigning them to UART1.  An earlier
     * attempt used UART0 (which has IOMUX direct access to these pins)
     * but couldn't actually receive any bytes -- something downstream
     * of the console reconfig left UART0 in a half-deaf state.
     * Explicitly resetting clears whatever residual peripheral binding
     * was on these pins before we route them to UART1 via the GPIO
     * matrix. */
    gpio_reset_pin((gpio_num_t)GPS_UART_RX_PIN);
    gpio_reset_pin((gpio_num_t)GPS_UART_TX_PIN);

    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, GPS_RX_BUF_SIZE * 2,
                                        0 /* no TX buffer */,
                                        0 /* no event queue */,
                                        NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM,
                                 GPS_UART_TX_PIN,
                                 GPS_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    /* Drop initial garbage that's already buffered (powerup transients). */
    uart_flush(GPS_UART_NUM);

    /* Spawn the parser task. 4 KB stack is plenty for line buffer + the
     * float parsing. Pin to core 1 so the LVGL task on core 0 isn't
     * disturbed by GPS work. */
    xTaskCreatePinnedToCore(gps_task, "gps", 4096, NULL, 5, &s_task, 1);
}
