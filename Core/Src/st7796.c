#include "st7796.h"

extern SPI_HandleTypeDef hspi1;

#define ST7796_SPI_TIMEOUT 100U

/*
 * MADCTL is the display controller's memory-access-control register. It decides
 * scan direction and RGB/BGR ordering. This value is the landscape orientation
 * that makes the logical screen 480 wide by 320 high.
 */
#define ST7796_MADCTL_HORIZONTAL 0x28u

/*
 * We do not allocate a full frame buffer. A 480x320 RGB565 frame would be
 * 307,200 bytes, far more RAM than this Nucleo has. Instead, large fills reuse
 * a small repeated-color SPI buffer.
 */
#define ST7796_FILL_CHUNK_PIXELS 128U
#define ST7796_FONT5X7_W 5U
#define ST7796_FONT5X7_H 7U
#define ST7796_FONT5X7_SPACING 1U

/*
 * CS is the display's chip-select. Keep it low while one logical TFT
 * transaction is active so the controller treats the bytes as one operation.
 */
static void ST7796_Select(void)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
}

/* Releasing CS tells the display this command/data burst is finished. */
static void ST7796_Unselect(void)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

uint16_t ST7796_RGB565(uint8_t r, uint8_t g, uint8_t b)
{
  /*
   * RGB565 stores red in 5 bits, green in 6 bits, and blue in 5 bits:
   * RRRR RGGG GGGB BBBB. Shifting discards the low color bits that the TFT
   * cannot display in 16-bit pixel mode.
   */
  return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                    ((uint16_t)(g & 0xFCU) << 3) |
                    ((uint16_t)b >> 3));
}

uint16_t ST7796_DimColor(uint16_t color, uint8_t amount)
{
  uint16_t r;
  uint16_t g;
  uint16_t b;

  /*
   * Expand RGB565 channels into integer ranges, scale them, then pack them
   * back down. This is handy for "off" LEDs and dim bezels.
   */
  r = (uint16_t)((color >> 11) & 0x1FU);
  g = (uint16_t)((color >> 5) & 0x3FU);
  b = (uint16_t)(color & 0x1FU);

  r = (uint16_t)((r * amount) / 255U);
  g = (uint16_t)((g * amount) / 255U);
  b = (uint16_t)((b * amount) / 255U);

  return (uint16_t)((r << 11) | (g << 5) | b);
}

/* DC low means the next SPI byte is a command/register address. */
static void ST7796_CommandMode(void)
{
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
}

/* DC high means the following SPI bytes are data for the previous command. */
static void ST7796_DataMode(void)
{
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
}

/* Hardware reset puts the controller in a known state before the init sequence. */
static void ST7796_Reset(void)
{
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(120);
}

static void ST7796_WriteCommand(uint8_t command)
{
  /*
   * A command is usually a register address inside the ST7796S controller.
   * DC low tells the TFT that this byte is a command, not pixel data.
   */
  ST7796_CommandMode();
  (void)HAL_SPI_Transmit(&hspi1, &command, 1U, ST7796_SPI_TIMEOUT);
}

/* Write a raw data buffer. HAL wants a non-const pointer, so the cast is local here. */
static void ST7796_WriteData(const uint8_t *data, uint16_t size)
{
  /*
   * Data bytes are parameters for the previous command, or raw pixel bytes
   * after command 0x2C. DC high selects data mode.
   */
  ST7796_DataMode();
  (void)HAL_SPI_Transmit(&hspi1, (uint8_t *)data, size, ST7796_SPI_TIMEOUT);
}

/* Most init commands are "command byte followed by N data bytes", so this keeps that pattern tidy. */
static void ST7796_WriteRegister(uint8_t command, const uint8_t *data, uint16_t size)
{
  ST7796_WriteCommand(command);
  if (size > 0U)
  {
    ST7796_WriteData(data, size);
  }
}

