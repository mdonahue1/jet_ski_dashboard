#include "gps.h"
#include "esp_link.h"
#include <string.h>

#define GPS_LINE_MAX 96U
#define GPS_COMMAND_MAX 96U

/*
 * gps_uart points to whichever UART main.c selected for the GPS module.
 * Right now that is USART1 on PA10 / Arduino D2, but keeping the handle generic
 * means this driver does not care which STM32 UART is used.
 */
static UART_HandleTypeDef *gps_uart = 0;

/*
 * HAL_UART_Receive_IT() needs a persistent byte buffer because the interrupt
 * may complete long after the function call returns. This one byte is reused
 * forever: receive one byte, process it, arm the next one.
 */
static uint8_t rx_byte = 0U;

/*
 * rx_line is built inside the UART interrupt one character at a time.
 * ready_line is a complete NMEA sentence copied out for the main loop to parse.
 * Keeping parsing out of the IRQ keeps interrupts short and predictable.
 */
static char rx_line[GPS_LINE_MAX];
static uint16_t rx_index = 0U;

static volatile uint8_t line_ready = 0U;
static char ready_line[GPS_LINE_MAX];

static GPS_Data_t gps_data = { 0U };

static char HexNibble(uint8_t value)
{
  value &= 0x0FU;
  if (value < 10U)
  {
    return (char)('0' + value);
  }

  return (char)('A' + (value - 10U));
}

static uint8_t BuildNmeaCommand(const char *payload, char *out, uint8_t out_size)
{
  uint8_t checksum = 0U;
  uint8_t i = 0U;
  uint8_t out_index = 0U;

  if ((payload == 0) || (out == 0) || (out_size < 7U))
  {
    return 0U;
  }

  /*
   * GPS configuration commands use the same wrapper style as NMEA sentences:
   *   $PAYLOAD*CS<CR><LF>
   * CS is an XOR of every payload byte between '$' and '*'.
   */
  out[out_index++] = '$';
  while ((payload[i] != '\0') && (out_index < (uint8_t)(out_size - 6U)))
  {
    checksum ^= (uint8_t)payload[i];
    out[out_index++] = payload[i++];
  }

  if (payload[i] != '\0')
  {
    return 0U;
  }

  out[out_index++] = '*';
  out[out_index++] = HexNibble((uint8_t)(checksum >> 4));
  out[out_index++] = HexNibble(checksum);
  out[out_index++] = '\r';
  out[out_index++] = '\n';
  out[out_index] = '\0';

  return out_index;
}

static void SendNmeaCommand(UART_HandleTypeDef *tx_uart, const char *payload)
{
  char command[GPS_COMMAND_MAX];
  uint8_t length;

  if (tx_uart == 0)
  {
    return;
  }

  length = BuildNmeaCommand(payload, command, sizeof(command));
  if (length == 0U)
  {
    return;
  }

  /*
   * This is a short blocking transmit during startup only. It does not affect
   * steady-state GPS receive performance because normal GPS data is interrupt
   * driven after GPS_Init() arms RX.
   */
  (void)HAL_UART_Transmit(tx_uart, (uint8_t *)command, length, 100U);
  HAL_Delay(50U);
}

static void ArmGpsReceive(void)
{
  if (gps_uart == 0)
  {
    return;
  }

  /*
   * HAL_UART_Receive_IT() arms the UART peripheral and returns immediately.
   * The byte arrives later in the USART interrupt, then HAL calls
   * HAL_UART_RxCpltCallback(). If arming fails, count it for diagnostics.
   */
  if (HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U) != HAL_OK)
  {
    gps_data.error_count++;
  }
}

/*
 * Convert an ASCII decimal like "12.34" knots into centi-knots: 1234.
 * "Centi" means hundredths. Using integer hundredths avoids floating point,
 * which keeps the code smaller and more predictable on the MCU.
 */
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

static uint32_t ParseUnsignedInteger(const char *text)
{
  uint32_t value = 0U;
  while ((*text >= '0') && (*text <= '9'))
  {
    value = (value * 10U) + (uint32_t)(*text - '0');
    text++;
  }
  return value;
}

