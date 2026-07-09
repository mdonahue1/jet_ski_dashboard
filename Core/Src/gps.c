#include "gps.h"
#include <string.h>

#define GPS_LINE_MAX 96U

static UART_HandleTypeDef *gps_uart = 0;
static uint8_t rx_byte = 0U;

static char rx_line[GPS_LINE_MAX];
static uint16_t rx_index = 0U;

static volatile uint8_t line_ready = 0U;
static char ready_line[GPS_LINE_MAX];

static GPS_Data_t gps_data = { 0U, 0U, 0U };

/* Convert an ASCII decimal like "12.34" knots into centi-knots: 1234. */
static uint32_t ParseCentiKnots(const char *text)
{
  uint32_t whole = 0U;
  uint32_t frac = 0U;
  uint8_t frac_digits = 0U;

  while ((*text >= '0') && (*text <= '9'))
  {
    whole = (whole * 10U) + (uint32_t)(*text - '0');
    text++;
  }

  if (*text == '.')
  {
    text++;
    while ((*text >= '0') && (*text <= '9') && (frac_digits < 2U))
    {
      frac = (frac * 10U) + (uint32_t)(*text - '0');
      frac_digits++;
      text++;
    }
  }

  while (frac_digits < 2U)
  {
    frac *= 10U;
    frac_digits++;
  }

  return (whole * 100U) + frac;
}

static uint16_t CentiKnotsToMph(uint32_t centi_knots)
{
  /*
   * 1 knot = 1.15078 mph.
   * centi_knots is knots * 100, so this integer math returns rounded MPH.
   */
  return (uint16_t)(((centi_knots * 115078U) + 5000000U) / 10000000U);
}

static uint8_t IsRmcSentence(const char *line)
{
  return ((line[0] == '$') &&
          (line[3] == 'R') &&
          (line[4] == 'M') &&
          (line[5] == 'C'));
}

static void CopyNmeaField(const char *line, uint8_t wanted_field, char *out, uint8_t out_size)
{
  uint8_t field = 0U;
  uint8_t out_index = 0U;

  if (out_size == 0U)
  {
    return;
  }

  out[0] = '\0';

  while ((*line != '\0') && (*line != '*'))
  {
    if (field == wanted_field)
    {
      while ((*line != '\0') && (*line != ',') && (*line != '*') && (out_index < (uint8_t)(out_size - 1U)))
      {
        out[out_index++] = *line++;
      }
      out[out_index] = '\0';
      return;
    }

    if (*line == ',')
    {
      field++;
    }

    line++;
  }
}

static void ParseRmcSentence(char *line)
{
  char status_text[2];
  char speed_knots_text[16];

  if (!IsRmcSentence(line))
  {
    return;
  }

  gps_data.rmc_count++;

  /*
   * RMC fields:
   * 0 sentence ID, 1 time, 2 status A/V, 3 lat, 4 N/S, 5 lon, 6 E/W,
   * 7 speed in knots, 8 course, 9 date, ...
   */
  CopyNmeaField(line, 2U, status_text, sizeof(status_text));
  CopyNmeaField(line, 7U, speed_knots_text, sizeof(speed_knots_text));

  if ((status_text[0] == 'A') && (speed_knots_text[0] != '\0'))
  {
    uint32_t centi_knots = ParseCentiKnots(speed_knots_text);

    gps_data.has_fix = 1U;
    gps_data.speed_valid = 1U;
    gps_data.speed_mph = CentiKnotsToMph(centi_knots);
  }
  else
  {
    gps_data.has_fix = 0U;
    gps_data.speed_valid = 0U;
    gps_data.speed_mph = 0U;
  }
}

void GPS_Init(UART_HandleTypeDef *huart)
{
  gps_uart = huart;
  rx_index = 0U;
  line_ready = 0U;
  gps_data.has_fix = 0U;
  gps_data.speed_valid = 0U;
  gps_data.speed_mph = 0U;
  gps_data.rx_byte_count = 0U;
  gps_data.line_count = 0U;
  gps_data.rmc_count = 0U;

  /* Arm the first 1-byte receive. Each completed byte re-arms reception in the callback. */
  (void)HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U);
}

void GPS_Task(void)
{
  char local_line[GPS_LINE_MAX];

  if (line_ready == 0U)
  {
    return;
  }

  /*
   * Copy the ready line with interrupts briefly disabled so the UART IRQ cannot modify it
   * halfway through this read. Parsing happens afterward in normal main-loop context.
   */
  __disable_irq();
  strncpy(local_line, ready_line, GPS_LINE_MAX);
  local_line[GPS_LINE_MAX - 1U] = '\0';
  line_ready = 0U;
  __enable_irq();

  ParseRmcSentence(local_line);
}

GPS_Data_t GPS_GetData(void)
{
  return gps_data;
}

void GPS_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((gps_uart == 0) || (huart != gps_uart))
  {
    return;
  }

  gps_data.rx_byte_count++;

  if (rx_byte == '$')
  {
    rx_index = 0U;
    rx_line[rx_index++] = (char)rx_byte;
  }
  else if ((rx_byte == '\n') || (rx_byte == '\r'))
  {
    if (rx_index > 0U)
    {
      rx_line[rx_index] = '\0';
      strncpy(ready_line, rx_line, GPS_LINE_MAX);
      ready_line[GPS_LINE_MAX - 1U] = '\0';
      line_ready = 1U;
      gps_data.line_count++;
      rx_index = 0U;
    }
  }
  else if (rx_index < (GPS_LINE_MAX - 1U))
  {
    rx_line[rx_index++] = (char)rx_byte;
  }
  else
  {
    /* Overflow means the sentence was longer than expected. Drop it and wait for next '$'. */
    rx_index = 0U;
  }

  (void)HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U);
}

void GPS_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((gps_uart == 0) || (huart != gps_uart))
  {
    return;
  }

  rx_index = 0U;
  (void)HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  GPS_UART_RxCpltCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  GPS_UART_ErrorCallback(huart);
}