void ST7796_Init(void)
{
  /*
   * These values are ported from the LCDWiki ST7796S hardware-SPI init flow.
   * Most TFT controllers need a vendor-specific startup sequence for power,
   * gamma, porch timing, pixel format, and orientation before they draw well.
   */
  static const uint8_t f0_unlock_1[] = { 0xC3 };
  static const uint8_t f0_unlock_2[] = { 0x96 };
  static const uint8_t madctl_initial[] = { 0x68 };
  static const uint8_t pixel_format[] = { 0x05 };
  static const uint8_t porch[] = { 0x01 };
  static const uint8_t gate_control[] = { 0xC6 };
  static const uint8_t display_function[] = { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 };
  static const uint8_t power_control_1[] = { 0x06 };
  static const uint8_t power_control_2[] = { 0xA7 };
  static const uint8_t vcom_control[] = { 0x18 };
  static const uint8_t positive_gamma[] = {
    0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54,
    0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B
  };
  static const uint8_t negative_gamma[] = {
    0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43,
    0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B
  };
  static const uint8_t madctl_horizontal[] = { ST7796_MADCTL_HORIZONTAL };
  static const uint8_t f0_lock_1[] = { 0xC3 };
  static const uint8_t f0_lock_2[] = { 0x69 };

  ST7796_Unselect();
  ST7796_Reset();
  ST7796_Select();

  /*
   * Unlock the vendor command page, configure color format/orientation/power/
   * gamma, then lock it again. 0x3A = pixel format; 0x05 means RGB565.
   */
  ST7796_WriteRegister(0xF0, f0_unlock_1, sizeof(f0_unlock_1));
  ST7796_WriteRegister(0xF0, f0_unlock_2, sizeof(f0_unlock_2));
  ST7796_WriteRegister(0x36, madctl_initial, sizeof(madctl_initial));
  ST7796_WriteRegister(0x3A, pixel_format, sizeof(pixel_format));
  ST7796_WriteRegister(0xB4, porch, sizeof(porch));
  ST7796_WriteRegister(0xB7, gate_control, sizeof(gate_control));
  ST7796_WriteRegister(0xE8, display_function, sizeof(display_function));
  ST7796_WriteRegister(0xC1, power_control_1, sizeof(power_control_1));
  ST7796_WriteRegister(0xC2, power_control_2, sizeof(power_control_2));
  ST7796_WriteRegister(0xC5, vcom_control, sizeof(vcom_control));
  ST7796_WriteRegister(0xE0, positive_gamma, sizeof(positive_gamma));
  ST7796_WriteRegister(0xE1, negative_gamma, sizeof(negative_gamma));

  ST7796_WriteRegister(0x36, madctl_horizontal, sizeof(madctl_horizontal));
  ST7796_WriteRegister(0xF0, f0_lock_1, sizeof(f0_lock_1));
  ST7796_WriteRegister(0xF0, f0_lock_2, sizeof(f0_lock_2));

  /* Sleep-out and display-on require delays per the controller timing. */
  ST7796_WriteCommand(0x11);
  ST7796_Unselect();
  HAL_Delay(120);

  ST7796_Select();
  ST7796_WriteCommand(0x29);
  ST7796_Unselect();
  HAL_Delay(20);

  ST7796_Fill(ST7796_BLACK);
}

void ST7796_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t data[4];

  /*
   * Clamp the requested area so a bad draw call cannot address outside the
   * panel. This keeps higher-level UI code from needing its own clipping.
   */
  if (x0 >= ST7796_WIDTH)
  {
    x0 = ST7796_WIDTH - 1U;
  }
  if (x1 >= ST7796_WIDTH)
  {
    x1 = ST7796_WIDTH - 1U;
  }
  if (y0 >= ST7796_HEIGHT)
  {
    y0 = ST7796_HEIGHT - 1U;
  }
  if (y1 >= ST7796_HEIGHT)
  {
    y1 = ST7796_HEIGHT - 1U;
  }

  /*
   * 0x2A is the column/X address range. The controller wants 16-bit values
   * sent big-endian: start high, start low, end high, end low.
   */
  data[0] = (uint8_t)(x0 >> 8);
  data[1] = (uint8_t)(x0 & 0xFFU);
  data[2] = (uint8_t)(x1 >> 8);
  data[3] = (uint8_t)(x1 & 0xFFU);
  ST7796_WriteRegister(0x2A, data, sizeof(data));

  /*
   * 0x2B is the row/Y address range. 0x2C is "memory write"; every RGB565 pixel
   * byte after 0x2C lands inside the selected rectangle, left-to-right/top-down.
   */
  data[0] = (uint8_t)(y0 >> 8);
  data[1] = (uint8_t)(y0 & 0xFFU);
  data[2] = (uint8_t)(y1 >> 8);
  data[3] = (uint8_t)(y1 & 0xFFU);
  ST7796_WriteRegister(0x2B, data, sizeof(data));

  ST7796_WriteCommand(0x2C);
}

void ST7796_Fill(uint16_t color)
{
  /* Full-screen fill is just a rectangle that covers the whole panel. */
  ST7796_FillRect(0U, 0U, ST7796_WIDTH, ST7796_HEIGHT, color);
}

