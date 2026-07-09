#include "st7796.h"

extern SPI_HandleTypeDef hspi1;

#define ST7796_SPI_TIMEOUT 100U
#define ST7796_MADCTL_HORIZONTAL 0x28u
#define ST7796_FILL_CHUNK_PIXELS 128U

/* CS is the display's chip-select. Keep it low while one logical TFT transaction is active. */
static void ST7796_Select(void)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
}

/* Releasing CS tells the display this command/data burst is finished. */
static void ST7796_Unselect(void)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
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
  ST7796_CommandMode();
  (void)HAL_SPI_Transmit(&hspi1, &command, 1U, ST7796_SPI_TIMEOUT);
}

/* Write a raw data buffer. HAL wants a non-const pointer, so the cast is local here. */
static void ST7796_WriteData(const uint8_t *data, uint16_t size)
{
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
  /* These values are ported from the LCDWiki ST7796S hardware-SPI init flow. */
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

  /* Unlock the vendor command page, configure color format/orientation/power/gamma, then lock it. */
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

  /* Clamp the requested area so a bad draw call cannot address outside the panel. */
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

  /* 0x2A is the column/X address range, sent big-endian: start high/low, end high/low. */
  data[0] = (uint8_t)(x0 >> 8);
  data[1] = (uint8_t)(x0 & 0xFFU);
  data[2] = (uint8_t)(x1 >> 8);
  data[3] = (uint8_t)(x1 & 0xFFU);
  ST7796_WriteRegister(0x2A, data, sizeof(data));

  /* 0x2B is the row/Y address range. After this, 0x2C starts the pixel stream. */
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
   */
  for (i = 0U; i < ST7796_FILL_CHUNK_PIXELS; i++)
  {
    line[(uint16_t)(i * 2U)] = (uint8_t)(color >> 8);
    line[(uint16_t)(i * 2U + 1U)] = (uint8_t)(color & 0xFFU);
  }

  ST7796_Select();
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

  /* Single-pixel writes are slow but handy for debugging and small details. */
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