/* Convert NMEA ddmm.mmmm / dddmm.mmmm coordinates to decimal degrees * 1e7. */
static int32_t ParseCoordinateE7(const char *text, uint8_t degree_digits, char hemisphere)
{
  uint32_t degrees = 0U;
  uint32_t minutes_whole = 0U;
  uint32_t minutes_frac = 0U;
  uint8_t i;
  uint8_t frac_digits = 0U;
  int32_t result;

  for (i = 0U; i < degree_digits; i++)
  {
    if ((text[i] < '0') || (text[i] > '9'))
    {
      return 0;
    }
    degrees = (degrees * 10U) + (uint32_t)(text[i] - '0');
  }
  text += degree_digits;
  while ((*text >= '0') && (*text <= '9'))
  {
    minutes_whole = (minutes_whole * 10U) + (uint32_t)(*text - '0');
    text++;
  }
  if (*text == '.')
  {
    text++;
    while ((*text >= '0') && (*text <= '9') && (frac_digits < 4U))
    {
      minutes_frac = (minutes_frac * 10U) + (uint32_t)(*text - '0');
      frac_digits++;
      text++;
    }
  }
  while (frac_digits < 4U)
  {
    minutes_frac *= 10U;
    frac_digits++;
  }

  result = (int32_t)((degrees * 10000000UL) +
                     (uint32_t)((((uint64_t)((minutes_whole * 10000UL) + minutes_frac)) * 10000000ULL) /
                                600000ULL));
  if ((hemisphere == 'S') || (hemisphere == 'W'))
  {
    result = -result;
  }
  return result;
}

static uint8_t IsRmcSentence(const char *line)
{
  /*
   * NMEA talker IDs can vary: $GPRMC, $GNRMC, etc. The RMC part starts at
   * characters 3-5, so checking those bytes accepts both GPS-only and GNSS
   * combined sentences.
   */
  return ((line[0] == '$') &&
          (line[3] == 'R') &&
          (line[4] == 'M') &&
          (line[5] == 'C'));
}

static uint8_t IsVtgSentence(const char *line)
{
  /*
   * VTG is "course over ground and ground speed". Like RMC, the talker prefix
   * can vary, so $GPVTG and $GNVTG are both accepted.
   */
  return ((line[0] == '$') &&
          (line[3] == 'V') &&
          (line[4] == 'T') &&
          (line[5] == 'G'));
}

static uint8_t IsGgaSentence(const char *line)
{
  return ((line[0] == '$') && (line[3] == 'G') && (line[4] == 'G') && (line[5] == 'A'));
}

static uint8_t IsGsaSentence(const char *line)
{
  return ((line[0] == '$') && (line[3] == 'G') && (line[4] == 'S') && (line[5] == 'A'));
}

static uint8_t IsGsvSentence(const char *line)
{
  return ((line[0] == '$') && (line[3] == 'G') && (line[4] == 'S') && (line[5] == 'V'));
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

  /*
   * NMEA fields are comma-separated, ending before the optional checksum '*'.
   * Field 0 is the sentence ID, field 1 is the first value after it, and so on.
   */
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
  char course_text[16];
  char latitude_text[16];
  char ns_text[2];
  char longitude_text[16];
  char ew_text[2];

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
  CopyNmeaField(line, 8U, course_text, sizeof(course_text));
  CopyNmeaField(line, 3U, latitude_text, sizeof(latitude_text));
  CopyNmeaField(line, 4U, ns_text, sizeof(ns_text));
  CopyNmeaField(line, 5U, longitude_text, sizeof(longitude_text));
  CopyNmeaField(line, 6U, ew_text, sizeof(ew_text));

  if ((status_text[0] == 'A') && (speed_knots_text[0] != '\0'))
  {
    uint32_t centi_knots = ParseCentiKnots(speed_knots_text);

    /*
     * RMC status 'A' means active/valid fix. 'V' means void, which usually
     * happens indoors, during startup, or when satellites are not locked yet.
     */
    gps_data.has_fix = 1U;
    gps_data.speed_valid = 1U;
    gps_data.speed_mph = CentiKnotsToMph(centi_knots);
    if ((latitude_text[0] != '\0') && (longitude_text[0] != '\0'))
    {
      gps_data.latitude_e7 = ParseCoordinateE7(latitude_text, 2U, ns_text[0]);
      gps_data.longitude_e7 = ParseCoordinateE7(longitude_text, 3U, ew_text[0]);
      gps_data.position_valid = 1U;
    }
    if (course_text[0] != '\0')
    {
      gps_data.heading_deg = (uint16_t)(((ParseCentiKnots(course_text) + 50U) / 100U) % 360U);
      gps_data.heading_valid = 1U;
    }
    else
    {
      gps_data.heading_valid = 0U;
    }
  }
  else
  {
    gps_data.has_fix = 0U;
    gps_data.speed_valid = 0U;
    gps_data.speed_mph = 0U;
    gps_data.heading_valid = 0U;
    gps_data.position_valid = 0U;
  }
}

