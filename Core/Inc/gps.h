#ifndef GPS_H
#define GPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

typedef struct
{
  /* 1 when the latest parsed RMC sentence says the GPS fix is valid. */
  uint8_t has_fix;

  /* 1 when speed_mph contains a current parsed speed. */
  uint8_t speed_valid;

  /* Whole-MPH speed converted from the RMC speed-in-knots field. */
  uint16_t speed_mph;

  /* True course-over-ground in whole degrees (0=N, 90=E, 180=S, 270=W). */
  uint8_t heading_valid;
  uint16_t heading_deg;

  /* Signed decimal degrees scaled by 10,000,000 for exact integer storage. */
  uint8_t position_valid;
  int32_t latitude_e7;
  int32_t longitude_e7;

  /* GGA-derived navigation quality fields. */
  uint8_t altitude_valid;
  int16_t altitude_m;
  uint8_t satellite_count;
  uint8_t fix_quality;

  /* GSA/GSV quality and signal data. Values ending in _x100 have 2 decimals. */
  uint8_t fix_type;
  uint8_t satellites_in_view;
  uint16_t pdop_x100;
  uint16_t hdop_x100;
  uint16_t vdop_x100;
  uint8_t strongest_snr[4];
  uint8_t snr_count;

  /* Diagnostics used by the on-screen status boxes and bench testing. */
  uint32_t rx_byte_count;
  uint32_t line_count;
  uint32_t rmc_count;
  uint32_t vtg_count;
  uint32_t error_count;
  uint8_t last_rx_byte;
} GPS_Data_t;

/* Start byte-by-byte UART reception from the GPS module. */
void GPS_Init(UART_HandleTypeDef *huart);

/*
 * Try to configure the GPS module for faster NMEA output.
 * rx_uart is the UART used to listen to GPS TX.
 * tx_uart is a separate UART whose TX pin is wired to GPS RX.
 */
void GPS_SendStartupConfig(UART_HandleTypeDef *tx_uart);

/* Called from the main loop. Parses any complete NMEA sentence captured by the IRQ. */
void GPS_Task(void);

/* Copy the latest parsed GPS data into user code. */
GPS_Data_t GPS_GetData(void);

/* HAL calls this when one UART byte arrives. gps.c uses it to keep reception running. */
void GPS_UART_RxCpltCallback(UART_HandleTypeDef *huart);

/* HAL calls this on UART framing/noise/overrun errors. gps.c restarts reception. */
void GPS_UART_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */
