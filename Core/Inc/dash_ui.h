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

/* Show a tiny GPS diagnostic strip: byte RX, complete NMEA lines, and valid fix. */
void DashUI_UpdateGpsStatus(uint8_t has_rx, uint8_t has_line, uint8_t has_fix);

#ifdef __cplusplus
}
#endif

#endif /* DASH_UI_H */