static void ParseVtgSentence(char *line)
{
  char speed_knots_text[16];
  char course_text[16];
  char mode_text[2];
  uint8_t mode_allows_speed = 1U;

  if (!IsVtgSentence(line))
  {
    return;
  }

  gps_data.vtg_count++;

  /*
   * VTG fields:
   * 0 sentence ID, 1 course true, 2 T, 3 course magnetic, 4 M,
   * 5 speed in knots, 6 N, 7 speed in km/h, 8 K, 9 mode if present.
   *
   * Some modules emit VTG near RMC. Parsing both can reduce firmware-side
   * latency a bit, but the GPS module update rate is still the main limit.
   */
  CopyNmeaField(line, 5U, speed_knots_text, sizeof(speed_knots_text));
  CopyNmeaField(line, 1U, course_text, sizeof(course_text));
  CopyNmeaField(line, 9U, mode_text, sizeof(mode_text));

  if (mode_text[0] == 'N')
  {
    mode_allows_speed = 0U;
  }

  if ((speed_knots_text[0] != '\0') && (mode_allows_speed != 0U))
  {
    uint32_t centi_knots = ParseCentiKnots(speed_knots_text);

    gps_data.speed_valid = 1U;
    gps_data.speed_mph = CentiKnotsToMph(centi_knots);
    if (course_text[0] != '\0')
    {
      gps_data.heading_deg = (uint16_t)(((ParseCentiKnots(course_text) + 50U) / 100U) % 360U);
      gps_data.heading_valid = 1U;
    }
  }
}

static void ParseGgaSentence(char *line)
{
  char latitude_text[16];
  char ns_text[2];
  char longitude_text[16];
  char ew_text[2];
  char quality_text[4];
  char satellites_text[4];
  char altitude_text[12];
  char hdop_text[12];

  if (!IsGgaSentence(line))
  {
    return;
  }

  CopyNmeaField(line, 2U, latitude_text, sizeof(latitude_text));
  CopyNmeaField(line, 3U, ns_text, sizeof(ns_text));
  CopyNmeaField(line, 4U, longitude_text, sizeof(longitude_text));
  CopyNmeaField(line, 5U, ew_text, sizeof(ew_text));
  CopyNmeaField(line, 6U, quality_text, sizeof(quality_text));
  CopyNmeaField(line, 7U, satellites_text, sizeof(satellites_text));
  CopyNmeaField(line, 9U, altitude_text, sizeof(altitude_text));
  CopyNmeaField(line, 8U, hdop_text, sizeof(hdop_text));

  gps_data.fix_quality = (uint8_t)ParseUnsignedInteger(quality_text);
  gps_data.satellite_count = (uint8_t)ParseUnsignedInteger(satellites_text);
  if (hdop_text[0] != '\0')
  {
    gps_data.hdop_x100 = (uint16_t)ParseCentiKnots(hdop_text);
  }
  if ((gps_data.fix_quality != 0U) && (latitude_text[0] != '\0') && (longitude_text[0] != '\0'))
  {
    gps_data.latitude_e7 = ParseCoordinateE7(latitude_text, 2U, ns_text[0]);
    gps_data.longitude_e7 = ParseCoordinateE7(longitude_text, 3U, ew_text[0]);
    gps_data.position_valid = 1U;
  }
  if ((gps_data.fix_quality != 0U) && (altitude_text[0] != '\0'))
  {
    int32_t altitude = (int32_t)ParseUnsignedInteger((altitude_text[0] == '-') ? altitude_text + 1 : altitude_text);
    if (altitude_text[0] == '-')
    {
      altitude = -altitude;
    }
    if (altitude > 32767) altitude = 32767;
    if (altitude < -32768) altitude = -32768;
    gps_data.altitude_m = (int16_t)altitude;
    gps_data.altitude_valid = 1U;
  }
  else
  {
    gps_data.altitude_valid = 0U;
  }
}