void ST7796_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  uint32_t pixels;
  uint8_t line[ST7796_FILL_CHUNK_PIXELS * 2U];
  uint16_t i;

  /* Nothing to draw if the rectangle starts off-screen or has no area. */
  if ((x >= ST7796_WIDTH) || (y >= ST7796_HEIGHT) || (w == 0U) || (h == 0U))
  {
    return;
  }

  /* Clip width/height at the display edge. This makes UI code less fragile. */
  if ((uint32_t)x + w > ST7796_WIDTH)
  {
    w = (uint16_t)(ST7796_WIDTH - x);
  }
  if ((uint32_t)y + h > ST7796_HEIGHT)
  {
    h = (uint16_t)(ST7796_HEIGHT - y);
  }

  pixels = (uint32_t)w * h;

  /*
   * RGB565 is two bytes per pixel, high byte first.
   * Pre-filling a small buffer lets us send many same-color pixels per HAL call.
   * This is much faster than calling ST7796_DrawPixel() thousands of times.
   */
  for (i = 0U; i < ST7796_FILL_CHUNK_PIXELS; i++)
  {
    line[(uint16_t)(i * 2U)] = (uint8_t)(color >> 8);
    line[(uint16_t)(i * 2U + 1U)] = (uint8_t)(color & 0xFFU);
  }

  ST7796_Select();
  /*
   * One rectangle fill is one SPI transaction: set the address window once,
   * then stream exactly width*height pixels into that window.
   */
  ST7796_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
  ST7796_DataMode();

  while (pixels >= ST7796_FILL_CHUNK_PIXELS)
  {
    (void)HAL_SPI_Transmit(&hspi1, line, sizeof(line), ST7796_SPI_TIMEOUT);
    pixels -= ST7796_FILL_CHUNK_PIXELS;
  }

  if (pixels > 0U)
  {
    (void)HAL_SPI_Transmit(&hspi1, line, (uint16_t)(pixels * 2U), ST7796_SPI_TIMEOUT);
  }

  ST7796_Unselect();
}

void ST7796_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  uint8_t data[2];

  /*
   * Single-pixel writes are slow because they must set a 1x1 address window
   * for each pixel. They are still handy for diagnostics and tiny details.
   */
  if ((x >= ST7796_WIDTH) || (y >= ST7796_HEIGHT))
  {
    return;
  }

  data[0] = (uint8_t)(color >> 8);
  data[1] = (uint8_t)(color & 0xFFU);

  ST7796_Select();
  ST7796_SetWindow(x, y, x, y);
  ST7796_WriteData(data, sizeof(data));
  ST7796_Unselect();
}

static void ST7796_DrawPixelClipped(int16_t x, int16_t y, uint16_t color)
{
  /*
   * Shape algorithms work with signed coordinates because circles/lines can
   * temporarily calculate points just outside the screen. Clip before converting
   * to uint16_t so negative values do not wrap around.
   */
  if ((x < 0) || (y < 0) || (x >= (int16_t)ST7796_WIDTH) || (y >= (int16_t)ST7796_HEIGHT))
  {
    return;
  }

  ST7796_DrawPixel((uint16_t)x, (uint16_t)y, color);
}

static void ST7796_FillRectClipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  /*
   * Signed wrapper around FillRect. This lets round/circle helpers draw pieces
   * that partially hang off-screen without unsigned underflow.
   */
  if ((w <= 0) || (h <= 0))
  {
    return;
  }

  if (x < 0)
  {
    w = (int16_t)(w + x);
    x = 0;
  }
  if (y < 0)
  {
    h = (int16_t)(h + y);
    y = 0;
  }
  if ((x >= (int16_t)ST7796_WIDTH) || (y >= (int16_t)ST7796_HEIGHT) || (w <= 0) || (h <= 0))
  {
    return;
  }

  ST7796_FillRect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

void ST7796_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
  int16_t dx;
  int16_t sx;
  int16_t dy;
  int16_t sy;
  int16_t err;

  /*
   * Fast paths for the most common dashboard lines. Rect fills stream pixels
   * much more efficiently than one-pixel-at-a-time line drawing.
   */
  if (x0 == x1)
  {
    int16_t y = (y0 < y1) ? y0 : y1;
    int16_t h = (int16_t)(((y0 < y1) ? y1 : y0) - y + 1);
    ST7796_FillRectClipped(x0, y, 1, h, color);
    return;
  }
  if (y0 == y1)
  {
    int16_t x = (x0 < x1) ? x0 : x1;
    int16_t w = (int16_t)(((x0 < x1) ? x1 : x0) - x + 1);
    ST7796_FillRectClipped(x, y0, w, 1, color);
    return;
  }

  /*
   * Bresenham's line algorithm. It uses only integer addition/subtraction, so
   * it is ideal for small MCUs without needing floating-point slopes.
   */
  dx = (x0 < x1) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
  sx = (x0 < x1) ? 1 : -1;
  dy = (y0 < y1) ? (int16_t)(y0 - y1) : (int16_t)(y1 - y0);
  sy = (y0 < y1) ? 1 : -1;
  err = (int16_t)(dx + dy);

  while (1)
  {
    int16_t e2;

    ST7796_DrawPixelClipped(x0, y0, color);
    if ((x0 == x1) && (y0 == y1))
    {
      break;
    }

    e2 = (int16_t)(2 * err);
    if (e2 >= dy)
    {
      err = (int16_t)(err + dy);
      x0 = (int16_t)(x0 + sx);
    }
    if (e2 <= dx)
    {
      err = (int16_t)(err + dx);
      y0 = (int16_t)(y0 + sy);
    }
  }
}

