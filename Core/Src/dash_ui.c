#include "dash_ui.h"
#include "st7796.h"

/* Screen colors are RGB565. Named UI colors keep layout code readable. */
#define DASH_BG       ST7796_BLACK
#define DASH_DIM      0x3186U
#define DASH_DARK     0x1082U
#define DASH_ARC_OFF  0x2945U
#define DASH_BEZEL    0x4A49U
#define DASH_GAUGE_BORDER 0xBDF7U
#define DASH_PANEL    0x18E3U
#define DASH_BLUE     0x04BFU
#define DASH_ORANGE   0xFD20U
#define DASH_RED      ST7796_RED
#define DASH_GREEN    ST7796_GREEN
#define DASH_WHITE    ST7796_WHITE

/*
 * The speed number is drawn from a tiny custom bitmap font. The "font" is
 * only 10 digits, so it costs very little flash/RAM compared with a full font
 * library. Each lit bitmap cell becomes one filled rectangle on the TFT.
 */
#define DIGIT_W       68U
#define DIGIT_H       120U
#define DIGIT_GAP     10U
#define DIGIT_CELL_W  12U
#define DIGIT_CELL_H  15U
#define DIGIT_CELL_G  2U
#define DIGIT_Y       92U
#define DIGIT_AREA_X  104U
#define DIGIT_AREA_W  272U
#define DIGIT_CENTER_X 240U
#define MPH_MAX       80U

/*
 * The swept gauge maps 0-80 MPH onto a 250 degree arc. The old dotted arc used
 * one dot every 5 MPH; the live sweep below is cleaner and less busy.
 */
#define ARC_STEP_MPH  5U

/* 0xFFFF means "force the first update" because no real MPH value can equal it. */
static uint16_t last_mph = 0xFFFFU;
/*
 * These cached status values avoid repainting small rectangles when nothing
 * changed. SPI display writes are much slower than normal CPU math, so caching
 * makes the dashboard calmer and faster.
 */
static uint8_t last_gps_rx = 0xFFU;
static uint8_t last_gps_line = 0xFFU;
static uint8_t last_gps_fix = 0xFFU;
static uint8_t last_sweep_percent = 0xFFU;
static uint8_t last_digit_count = 0U;
static uint8_t last_digit_values[3] = { 0U, 0U, 0U };
static uint16_t last_digit_x[3] = { 0U, 0U, 0U };

/*
 * Heavy 5x7 block digit font. Each byte uses the low five bits as columns.
 * It is more condensed and dashboard-like than seven-segment numerals.
 *
 * Example row: 0x1F is binary 11111, so all five columns are filled.
 * A row like 0x11 is binary 10001, so only the left and right columns fill.
 */
static const uint8_t digit_rows[10][7] = {
  { 0x1FU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x1FU },
  { 0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU },
  { 0x1EU, 0x01U, 0x01U, 0x1EU, 0x10U, 0x10U, 0x1FU },
  { 0x1EU, 0x01U, 0x01U, 0x0EU, 0x01U, 0x01U, 0x1EU },
  { 0x12U, 0x12U, 0x12U, 0x1FU, 0x02U, 0x02U, 0x02U },
  { 0x1FU, 0x10U, 0x10U, 0x1EU, 0x01U, 0x01U, 0x1EU },
  { 0x0FU, 0x10U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x1FU },
  { 0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U },
  { 0x1FU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x1FU },
  { 0x1FU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x01U, 0x1EU }
};

/*
 * Draw one big digit from a compact bitmap. Filled cells make an Impact-ish,
 * heavy sans-serif number without needing a font library.
 */