static void InsertStrongSnr(uint8_t snr)
{
  uint8_t i;
  if (snr == 0U) return;
  for (i = 0U; i < 4U; i++)
  {
    if (snr > gps_data.strongest_snr[i])
    {
      uint8_t j;
      for (j = 3U; j > i; j--) gps_data.strongest_snr[j] = gps_data.strongest_snr[j - 1U];
      gps_data.strongest_snr[i] = snr;
      if (gps_data.snr_count < 4U) gps_data.snr_count++;
      return;
    }
  }
}

static void ParseGsaSentence(char *line)
{
  char field[12];
  if (!IsGsaSentence(line)) return;

  CopyNmeaField(line, 2U, field, sizeof(field));
  gps_data.fix_type = (uint8_t)ParseUnsignedInteger(field);
  CopyNmeaField(line, 15U, field, sizeof(field));
  if (field[0] != '\0') gps_data.pdop_x100 = (uint16_t)ParseCentiKnots(field);
  CopyNmeaField(line, 16U, field, sizeof(field));
  if (field[0] != '\0') gps_data.hdop_x100 = (uint16_t)ParseCentiKnots(field);
  CopyNmeaField(line, 17U, field, sizeof(field));
  if (field[0] != '\0') gps_data.vdop_x100 = (uint16_t)ParseCentiKnots(field);
}

static void ParseGsvSentence(char *line)
{
  char field[12];
  uint8_t message_number;
  uint8_t field_index;
  if (!IsGsvSentence(line)) return;

  CopyNmeaField(line, 2U, field, sizeof(field));
  message_number = (uint8_t)ParseUnsignedInteger(field);
  CopyNmeaField(line, 3U, field, sizeof(field));
  gps_data.satellites_in_view = (uint8_t)ParseUnsignedInteger(field);
  if (message_number == 1U)
  {
    memset(gps_data.strongest_snr, 0, sizeof(gps_data.strongest_snr));
    gps_data.snr_count = 0U;
  }
  for (field_index = 7U; field_index <= 19U; field_index = (uint8_t)(field_index + 4U))
  {
    CopyNmeaField(line, field_index, field, sizeof(field));
    if (field[0] != '\0') InsertStrongSnr((uint8_t)ParseUnsignedInteger(field));
  }
}

static void ParseGpsSentence(char *line)
{
  ParseRmcSentence(line);
  ParseVtgSentence(line);
  ParseGgaSentence(line);
  ParseGsaSentence(line);
  ParseGsvSentence(line);
}

void GPS_Init(UART_HandleTypeDef *huart)
{
  gps_uart = huart;
  rx_index = 0U;
  line_ready = 0U;
  gps_data.has_fix = 0U;
  gps_data.speed_valid = 0U;
  gps_data.speed_mph = 0U;
  gps_data.heading_valid = 0U;
  gps_data.heading_deg = 0U;
  gps_data.position_valid = 0U;
  gps_data.latitude_e7 = 0;
  gps_data.longitude_e7 = 0;
  gps_data.altitude_valid = 0U;
  gps_data.altitude_m = 0;
  gps_data.satellite_count = 0U;
  gps_data.fix_quality = 0U;
  gps_data.fix_type = 1U;
  gps_data.satellites_in_view = 0U;
  gps_data.pdop_x100 = 0U;
  gps_data.hdop_x100 = 0U;
  gps_data.vdop_x100 = 0U;
  memset(gps_data.strongest_snr, 0, sizeof(gps_data.strongest_snr));
  gps_data.snr_count = 0U;
  gps_data.rx_byte_count = 0U;
  gps_data.line_count = 0U;
  gps_data.rmc_count = 0U;
  gps_data.vtg_count = 0U;
  gps_data.error_count = 0U;
  gps_data.last_rx_byte = 0U;

  /* Arm the first 1-byte receive. Each completed byte re-arms reception in the callback. */
  ArmGpsReceive();
}

