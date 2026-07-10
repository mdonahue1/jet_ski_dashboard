#ifndef ST7796_H
#define ST7796_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* Logical display size for the LCDWiki/Hosyond ST7796S panel in landscape mode. */
#define ST7796_WIDTH   480U
#define ST7796_HEIGHT  320U

/* Common RGB565 colors. RGB565 layout is: 5 red bits, 6 green bits, 5 blue bits. */
#define ST7796_BLACK   0x0000U
#define ST7796_BLUE    0x001FU
#define ST7796_GREEN   0x07E0U
#define ST7796_CYAN    0x07FFU
#define ST7796_RED     0xF800U
#define ST7796_MAGENTA 0xF81FU
#define ST7796_YELLOW  0xFFE0U
#define ST7796_WHITE   0xFFFFU

/* Run the controller reset/init sequence and clear the screen. */
void ST7796_Init(void);

/* Select the rectangular GRAM region that following pixel bytes will write into. */
void ST7796_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* Fill the whole screen with one RGB565 color. */
void ST7796_Fill(uint16_t color);

/*
 * Fill a clipped rectangle. This is the main primitive used by the dashboard UI.
 * The project draws directly over SPI instead of keeping a full-screen framebuffer.
 */
void ST7796_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* Draw one pixel. Useful for diagnostics or tiny details, but slow for large graphics. */
void ST7796_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif /* ST7796_H */
