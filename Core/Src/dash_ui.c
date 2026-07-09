#include "dash_ui.h"
#include "st7796.h"

/* Screen colors are RGB565. Named UI colors keep layout code readable. */
#define DASH_BG       ST7796_BLACK
#define DASH_DIM      0x3186U
#define DASH_DARK     0x1082U
#define DASH_ORANGE   0xFD20U
#define DASH_RED      ST7796_RED
#define DASH_GREEN    ST7796_GREEN
#define DASH_WHITE    ST7796_WHITE

#define DIGIT_W       70U
#define DIGIT_H       120U
#define DIGIT_T       14U
#define DIGIT_GAP     10U
#define DIGIT_Y       58U
#define DIGIT_X0      112U
#define MPH_MAX       80U

/* 0xFFFF means "force the first update" because no real MPH value can equal it. */
static uint16_t last_mph = 0xFFFFU;
static uint8_t last_digit_slots[3] = { 0xFFU, 0xFFU, 0xFFU };
static uint8_t last_gps_rx = 0xFFU;
static uint8_t last_gps_line = 0xFFU;
static uint8_t last_gps_fix = 0xFFU;

/*
 * Seven-segment bit map for digits.
 * Bit order is: top, upper-right, lower-right, bottom, lower-left, upper-left, middle.
 */
static const uint8_t digit_segments[10] = {
  0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
  0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU
};

/* Draw one segment of one big digit as a filled rectangle. */
static void DrawSegment(uint16_t x, uint16_t y, uint8_t segment, uint16_t color)
{
  switch (segment)
  {
    case 0U:
      ST7796_FillRect(x + DIGIT_T, y, DIGIT_W - (2U * DIGIT_T), DIGIT_T, color);
      break;
    case 1U:
      ST7796_FillRect(x + DIGIT_W - DIGIT_T, y + DIGIT_T, DIGIT_T, (DIGIT_H / 2U) - DIGIT_T, color);
      break;
    case 2U:
      ST7796_FillRect(x + DIGIT_W - DIGIT_T, y + (DIGIT_H / 2U), DIGIT_T, (DIGIT_H / 2U) - DIGIT_T, color);
      break;
    case 3U:
      ST7796_FillRect(x + DIGIT_T, y + DIGIT_H - DIGIT_T, DIGIT_W - (2U * DIGIT_T), DIGIT_T, color);
      break;
    case 4U:
      ST7796_FillRect(x, y + (DIGIT_H / 2U), DIGIT_T, (DIGIT_H / 2U) - DIGIT_T, color);
      break;
    case 5U:
      ST7796_FillRect(x, y + DIGIT_T, DIGIT_T, (DIGIT_H / 2U) - DIGIT_T, color);
      break;
    case 6U:
      ST7796_FillRect(x + DIGIT_T, y + (DIGIT_H / 2U) - (DIGIT_T / 2U), DIGIT_W - (2U * DIGIT_T), DIGIT_T, color);
      break;
    default:
      break;
  }
}

/*
 * Draw a full 7-segment digit.
 * Inactive segments are drawn dark instead of erased, which gives a gauge/instrument look.
 */
static void DrawDigit(uint16_t x, uint16_t y, uint8_t digit, uint16_t color)
{
  uint8_t bits;
  uint8_t segment;

  ST7796_FillRect(x, y, DIGIT_W, DIGIT_H, DASH_BG);

  if (digit > 9U)
  {
    return;
  }

  bits = digit_segments[digit];
  for (segment = 0U; segment < 7U; segment++)
  {
    DrawSegment(x, y, segment, ((bits & (uint8_t)(1U << segment)) != 0U) ? color : DASH_DARK);
  }
}

/* The MPH label is block letters built from rectangles, so we do not need a font library yet. */
static void DrawLetterM(uint16_t x, uint16_t y, uint16_t color)
{
  ST7796_FillRect(x, y, 8U, 48U, color);
  ST7796_FillRect(x + 34U, y, 8U, 48U, color);
  ST7796_FillRect(x + 8U, y + 8U, 8U, 16U, color);
  ST7796_FillRect(x + 18U, y + 18U, 8U, 16U, color);
  ST7796_FillRect(x + 26U, y + 8U, 8U, 16U, color);
}

static void DrawLetterP(uint16_t x, uint16_t y, uint16_t color)
{
  ST7796_FillRect(x, y, 8U, 48U, color);
  ST7796_FillRect(x, y, 34U, 8U, color);
  ST7796_FillRect(x, y + 20U, 34U, 8U, color);
  ST7796_FillRect(x + 28U, y + 8U, 8U, 16U, color);
}

static void DrawLetterH(uint16_t x, uint16_t y, uint16_t color)
{
  ST7796_FillRect(x, y, 8U, 48U, color);
  ST7796_FillRect(x + 30U, y, 8U, 48U, color);
  ST7796_FillRect(x, y + 20U, 38U, 8U, color);
}

static void DrawMphLabel(void)
{
  DrawLetterM(176U, 190U, DASH_WHITE);
  DrawLetterP(228U, 190U, DASH_WHITE);
  DrawLetterH(274U, 190U, DASH_WHITE);
}

/* Simple 0-80 scale across the bottom. Later this can become a proper arc/needle. */
static void DrawScaleMarks(void)
{
  uint16_t i;

  ST7796_FillRect(40U, 268U, 400U, 8U, DASH_DIM);
  for (i = 0U; i <= 8U; i++)
  {
    uint16_t x = (uint16_t)(40U + (i * 50U));
    ST7796_FillRect(x, 258U, 4U, 26U, DASH_WHITE);
  }
}