void GPS_SendStartupConfig(UART_HandleTypeDef *tx_uart)
{
  /*
   * ATGM336H-style modules are commonly based on CASIC/ATGM firmware, while
   * many hobby GPS examples use MediaTek PMTK commands. Sending both families
   * is harmless in practice: unsupported commands are ignored by the receiver.
   *
   * Important wiring note:
   * - GPS TX -> PA10 / USART1_RX for data into the STM32.
   * - STM32 PA2 / USART2_TX -> GPS RX for these configuration commands.
   *
   * We keep the baud rate at 9600 for this first 10 Hz attempt. If the module
   * starts outputting too many sentences and UART error_count rises, the next
   * step is changing the GPS and STM32 UARTs together to 38400 or 115200 baud
   * and reducing the enabled sentence set.
   */

  /* RMC + GGA at 5 Hz fits 9600 baud while adding position, altitude, and satellites. */
  SendNmeaCommand(tx_uart, "PMTK314,0,1,0,1,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0");
  SendNmeaCommand(tx_uart, "PMTK220,200");
  SendNmeaCommand(tx_uart, "PMTK300,200,0,0,0,0");

  /* CASIC/ATGM PCAS: request a 100 ms positioning/output interval if accepted. */
  SendNmeaCommand(tx_uart, "PCAS02,200");
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
   * This is the core IRQ/main-loop handoff: the IRQ collects bytes, the task parses lines.
   */
  __disable_irq();
  strncpy(local_line, ready_line, GPS_LINE_MAX);
  local_line[GPS_LINE_MAX - 1U] = '\0';
  line_ready = 0U;
  __enable_irq();

  ParseGpsSentence(local_line);
}

GPS_Data_t GPS_GetData(void)
{
  GPS_Data_t snapshot;

  /*
   * gps_data can be changed by the UART callback at interrupt time. Disable
   * interrupts only long enough to copy the struct, then return the snapshot.
   */
  __disable_irq();
  snapshot = gps_data;
  __enable_irq();

  return snapshot;
}

void GPS_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((gps_uart == 0) || (huart != gps_uart))
  {
    return;
  }

  gps_data.rx_byte_count++;
  gps_data.last_rx_byte = rx_byte;

  if (rx_byte == '$')
  {
    /* '$' marks the start of a new NMEA sentence. Drop any partial garbage. */
    rx_index = 0U;
    rx_line[rx_index++] = (char)rx_byte;
  }
  else if ((rx_byte == '\n') || (rx_byte == '\r'))
  {
    if (rx_index > 0U)
    {
      /*
       * CR/LF marks the end of a sentence. Copy it to ready_line so the main
       * loop can parse a stable complete sentence outside the interrupt.
       */
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
    /* Normal sentence character. Save it if there is room for a terminator. */
    rx_line[rx_index++] = (char)rx_byte;
  }
  else
  {
    /* Overflow means the sentence was longer than expected. Drop it and wait for next '$'. */
    rx_index = 0U;
  }

  /* Re-arm reception so the next byte can interrupt us too. */
  ArmGpsReceive();
}

void GPS_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((gps_uart == 0) || (huart != gps_uart))
  {
    return;
  }

  rx_index = 0U;
  gps_data.error_count++;
  /* Framing/noise/overrun errors can stop reception; restart it immediately. */
  ArmGpsReceive();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  /*
   * This HAL callback name is fixed by STM32 HAL. We forward it into the GPS
   * driver so the rest of the project does not need GPS code in interrupt files.
   */
  GPS_UART_RxCpltCallback(huart);
  ESP_LinkUartRxCpltCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  /* Same forwarding pattern for UART error recovery. */
  GPS_UART_ErrorCallback(huart);
  ESP_LinkUartErrorCallback(huart);
}