void ST7796_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
  int16_t x;
  int16_t y;
  int16_t err;

  if (r < 0)
  {
    return;
  }

  /*
   * Midpoint circle algorithm. One point calculation is mirrored into eight
   * octants, which is why circles are still reasonably cheap without trig.
   */
  x = (int16_t)(-r);
  y = 0;
  err = (int16_t)(2 - (2 * r));

  do
  {
    ST7796_DrawPixelClipped((int16_t)(x0 - x), (int16_t)(y0 + y), color);
    ST7796_DrawPixelClipped((int16_t)(x0 - y), (int16_t)(y0 - x), color);
    ST7796_DrawPixelClipped((int16_t)(x0 + x), (int16_t)(y0 - y), color);
    ST7796_DrawPixelClipped((int16_t)(x0 + y), (int16_t)(y0 + x), color);

    r = err;
    if (r <= y)
    {
      y++;
      err = (int16_t)(err + ((2 * y) + 1));
    }
    if ((r > x) || (err > y))
    {
      x++;
      err = (int16_t)(err + ((2 * x) + 1));
    }
  } while (x < 0);
}

void ST7796_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
  int16_t x;
  int16_t y;
  int16_t err;

  if (r < 0)
  {
    return;
  }

  /*
   * Filled circle version of the midpoint algorithm. Horizontal spans are much
   * faster than individual pixels because each span becomes a rectangle fill.
   */
  x = (int16_t)(-r);
  y = 0;
  err = (int16_t)(2 - (2 * r));

  do
  {
    ST7796_FillRectClipped((int16_t)(x0 + x), (int16_t)(y0 - y), (int16_t)((-2 * x) + 1), 1, color);
    ST7796_FillRectClipped((int16_t)(x0 + x), (int16_t)(y0 + y), (int16_t)((-2 * x) + 1), 1, color);
    ST7796_FillRectClipped((int16_t)(x0 - y), (int16_t)(y0 + x), (int16_t)((2 * y) + 1), 1, color);
    ST7796_FillRectClipped((int16_t)(x0 - y), (int16_t)(y0 - x), (int16_t)((2 * y) + 1), 1, color);

    r = err;
    if (r <= y)
    {
      y++;
      err = (int16_t)(err + ((2 * y) + 1));
    }
    if ((r > x) || (err > y))
    {
      x++;
      err = (int16_t)(err + ((2 * x) + 1));
    }
  } while (x < 0);
}

void ST7796_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color)
{
  if ((w == 0U) || (h == 0U))
  {
    return;
  }

  if (r > (w / 2U))
  {
    r = (uint16_t)(w / 2U);
  }
  if (r > (h / 2U))
  {
    r = (uint16_t)(h / 2U);
  }

  if (r == 0U)
  {
    ST7796_FillRect(x, y, w, h, color);
    return;
  }

  /*
   * A rounded rectangle is three rectangles plus four circles. This overdraws
   * a few pixels, which is fine here and keeps the code simple.
   */
  ST7796_FillRect(x, (uint16_t)(y + r), w, (uint16_t)(h - (2U * r)), color);
  ST7796_FillRect((uint16_t)(x + r), y, (uint16_t)(w - (2U * r)), r, color);
  ST7796_FillRect((uint16_t)(x + r), (uint16_t)(y + h - r), (uint16_t)(w - (2U * r)), r, color);

  ST7796_FillCircle((int16_t)(x + r), (int16_t)(y + r), (int16_t)r, color);
  ST7796_FillCircle((int16_t)(x + w - r - 1U), (int16_t)(y + r), (int16_t)r, color);
  ST7796_FillCircle((int16_t)(x + r), (int16_t)(y + h - r - 1U), (int16_t)r, color);
  ST7796_FillCircle((int16_t)(x + w - r - 1U), (int16_t)(y + h - r - 1U), (int16_t)r, color);
}