static void DrawStatusBox(uint16_t x, uint8_t active, uint16_t active_color)
{
  ST7796_FillRect(x, 34U, 26U, 14U, active ? active_color : DASH_DARK);
}

/* The colored bar is a quick visual speed cue while the large digits stay primary. */
static void DrawSpeedBar(uint16_t mph)
{
  uint16_t fill_w;
  uint16_t color;

  if (mph > MPH_MAX)
  {
    mph = MPH_MAX;
  }

  fill_w = (uint16_t)(((uint32_t)mph * 400U) / MPH_MAX);

  if (mph < 35U)
  {
    color = DASH_GREEN;
  }
  else if (mph < 60U)
  {
    color = DASH_ORANGE;
  }
  else
  {
    color = DASH_RED;
  }

  /* Clear the old bar first. The digits are cached separately, but the bar is cheap to repaint. */
  ST7796_FillRect(40U, 288U, 400U, 18U, DASH_DARK);
  if (fill_w > 0U)
  {
    ST7796_FillRect(40U, 288U, fill_w, 18U, color);
  }
}

/* Convert a speed value into three display slots. 0xFF means "blank this digit". */
static void MakeDigitSlots(uint16_t mph, uint8_t slots[3])
{
  uint8_t hundreds = (uint8_t)((mph / 100U) % 10U);
  uint8_t tens = (uint8_t)((mph / 10U) % 10U);
  uint8_t ones = (uint8_t)(mph % 10U);

  slots[0] = (hundreds == 0U) ? 0xFFU : hundreds;
  slots[1] = ((hundreds == 0U) && (tens == 0U)) ? 0xFFU : tens;
  slots[2] = ones;
}

/* Draw or blank one of the three digit positions. */
static void DrawDigitSlot(uint8_t slot_index, uint8_t value)
{
  uint16_t x = (uint16_t)(DIGIT_X0 + (slot_index * (DIGIT_W + DIGIT_GAP)));

  if (value == 0xFFU)
  {
    ST7796_FillRect(x, DIGIT_Y, DIGIT_W, DIGIT_H, DASH_BG);
  }
  else
  {
    DrawDigit(x, DIGIT_Y, value, DASH_WHITE);
  }
}

/*
 * Only redraw the digit slots that changed.
 * This is much smoother than repainting all three digits for every MPH update.
 */
static void DrawSpeedDigits(uint16_t mph)
{
  uint8_t slots[3];
  uint8_t i;

  MakeDigitSlots(mph, slots);

  for (i = 0U; i < 3U; i++)
  {
    if (slots[i] != last_digit_slots[i])
    {
      DrawDigitSlot(i, slots[i]);
      last_digit_slots[i] = slots[i];
    }
  }
}

void DashUI_Init(void)
{
  /* Force a clean first speed redraw after the static screen is painted. */
  last_mph = 0xFFFFU;
  last_digit_slots[0] = 0xFFU;
  last_digit_slots[1] = 0xFFU;
  last_digit_slots[2] = 0xFFU;
  last_gps_rx = 0xFFU;
  last_gps_line = 0xFFU;
  last_gps_fix = 0xFFU;

  /* Static dashboard background: border, separator lines, label, and scale marks. */
  ST7796_Fill(DASH_BG);
  ST7796_FillRect(0U, 0U, ST7796_WIDTH, 6U, DASH_WHITE);
  ST7796_FillRect(0U, ST7796_HEIGHT - 6U, ST7796_WIDTH, 6U, DASH_WHITE);
  ST7796_FillRect(0U, 0U, 6U, ST7796_HEIGHT, DASH_WHITE);
  ST7796_FillRect(ST7796_WIDTH - 6U, 0U, 6U, ST7796_HEIGHT, DASH_WHITE);
  ST7796_FillRect(32U, 24U, 416U, 4U, DASH_DIM);
  ST7796_FillRect(32U, 238U, 416U, 4U, DASH_DIM);

  DrawMphLabel();
  DrawScaleMarks();
  DashUI_UpdateGpsStatus(0U, 0U, 0U);
}

void DashUI_UpdateSpeed(uint16_t mph)
{
  /* Keep wild test/GPS values from drawing outside the three-digit area. */
  if (mph > 199U)
  {
    mph = 199U;
  }

  /* If nothing changed, skip all SPI drawing work. */
  if (mph == last_mph)
  {
    return;
  }

  DrawSpeedDigits(mph);
  DrawSpeedBar(mph);
  last_mph = mph;
}

void DashUI_UpdateGpsStatus(uint8_t has_rx, uint8_t has_line, uint8_t has_fix)
{
  has_rx = (has_rx != 0U) ? 1U : 0U;
  has_line = (has_line != 0U) ? 1U : 0U;
  has_fix = (has_fix != 0U) ? 1U : 0U;

  if (has_rx != last_gps_rx)
  {
    DrawStatusBox(40U, has_rx, DASH_ORANGE);
    last_gps_rx = has_rx;
  }

  if (has_line != last_gps_line)
  {
    DrawStatusBox(74U, has_line, DASH_ORANGE);
    last_gps_line = has_line;
  }

  if (has_fix != last_gps_fix)
  {
    DrawStatusBox(108U, has_fix, DASH_GREEN);
    last_gps_fix = has_fix;
  }
}
