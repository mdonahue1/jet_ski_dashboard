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

  /* Diagnostics used by the on-screen status boxes and bench testing. */
  uint32_t rx_byte_count;
  uint32_t line_count;
  uint32_t rmc_count;
  uint32_t error_count;
  uint8_t last_rx_byte;
} GPS_Data_t;

/* Start byte-by-byte UART reception from the GPS module. */
void GPS_Init(UART_HandleTypeDef *huart);

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