void ST7796_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t thickness, uint16_t color)
{
  uint16_t i;

  if ((w == 0U) || (h == 0U) || (thickness == 0U))
  {
    return;
  }

  if (r > (w / 2U))
  {
    r = (uint16_t)(w / 2U);
  }
  if (r > (h / 2U))
  {
    r = (uint16_t)(h / 2U);
  }

  /*
   * Draw several one-pixel outlines inward. Because this function does not
   * know the panel background color, it only adds pixels; it does not "carve"
   * the middle back out like FillRoundRect would.
   */
  for (i = 0U; i < thickness; i++)
  {
    uint16_t xi = (uint16_t)(x + i);
    uint16_t yi = (uint16_t)(y + i);
    uint16_t wi = (w > (2U * i)) ? (uint16_t)(w - (2U * i)) : 0U;
    uint16_t hi = (h > (2U * i)) ? (uint16_t)(h - (2U * i)) : 0U;
    uint16_t ri = (r > i) ? (uint16_t)(r - i) : 0U;

    if ((wi == 0U) || (hi == 0U))
    {
      break;
    }

    if (ri == 0U)
    {
      ST7796_DrawLine((int16_t)xi, (int16_t)yi, (int16_t)(xi + wi - 1U), (int16_t)yi, color);
      ST7796_DrawLine((int16_t)xi, (int16_t)(yi + hi - 1U), (int16_t)(xi + wi - 1U), (int16_t)(yi + hi - 1U), color);
      ST7796_DrawLine((int16_t)xi, (int16_t)yi, (int16_t)xi, (int16_t)(yi + hi - 1U), color);
      ST7796_DrawLine((int16_t)(xi + wi - 1U), (int16_t)yi, (int16_t)(xi + wi - 1U), (int16_t)(yi + hi - 1U), color);
    }
    else
    {
      ST7796_DrawLine((int16_t)(xi + ri), (int16_t)yi, (int16_t)(xi + wi - ri - 1U), (int16_t)yi, color);
      ST7796_DrawLine((int16_t)(xi + ri), (int16_t)(yi + hi - 1U), (int16_t)(xi + wi - ri - 1U), (int16_t)(yi + hi - 1U), color);
      ST7796_DrawLine((int16_t)xi, (int16_t)(yi + ri), (int16_t)xi, (int16_t)(yi + hi - ri - 1U), color);
      ST7796_DrawLine((int16_t)(xi + wi - 1U), (int16_t)(yi + ri), (int16_t)(xi + wi - 1U), (int16_t)(yi + hi - ri - 1U), color);

      /*
       * Only draw the corner quadrants. A full circle at each corner creates
       * the loopy artifacts we saw around the screen border.
       */
      ST7796_DrawArc((int16_t)(xi + ri), (int16_t)(yi + ri), (int16_t)ri, 180, 270, 3U, color);
      ST7796_DrawArc((int16_t)(xi + wi - ri - 1U), (int16_t)(yi + ri), (int16_t)ri, 270, 360, 3U, color);
      ST7796_DrawArc((int16_t)(xi + wi - ri - 1U), (int16_t)(yi + hi - ri - 1U), (int16_t)ri, 0, 90, 3U, color);
      ST7796_DrawArc((int16_t)(xi + ri), (int16_t)(yi + hi - ri - 1U), (int16_t)ri, 90, 180, 3U, color);
    }
  }
}

void ST7796_DrawThickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, uint16_t color)
{
  int16_t dx;
  int16_t sx;
  int16_t dy;
  int16_t sy;
  int16_t err;
  int16_t radius;

  if (thickness <= 1U)
  {
    ST7796_DrawLine(x0, y0, x1, y1, color);
    return;
  }

  radius = (int16_t)(thickness / 2U);
  dx = (x0 < x1) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
  sx = (x0 < x1) ? 1 : -1;
  dy = (y0 < y1) ? (int16_t)(y0 - y1) : (int16_t)(y1 - y0);
  sy = (y0 < y1) ? 1 : -1;
  err = (int16_t)(dx + dy);

  /*
   * This stamps filled circles along a normal line path. It costs more SPI
   * writes than a thin Bresenham line, so use it for static bezels/needles or
   * redraw it only when the value changes.
   */
  while (1)
  {
    int16_t e2;

    ST7796_FillCircle(x0, y0, radius, color);
    if ((x0 == x1) && (y0 == y1))
    {
      break;
    }

    e2 = (int16_t)(2 * err);
    if (e2 >= dy)
    {
      err = (int16_t)(err + dy);
      x0 = (int16_t)(x0 + sx);
    }
    if (e2 <= dx)
    {
      err = (int16_t)(err + dx);
      y0 = (int16_t)(y0 + sy);
    }
  }
}

static int16_t ST7796_NormalizeDeg(int16_t angle_deg)
{
  while (angle_deg < 0)
  {
    angle_deg = (int16_t)(angle_deg + 360);
  }
  while (angle_deg >= 360)
  {
    angle_deg = (int16_t)(angle_deg - 360);
  }

  return angle_deg;
}

static int16_t ST7796_Sin1024(int16_t angle_deg)
{
  int16_t a;
  int32_t numerator;
  int32_t denominator;
  int16_t sign = 1;

  /*
   * Integer sine approximation scaled by 1024. It is accurate enough for UI
   * arcs and ticks, and avoids linking the floating-point math library.
   */
  angle_deg = ST7796_NormalizeDeg(angle_deg);
  if (angle_deg > 180)
  {
    angle_deg = (int16_t)(angle_deg - 180);
    sign = -1;
  }

  a = angle_deg;
  numerator = (int32_t)4 * a * (180 - a) * 1024;
  denominator = 40500 - ((int32_t)a * (180 - a));

  if (denominator == 0)
  {
    return 0;
  }

  return (int16_t)(sign * (numerator / denominator));
}

static int16_t ST7796_Cos1024(int16_t angle_deg)
{
  return ST7796_Sin1024((int16_t)(angle_deg + 90));
}

