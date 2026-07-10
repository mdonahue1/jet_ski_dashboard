#include "dash_ui.h"
#include "st7796.h"

/* Screen colors are RGB565. Named UI colors keep layout code readable. */
#define DASH_BG       ST7796_BLACK
#define DASH_DIM      0x3186U
#define DASH_DARK     0x1082U
#define DASH_ARC_OFF  0x2945U
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
 * The gauge arc has one block every 5 MPH: 0, 5, 10, ... 80.
 * That gives 17 blocks total and keeps updates cheap over SPI.
 */
#define ARC_STEP_MPH  5U
#define ARC_BLOCK     14U

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
static uint8_t last_arc_blocks = 0xFFU;

/*
 * Dotted circular gauge arc. The points are spaced from 0 MPH at lower-left
 * through 40 MPH at top-center to 80 MPH at lower-right.
 *
 * These coordinates were precomputed instead of using sin/cos on the STM32.
 * The MCU can do trig, but fixed tables are faster, simpler, and deterministic.
 */
static const uint16_t arc_block_xy[17][2] = {
  { 122U, 240U }, { 109U, 207U }, { 104U, 172U }, { 109U, 137U }, { 122U, 104U },
  { 144U,  76U }, { 172U,  54U }, { 205U,  41U }, { 240U,  36U },
  { 275U,  41U }, { 308U,  54U }, { 336U,  76U }, { 358U, 104U },
  { 371U, 137U }, { 376U, 172U }, { 371U, 207U }, { 358U, 240U }
};

/*
 * Inner ring points make the gauge read as a circular instrument even at 0 MPH.
 * They are a fixed ellipse so no runtime trig or floating point is needed.
 * The ring is decorative/static; the outer arc is the live speed indicator.
 */
static const uint16_t gauge_ring_xy[25][2] = {
  { 110U, 218U }, {  99U, 200U }, {  92U, 181U }, {  90U, 162U }, {  92U, 143U },
  {  99U, 124U }, { 110U, 106U }, { 125U,  90U }, { 144U,  76U }, { 165U,  65U },
  { 189U,  57U }, { 214U,  52U }, { 240U,  50U }, { 266U,  52U }, { 291U,  57U },
  { 315U,  65U }, { 336U,  76U }, { 355U,  90U }, { 370U, 106U }, { 381U, 124U },
  { 388U, 143U }, { 390U, 162U }, { 388U, 181U }, { 381U, 200U }, { 370U, 218U }
};

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
        ST7796_FillRect((uint16_t)(x + (col * (DIGIT_CELL_W + DIGIT_CELL_G))),
                        (uint16_t)(y + (row * (DIGIT_CELL_H + DIGIT_CELL_G))),
                        DIGIT_CELL_W,
                        DIGIT_CELL_H,
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
  uint8_t i;

  for (i = 0U; i < 25U; i++)
  {
    uint16_t x = gauge_ring_xy[i][0];
    uint16_t y = gauge_ring_xy[i][1];

    ST7796_FillRect((uint16_t)(x - 3U), (uint16_t)(y - 3U), 6U, 6U, DASH_DIM);
  }
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

static void DrawArcBlock(uint8_t index, uint8_t active)
{
  uint16_t x;
  uint16_t y;
  uint16_t color;

  if (index >= 17U)
  {
    return;
  }

  x = arc_block_xy[index][0];
  y = arc_block_xy[index][1];
  /*
   * The block index maps directly to speed because each block is ARC_STEP_MPH.
   * Example: index 8 means about 40 MPH.
   */
  color = active ? GaugeColorForMph((uint16_t)(index * ARC_STEP_MPH)) : DASH_ARC_OFF;

  ST7796_FillRect((uint16_t)(x - (ARC_BLOCK / 2U)),
                  (uint16_t)(y - (ARC_BLOCK / 2U)),
                  ARC_BLOCK,
                  ARC_BLOCK,
                  color);
}

static void DrawGaugeScale(void)
{
  uint16_t i;

  for (i = 0U; i < 17U; i++)
  {
    DrawArcBlock((uint8_t)i, 0U);
  }

  /* Major reference ticks at 0, 20, 40, 60, and 80 MPH. */
  for (i = 0U; i < 5U; i++)
  {
    uint8_t index = (uint8_t)(i * 4U);
    uint16_t x = arc_block_xy[index][0];
    uint16_t y = arc_block_xy[index][1];

    ST7796_FillRect((uint16_t)(x - 9U), (uint16_t)(y - 9U), 18U, 18U, DASH_DIM);
    ST7796_FillRect((uint16_t)(x - 4U), (uint16_t)(y - 4U), 8U, 8U, DASH_WHITE);
  }
}

static void DrawSpeedArc(uint16_t mph)
{
  uint8_t active_blocks;
  uint8_t i;

  if (mph > MPH_MAX)
  {
    mph = MPH_MAX;
  }

  /*
   * Add one so 0 MPH still lights the first block. That gives the gauge a
   * visible starting point instead of disappearing at rest.
   */
  active_blocks = (uint8_t)((mph / ARC_STEP_MPH) + 1U);
  if (active_blocks > 17U)
  {
    active_blocks = 17U;
  }

  if (active_blocks == last_arc_blocks)
  {
    return;
  }

  for (i = 0U; i < 17U; i++)
  {
    DrawArcBlock(i, (i < active_blocks) ? 1U : 0U);
  }

  last_arc_blocks = active_blocks;
}

static void DrawStatusBox(uint16_t x, uint8_t active, uint16_t active_color)
{
  ST7796_FillRect(x, 282U, 42U, 18U, active ? active_color : DASH_DARK);
  ST7796_FillRect(x, 282U, 42U, 2U, DASH_WHITE);
  ST7796_FillRect(x, 298U, 42U, 2U, DASH_WHITE);
  ST7796_FillRect(x, 282U, 2U, 18U, DASH_WHITE);
  ST7796_FillRect(x + 40U, 282U, 2U, 18U, DASH_WHITE);
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

  ST7796_FillRect(DIGIT_AREA_X, DIGIT_Y, DIGIT_AREA_W, DIGIT_H, DASH_BG);

  for (i = 0U; i < digit_count; i++)
  {
    DrawDigit((uint16_t)(x + (i * (DIGIT_W + DIGIT_GAP))), DIGIT_Y, digits[i], DASH_WHITE);
  }
}

void DashUI_Init(void)
{
  /* Force a clean first speed redraw after the static screen is painted. */
  last_mph = 0xFFFFU;
  last_gps_rx = 0xFFU;
  last_gps_line = 0xFFU;
  last_gps_fix = 0xFFU;
  last_arc_blocks = 0xFFU;

  /*
   * Static dashboard background: border, circular gauge arc, label, and
   * diagnostics. CubeMX will not edit this file, so layout changes belong here.
   */
  ST7796_Fill(DASH_BG);
  ST7796_FillRect(0U, 0U, ST7796_WIDTH, 6U, DASH_WHITE);
  ST7796_FillRect(0U, ST7796_HEIGHT - 6U, ST7796_WIDTH, 6U, DASH_WHITE);
  ST7796_FillRect(0U, 0U, 6U, ST7796_HEIGHT, DASH_WHITE);
  ST7796_FillRect(ST7796_WIDTH - 6U, 0U, 6U, ST7796_HEIGHT, DASH_WHITE);
  ST7796_FillRect(32U, 268U, 416U, 4U, DASH_DIM);

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

  DrawSpeedDigits(mph);
  DrawSpeedArc(mph);
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