static void DrawDigit(uint16_t x, uint16_t y, uint8_t digit, uint16_t color)
{
  uint8_t row;
  uint8_t col;

  if (digit > 9U)
  {
    return;
  }

  for (row = 0U; row < 7U; row++)
  {
    for (col = 0U; col < 5U; col++)
    {
      /*
       * Test one bit from the row pattern. Column 0 reads bit 4, column 4 reads
       * bit 0, so the bits map left-to-right on the display.
       */
      if ((digit_rows[digit][row] & (uint8_t)(1U << (4U - col))) != 0U)
      {
        /*
         * Rounded cells cost more SPI work than plain rectangles, but they make
         * the custom digit font feel less blocky while keeping the font tiny.
         */
        ST7796_FillRoundRect((uint16_t)(x + (col * (DIGIT_CELL_W + DIGIT_CELL_G))),
                             (uint16_t)(y + (row * (DIGIT_CELL_H + DIGIT_CELL_G))),
                             DIGIT_CELL_W,
                             DIGIT_CELL_H,
                             3U,
                             color);
      }
    }
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
  DrawLetterM(176U, 220U, DASH_WHITE);
  DrawLetterP(228U, 220U, DASH_WHITE);
  DrawLetterH(274U, 220U, DASH_WHITE);
}

static void DrawGaugeRing(void)
{
  /*
   * Keep the static gauge furniture quiet: one outer bezel and one subtle inner
   * line. The live speed sweep is what should catch the eye.
   *
   * DASH_GAUGE_BORDER is a light grey RGB565 value. Because this border is
   * static, it costs time only during startup, not during sensor acquisition.
   */
  ST7796_DrawThickArc(240, 163, 151, 145, 395, 3U, 2U, DASH_PANEL);
  ST7796_DrawThickArc(240, 163, 146, 145, 395, 2U, 2U, DASH_GAUGE_BORDER);
  ST7796_DrawArc(240, 163, 119, 155, 385, 4U, DASH_DIM);
}

static uint16_t GaugeColorForMph(uint16_t mph)
{
  /*
   * RGB565 colors are 16-bit values: 5 red bits, 6 green bits, 5 blue bits.
   * The named constants hide that bit packing so the UI logic stays readable.
   */
  if (mph < 35U)
  {
    return DASH_GREEN;
  }
  if (mph < 60U)
  {
    return DASH_ORANGE;
  }

  return DASH_RED;
}

static void DrawGaugeScale(void)
{
  /*
   * Draw the unlit sweep once. DrawSpeedSweep() later paints live color over the
   * same path. Angle 145 is lower-left; 395 wraps around through the top and
   * ends at lower-right.
   */
  ST7796_DrawThickArc(240, 163, 136, 145, 395, 7U, 2U, DASH_ARC_OFF);

  /* Five quiet reference ticks: 0, 20, 40, 60, and 80 MPH. */
  ST7796_DrawGaugeTicks(240, 163, 126, 141, 145, 395, 5U, 1U, DASH_DIM, DASH_BEZEL);

  /*
   * Tiny max-speed label at the end of the arc. The gauge currently maps
   * 0..MPH_MAX onto the sweep, so this number marks the right-end limit.
   */
  ST7796_DrawString5x7(372U, 224U, "80", DASH_GAUGE_BORDER, DASH_BG, 2U, 0U);
}

static void DrawSpeedSweep(uint16_t mph)
{
  int16_t active_end;

  if (mph > MPH_MAX)
  {
    mph = MPH_MAX;
  }

  /*
   * Redraw the full sweep in the off color, then redraw the active part. This
   * handles both acceleration and deceleration without needing a frame buffer.
   */
  ST7796_DrawThickArc(240, 163, 136, 145, 395, 9U, 2U, DASH_ARC_OFF);
  active_end = (int16_t)(145 + (((uint32_t)250U * mph) / MPH_MAX));
  if (active_end <= 145)
  {
    active_end = 148;
  }
  ST7796_DrawThickArc(240, 163, 136, 145, active_end, 9U, 2U, GaugeColorForMph(mph));
}

static void DrawSpeedArc(uint16_t mph, uint8_t force_redraw)
{
  uint8_t sweep_percent;

  if (mph > MPH_MAX)
  {
    mph = MPH_MAX;
  }

  sweep_percent = (uint8_t)(((uint32_t)mph * 100U) / MPH_MAX);
  if ((force_redraw == 0U) && (sweep_percent == last_sweep_percent))
  {
    return;
  }

  DrawSpeedSweep(mph);
  last_sweep_percent = sweep_percent;
}

static void DrawStatusBox(uint16_t x, uint8_t active, uint16_t active_color)
{
  /*
   * Rounded diagnostic pills: RX byte, complete NMEA line, valid fix.
   * The white backing is slightly larger, acting like a border.
   */
  ST7796_FillRoundRect(x, 282U, 42U, 18U, 8U, DASH_WHITE);
  ST7796_FillRoundRect((uint16_t)(x + 3U), 285U, 36U, 12U, 6U, active ? active_color : DASH_DARK);
}

static uint8_t MakeDigitList(uint16_t mph, uint8_t digits[3])
{
  /*
   * Convert the integer speed into only the digits we need. This lets the draw
   * code center "0", "12", and "105" instead of using fixed blank-leading slots.
   */
  if (mph >= 100U)
  {
    digits[0] = (uint8_t)((mph / 100U) % 10U);
    digits[1] = (uint8_t)((mph / 10U) % 10U);
    digits[2] = (uint8_t)(mph % 10U);
    return 3U;
  }

  if (mph >= 10U)
  {
    digits[0] = (uint8_t)((mph / 10U) % 10U);
    digits[1] = (uint8_t)(mph % 10U);
    return 2U;
  }

  digits[0] = (uint8_t)(mph % 10U);
  return 1U;
}

static void ErasePreviousSpeedDigits(void)
{
  uint8_t i;

  /*
   * Do not clear one giant digit rectangle: that was visibly chopping the
   * gauge. Erasing only the lit cells from the previous number is much less
   * SPI work and avoids the big black wipe during 0/1/2 MPH bench changes.
   */
  for (i = 0U; i < last_digit_count; i++)
  {
    DrawDigit(last_digit_x[i], DIGIT_Y, last_digit_values[i], DASH_BG);
  }

  last_digit_count = 0U;
}

/*
 * Redraw the complete digit band so one-, two-, and three-digit speeds can stay
 * visually centered instead of living in fixed right-aligned slots.
 */
static void DrawSpeedDigits(uint16_t mph)
{
  uint8_t digits[3];
  uint8_t digit_count;
  uint8_t i;
  uint16_t number_w;
  uint16_t x;

  digit_count = MakeDigitList(mph, digits);
  /*
   * Calculate the exact pixel width of the number, then start half that width
   * left of the center line. This is why one-, two-, and three-digit speeds all
   * stay centered in the gauge.
   */
  number_w = (uint16_t)((digit_count * DIGIT_W) + ((digit_count - 1U) * DIGIT_GAP));
  x = (uint16_t)(DIGIT_CENTER_X - (number_w / 2U));

  for (i = 0U; i < digit_count; i++)
  {
    uint16_t digit_x = (uint16_t)(x + (i * (DIGIT_W + DIGIT_GAP)));

    DrawDigit(digit_x, DIGIT_Y, digits[i], DASH_WHITE);
    last_digit_values[i] = digits[i];
    last_digit_x[i] = digit_x;
  }

  last_digit_count = digit_count;
}

void DashUI_Init(void)
{
  /* Force a clean first speed redraw after the static screen is painted. */
  last_mph = 0xFFFFU;
  last_gps_rx = 0xFFU;
  last_gps_line = 0xFFU;
  last_gps_fix = 0xFFU;
  last_sweep_percent = 0xFFU;
  last_digit_count = 0U;

  /*
   * Static dashboard background: border, circular gauge arc, label, and
   * diagnostics. CubeMX will not edit this file, so layout changes belong here.
   */
  ST7796_Fill(DASH_BG);
  ST7796_FillRect(0U, 0U, ST7796_WIDTH, 6U, DASH_WHITE);
  ST7796_FillRect(0U, ST7796_HEIGHT - 6U, ST7796_WIDTH, 6U, DASH_WHITE);
  ST7796_FillRect(0U, 0U, 6U, ST7796_HEIGHT, DASH_WHITE);
  ST7796_FillRect(ST7796_WIDTH - 6U, 0U, 6U, ST7796_HEIGHT, DASH_WHITE);

  DrawGaugeRing();
  DrawGaugeScale();
  DrawMphLabel();
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

  /*
   * Draw order matters on a no-framebuffer TFT. Keep the static gauge artwork
   * out of the live update path so speed changes feel quicker over SPI.
   */
  ErasePreviousSpeedDigits();
  DrawSpeedArc(mph, 0U);
  DrawSpeedDigits(mph);
  last_mph = mph;
}

void DashUI_UpdateGpsStatus(uint8_t has_rx, uint8_t has_line, uint8_t has_fix)
{
  has_rx = (has_rx != 0U) ? 1U : 0U;
  has_line = (has_line != 0U) ? 1U : 0U;
  has_fix = (has_fix != 0U) ? 1U : 0U;

  if (has_rx != last_gps_rx)
  {
    DrawStatusBox(170U, has_rx, DASH_ORANGE);
    last_gps_rx = has_rx;
  }

  if (has_line != last_gps_line)
  {
    DrawStatusBox(219U, has_line, DASH_ORANGE);
    last_gps_line = has_line;
  }

  if (has_fix != last_gps_fix)
  {
    DrawStatusBox(268U, has_fix, DASH_GREEN);
    last_gps_fix = has_fix;
  }
}