void ST7796_PointOnCircle(int16_t cx, int16_t cy, int16_t r, int16_t angle_deg, int16_t *x, int16_t *y)
{
  if ((x == 0) || (y == 0))
  {
    return;
  }

  *x = (int16_t)(cx + (((int32_t)r * ST7796_Cos1024(angle_deg)) / 1024));
  *y = (int16_t)(cy + (((int32_t)r * ST7796_Sin1024(angle_deg)) / 1024));
}

void ST7796_DrawArc(int16_t cx, int16_t cy, int16_t r, int16_t start_deg, int16_t end_deg, uint8_t step_deg, uint16_t color)
{
  int16_t span;
  int16_t a;
  int16_t px;
  int16_t py;

  if ((r <= 0) || (step_deg == 0U))
  {
    return;
  }

  start_deg = ST7796_NormalizeDeg(start_deg);
  end_deg = ST7796_NormalizeDeg(end_deg);
  if (end_deg < start_deg)
  {
    end_deg = (int16_t)(end_deg + 360);
  }

  span = (int16_t)(end_deg - start_deg);
  if (span == 0)
  {
    return;
  }

  ST7796_PointOnCircle(cx, cy, r, start_deg, &px, &py);

  for (a = (int16_t)(start_deg + step_deg); a < end_deg; a = (int16_t)(a + step_deg))
  {
    int16_t x;
    int16_t y;

    ST7796_PointOnCircle(cx, cy, r, a, &x, &y);
    ST7796_DrawLine(px, py, x, y, color);
    px = x;
    py = y;
  }

  {
    int16_t x;
    int16_t y;

    ST7796_PointOnCircle(cx, cy, r, end_deg, &x, &y);
    ST7796_DrawLine(px, py, x, y, color);
  }
}

void ST7796_DrawThickArc(int16_t cx, int16_t cy, int16_t r, int16_t start_deg, int16_t end_deg, uint8_t thickness, uint8_t step_deg, uint16_t color)
{
  int16_t a;
  int16_t dot_r;

  if ((r <= 0) || (thickness <= 1U))
  {
    ST7796_DrawArc(cx, cy, r, start_deg, end_deg, step_deg, color);
    return;
  }

  if (step_deg == 0U)
  {
    step_deg = 2U;
  }

  start_deg = ST7796_NormalizeDeg(start_deg);
  end_deg = ST7796_NormalizeDeg(end_deg);
  if (end_deg < start_deg)
  {
    end_deg = (int16_t)(end_deg + 360);
  }

  dot_r = (int16_t)(thickness / 2U);
  for (a = start_deg; a <= end_deg; a = (int16_t)(a + step_deg))
  {
    int16_t x;
    int16_t y;

    ST7796_PointOnCircle(cx, cy, r, a, &x, &y);
    ST7796_FillCircle(x, y, dot_r, color);
  }
}

void ST7796_DrawRadialTick(int16_t cx, int16_t cy, int16_t inner_r, int16_t outer_r, int16_t angle_deg, uint8_t thickness, uint16_t color)
{
  int16_t x0;
  int16_t y0;
  int16_t x1;
  int16_t y1;

  ST7796_PointOnCircle(cx, cy, inner_r, angle_deg, &x0, &y0);
  ST7796_PointOnCircle(cx, cy, outer_r, angle_deg, &x1, &y1);
  ST7796_DrawThickLine(x0, y0, x1, y1, thickness, color);
}

void ST7796_DrawGaugeTicks(int16_t cx, int16_t cy, int16_t inner_r, int16_t outer_r, int16_t start_deg, int16_t end_deg, uint8_t count, uint8_t major_every, uint16_t minor_color, uint16_t major_color)
{
  uint8_t i;
  int16_t span;

  if (count < 2U)
  {
    return;
  }

  start_deg = ST7796_NormalizeDeg(start_deg);
  end_deg = ST7796_NormalizeDeg(end_deg);
  if (end_deg < start_deg)
  {
    end_deg = (int16_t)(end_deg + 360);
  }
  span = (int16_t)(end_deg - start_deg);

  for (i = 0U; i < count; i++)
  {
    int16_t angle = (int16_t)(start_deg + (((int32_t)span * i) / (count - 1U)));
    uint8_t major = ((major_every != 0U) && ((i % major_every) == 0U)) ? 1U : 0U;
    int16_t tick_inner = major ? (int16_t)(inner_r - 8) : inner_r;

    ST7796_DrawRadialTick(cx,
                          cy,
                          tick_inner,
                          outer_r,
                          angle,
                          major ? 3U : 1U,
                          major ? major_color : minor_color);
  }
}

void ST7796_DrawNeedle(int16_t cx, int16_t cy, int16_t r, int16_t angle_deg, uint8_t thickness, uint16_t color)
{
  int16_t x;
  int16_t y;

  ST7796_PointOnCircle(cx, cy, r, angle_deg, &x, &y);
  ST7796_DrawThickLine(cx, cy, x, y, thickness, color);
  ST7796_FillCircle(cx, cy, (int16_t)(thickness + 2U), color);
}

