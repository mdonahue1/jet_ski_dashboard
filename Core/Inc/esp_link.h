#ifndef ESP_LINK_H
#define ESP_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

typedef struct
{
  uint32_t rx_byte_count;
  uint32_t line_count;
  uint32_t heartbeat_count;
  uint32_t error_count;
  uint32_t last_heartbeat_ms;
  uint32_t scan_id;
  uint16_t network_count;
} ESP_LinkData_t;

void ESP_LinkInit(UART_HandleTypeDef *huart);
void ESP_LinkTask(void);
ESP_LinkData_t ESP_LinkGetData(void);
uint8_t ESP_LinkIsOnline(void);
void ESP_LinkUartRxCpltCallback(UART_HandleTypeDef *huart);
void ESP_LinkUartErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* ESP_LINK_H */
