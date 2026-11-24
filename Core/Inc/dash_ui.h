#ifndef DASH_UI_H
#define DASH_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Paint the fixed dashboard parts: background, borders, label, and scale. */
void DashUI_Init(void);

/* Update the speed readout. The UI code decides what actually needs repainting. */
void DashUI_UpdateSpeed(uint16_t mph);

/* Replace stale speed digits with a cached loss-of-fix message. */
void DashUI_ShowNoGps(void);

/* Display GPS true course-over-ground on the normal gauge screen. */
void DashUI_UpdateHeading(uint8_t valid, uint16_t heading_deg);

/* Show a tiny GPS diagnostic strip: byte RX, complete NMEA lines, and valid fix. */
void DashUI_UpdateGpsStatus(uint8_t has_rx, uint8_t has_line, uint8_t has_fix);

/* Report a card only after SPI initialization and a sector read succeed. */
void DashUI_UpdateSdStatus(uint8_t card_ready);

/* Show whether valid heartbeat lines are arriving from the XIAO ESP32-C6. */
void DashUI_UpdateEspStatus(uint8_t esp_online);

/* Show the AP count reported by the latest passive ESP32 scan. */
void DashUI_UpdateWifiCount(uint16_t network_count);

/* Select 0=gauge, 1=Wi-Fi survey, or 2=satellite quality screen. */
void DashUI_SetCustomMode(uint8_t mode);

/* Update secondary counters shown only on the Wi-Fi survey screen. */
void DashUI_UpdateWardriveStats(uint32_t scan_id, uint32_t uart_line_count);

/* Show GPS position metadata on the dedicated Wi-Fi survey screen. */
void DashUI_UpdateWardriveGps(uint8_t position_valid,
                              int32_t latitude_e7,
                              int32_t longitude_e7,
                              uint8_t altitude_valid,
                              int16_t altitude_m,
                              uint8_t satellite_count);

/* Update GSA/GSV quality and signal information on satellite mode. */
void DashUI_UpdateSatelliteMode(uint8_t fix_type,
                                uint8_t satellites_used,
                                uint8_t satellites_in_view,
                                uint16_t hdop_x100,
                                uint16_t pdop_x100,
                                uint16_t vdop_x100,
                                const uint8_t strongest_snr[4]);

#ifdef __cplusplus
}
#endif

#endif /* DASH_UI_H */