void ST7796_DrawLedBar(uint16_t x, uint16_t y, uint8_t count, uint8_t lit_count, uint8_t radius, uint8_t gap, uint16_t off_color, uint16_t low_color, uint16_t mid_color, uint16_t high_color)
{
  uint8_t i;
  uint16_t step;

  step = (uint16_t)((2U * radius) + gap);
  for (i = 0U; i < count; i++)
  {
    uint16_t color = off_color;

    if (i < lit_count)
    {
      if (i < (count / 2U))
      {
        color = low_color;
      }
      else if (i < ((count * 3U) / 4U))
      {
        color = mid_color;
      }
      else
      {
        color = high_color;
      }
    }

    ST7796_FillCircle((int16_t)(x + radius + (i * step)), (int16_t)(y + radius), (int16_t)radius, color);
  }
}

void ST7796_FillHorizontalBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t percent, uint16_t fill_color, uint16_t bg_color, uint16_t border_color)
{
  uint16_t fill_w;

  if (percent > 100U)
  {
    percent = 100U;
  }

  ST7796_FillRoundRect(x, y, w, h, 3U, border_color);
  if ((w <= 4U) || (h <= 4U))
  {
    return;
  }

  ST7796_FillRoundRect((uint16_t)(x + 2U), (uint16_t)(y + 2U), (uint16_t)(w - 4U), (uint16_t)(h - 4U), 2U, bg_color);
  fill_w = (uint16_t)(((uint32_t)(w - 4U) * percent) / 100U);
  if (fill_w > 0U)
  {
    ST7796_FillRoundRect((uint16_t)(x + 2U), (uint16_t)(y + 2U), fill_w, (uint16_t)(h - 4U), 2U, fill_color);
  }
}

static uint8_t ST7796_ToFontUpper(char c)
{
  if ((c >= 'a') && (c <= 'z'))
  {
    c = (char)(c - ('a' - 'A'));
  }

  return (uint8_t)c;
}

static uint8_t ST7796_Font5x7Row(char c, uint8_t row)
{
  /*
   * Each returned byte uses only the low 5 bits. Bit 4 is the left column and
   * bit 0 is the right column. Example: 0x1F = 11111 fills the whole row.
   *
   * This switch-based font avoids a large 96-character ASCII table while still
   * supporting the labels we usually need on an embedded dashboard.
   */
  static const uint8_t blank[7] = { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U };
  static const uint8_t unknown[7] = { 0x1FU, 0x01U, 0x02U, 0x04U, 0x04U, 0x00U, 0x04U };
  static const uint8_t n0[7] = { 0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU };
  static const uint8_t n1[7] = { 0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU };
  static const uint8_t n2[7] = { 0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU };
  static const uint8_t n3[7] = { 0x1EU, 0x01U, 0x01U, 0x0EU, 0x01U, 0x01U, 0x1EU };
  static const uint8_t n4[7] = { 0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U };
  static const uint8_t n5[7] = { 0x1FU, 0x10U, 0x10U, 0x1EU, 0x01U, 0x01U, 0x1EU };
  static const uint8_t n6[7] = { 0x0EU, 0x10U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU };
  static const uint8_t n7[7] = { 0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U };
  static const uint8_t n8[7] = { 0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU };
  static const uint8_t n9[7] = { 0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x01U, 0x0EU };
  static const uint8_t A[7] = { 0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U };
  static const uint8_t B[7] = { 0x1EU, 0x11U, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU };
  static const uint8_t C[7] = { 0x0EU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0EU };
  static const uint8_t D[7] = { 0x1EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1EU };
  static const uint8_t E[7] = { 0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x1FU };
  static const uint8_t F[7] = { 0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U };
  static const uint8_t G[7] = { 0x0EU, 0x11U, 0x10U, 0x17U, 0x11U, 0x11U, 0x0FU };
  static const uint8_t H[7] = { 0x11U, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U };
  static const uint8_t I[7] = { 0x0EU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU };
  static const uint8_t J[7] = { 0x01U, 0x01U, 0x01U, 0x01U, 0x11U, 0x11U, 0x0EU };
  static const uint8_t K[7] = { 0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U };
  static const uint8_t L[7] = { 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1FU };
  static const uint8_t M[7] = { 0x11U, 0x1BU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U };
  static const uint8_t N[7] = { 0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U, 0x11U };
  static const uint8_t O[7] = { 0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU };
  static const uint8_t P[7] = { 0x1EU, 0x11U, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U };
  static const uint8_t Q[7] = { 0x0EU, 0x11U, 0x11U, 0x11U, 0x15U, 0x12U, 0x0DU };
  static const uint8_t R[7] = { 0x1EU, 0x11U, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U };
  static const uint8_t S[7] = { 0x0FU, 0x10U, 0x10U, 0x0EU, 0x01U, 0x01U, 0x1EU };
  static const uint8_t T[7] = { 0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U };
  static const uint8_t U[7] = { 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU };
  static const uint8_t V[7] = { 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U };
  static const uint8_t W[7] = { 0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x15U, 0x0AU };
  static const uint8_t X[7] = { 0x11U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U, 0x11U };
  static const uint8_t Y[7] = { 0x11U, 0x11U, 0x0AU, 0x04U, 0x04U, 0x04U, 0x04U };
  static const uint8_t Z[7] = { 0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x1FU };
  static const uint8_t dash[7] = { 0x00U, 0x00U, 0x00U, 0x1FU, 0x00U, 0x00U, 0x00U };
  static const uint8_t dot[7] = { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x0CU };
  static const uint8_t colon[7] = { 0x00U, 0x0CU, 0x0CU, 0x00U, 0x0CU, 0x0CU, 0x00U };
  static const uint8_t slash[7] = { 0x01U, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x10U };
  static const uint8_t percent[7] = { 0x18U, 0x19U, 0x02U, 0x04U, 0x08U, 0x13U, 0x03U };
  const uint8_t *glyph = unknown;

  if (row >= ST7796_FONT5X7_H)
  {
    return 0U;
  }

  c = (char)ST7796_ToFontUpper(c);

  switch (c)
  {
    case ' ': glyph = blank; break;
    case '0': glyph = n0; break;
    case '1': glyph = n1; break;
    case '2': glyph = n2; break;
    case '3': glyph = n3; break;
    case '4': glyph = n4; break;
    case '5': glyph = n5; break;
    case '6': glyph = n6; break;
    case '7': glyph = n7; break;
    case '8': glyph = n8; break;
    case '9': glyph = n9; break;
    case 'A': glyph = A; break;
    case 'B': glyph = B; break;
    case 'C': glyph = C; break;
    case 'D': glyph = D; break;
    case 'E': glyph = E; break;
    case 'F': glyph = F; break;
    case 'G': glyph = G; break;
    case 'H': glyph = H; break;
    case 'I': glyph = I; break;
    case 'J': glyph = J; break;
    case 'K': glyph = K; break;
    case 'L': glyph = L; break;
    case 'M': glyph = M; break;
    case 'N': glyph = N; break;
    case 'O': glyph = O; break;
    case 'P': glyph = P; break;
    case 'Q': glyph = Q; break;
    case 'R': glyph = R; break;
    case 'S': glyph = S; break;
    case 'T': glyph = T; break;
    case 'U': glyph = U; break;
    case 'V': glyph = V; break;
    case 'W': glyph = W; break;
    case 'X': glyph = X; break;
    case 'Y': glyph = Y; break;
    case 'Z': glyph = Z; break;
    case '-': glyph = dash; break;
    case '.': glyph = dot; break;
    case ':': glyph = colon; break;
    case '/': glyph = slash; break;
    case '%': glyph = percent; break;
    default: break;
  }

  return glyph[row];
}

