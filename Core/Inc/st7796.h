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

/*
 * Pack 8-bit red/green/blue values into RGB565. This is useful when a UI wants
 * a custom color without hand-calculating the 16-bit value.
 */
uint16_t ST7796_RGB565(uint8_t r, uint8_t g, uint8_t b);

/* Return a dimmer version of an RGB565 color. amount=0 is black; 255 is unchanged. */
uint16_t ST7796_DimColor(uint16_t color, uint8_t amount);

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

/*
 * Draw a straight line using integer Bresenham math. Useful for needles, ticks,
 * dividers, and simple icons. Horizontal/vertical lines are faster as FillRect.
 */
void ST7796_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/* Draw only the outline of a circle. */
void ST7796_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/* Fill a circle. Useful for round gauge dots, LEDs, and rounded-corner helpers. */
void ST7796_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/*
 * Fill a rounded rectangle by combining three rectangles and four filled
 * circles. Radius is clipped so it never exceeds half the rectangle size.
 */
void ST7796_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color);

/*
 * Draw the outline of a rounded rectangle. The thickness argument is in pixels.
 * Good for data tiles, gauge bezels, and status panels.
 */
void ST7796_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t thickness, uint16_t color);

/* Draw a thicker line by stamping small circles along a normal Bresenham line. */
void ST7796_DrawThickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, uint16_t color);

/*
 * Angle convention for arc/gauge helpers:
 *   0 degrees   = right
 *   90 degrees  = down
 *   180 degrees = left
 *   270 degrees = up
 * This matches screen coordinates where Y increases downward.
 */
void ST7796_PointOnCircle(int16_t cx, int16_t cy, int16_t r, int16_t angle_deg, int16_t *x, int16_t *y);
void ST7796_DrawArc(int16_t cx, int16_t cy, int16_t r, int16_t start_deg, int16_t end_deg, uint8_t step_deg, uint16_t color);
void ST7796_DrawThickArc(int16_t cx, int16_t cy, int16_t r, int16_t start_deg, int16_t end_deg, uint8_t thickness, uint8_t step_deg, uint16_t color);
void ST7796_DrawRadialTick(int16_t cx, int16_t cy, int16_t inner_r, int16_t outer_r, int16_t angle_deg, uint8_t thickness, uint16_t color);
void ST7796_DrawGaugeTicks(int16_t cx, int16_t cy, int16_t inner_r, int16_t outer_r, int16_t start_deg, int16_t end_deg, uint8_t count, uint8_t major_every, uint16_t minor_color, uint16_t major_color);

/* Simple dashboard widgets inspired by ECU/digital dash layouts. */
void ST7796_DrawNeedle(int16_t cx, int16_t cy, int16_t r, int16_t angle_deg, uint8_t thickness, uint16_t color);
void ST7796_DrawLedBar(uint16_t x, uint16_t y, uint8_t count, uint8_t lit_count, uint8_t radius, uint8_t gap, uint16_t off_color, uint16_t low_color, uint16_t mid_color, uint16_t high_color);
void ST7796_FillHorizontalBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t percent, uint16_t fill_color, uint16_t bg_color, uint16_t border_color);

/*
 * Tiny built-in 5x7 bitmap text. Good for labels, diagnostics, and menus.
 * Lowercase letters are drawn as uppercase to keep the font table small.
 * scale=1 gives 5x7 pixel characters; scale=2 gives 10x14, etc.
 * bg_color is only used when draw_bg is nonzero.
 */
void ST7796_DrawChar5x7(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color, uint8_t scale, uint8_t draw_bg);
void ST7796_DrawString5x7(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg_color, uint8_t scale, uint8_t draw_bg);
uint16_t ST7796_TextWidth5x7(const char *text, uint8_t scale);

#ifdef __cplusplus
}
#endif

#endif /* ST7796_H */