void ST7796_DrawChar5x7(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color, uint8_t scale, uint8_t draw_bg)
{
  uint8_t row;
  uint8_t col;
  uint16_t cell;

  if (scale == 0U)
  {
    scale = 1U;
  }

  cell = scale;

  if (draw_bg != 0U)
  {
    ST7796_FillRect(x,
                    y,
                    (uint16_t)((ST7796_FONT5X7_W + ST7796_FONT5X7_SPACING) * scale),
                    (uint16_t)(ST7796_FONT5X7_H * scale),
                    bg_color);
  }

  for (row = 0U; row < ST7796_FONT5X7_H; row++)
  {
    uint8_t bits = ST7796_Font5x7Row(c, row);

    for (col = 0U; col < ST7796_FONT5X7_W; col++)
    {
      if ((bits & (uint8_t)(1U << (4U - col))) != 0U)
      {
        ST7796_FillRect((uint16_t)(x + (col * cell)),
                        (uint16_t)(y + (row * cell)),
                        cell,
                        cell,
                        color);
      }
    }
  }
}

void ST7796_DrawString5x7(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg_color, uint8_t scale, uint8_t draw_bg)
{
  uint16_t cursor_x = x;

  if (text == 0)
  {
    return;
  }

  if (scale == 0U)
  {
    scale = 1U;
  }

  while (*text != '\0')
  {
    ST7796_DrawChar5x7(cursor_x, y, *text, color, bg_color, scale, draw_bg);
    cursor_x = (uint16_t)(cursor_x + ((ST7796_FONT5X7_W + ST7796_FONT5X7_SPACING) * scale));
    text++;
  }
}

uint16_t ST7796_TextWidth5x7(const char *text, uint8_t scale)
{
  uint16_t chars = 0U;

  if (text == 0)
  {
    return 0U;
  }

  if (scale == 0U)
  {
    scale = 1U;
  }

  while (*text != '\0')
  {
    chars++;
    text++;
  }

  return (uint16_t)(chars * (ST7796_FONT5X7_W + ST7796_FONT5X7_SPACING) * scale);
}
