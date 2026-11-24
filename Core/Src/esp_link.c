#include "esp_link.h"

#include <string.h>
#include <stdio.h>

#define ESP_LINE_MAX          128U
#define ESP_ONLINE_TIMEOUT_MS 2500U

static UART_HandleTypeDef *esp_uart = 0;
static uint8_t rx_byte = 0U;
static char rx_line[ESP_LINE_MAX];
static char ready_line[ESP_LINE_MAX];
static volatile uint8_t rx_index = 0U;
static volatile uint8_t line_ready = 0U;
static volatile ESP_LinkData_t esp_data;
static uint32_t last_ping_ms = 0U;

static void ArmEspReceive(void)
{
  if ((esp_uart != 0) && (HAL_UART_Receive_IT(esp_uart, &rx_byte, 1U) != HAL_OK))
  {
    esp_data.error_count++;
  }
}

void ESP_LinkInit(UART_HandleTypeDef *huart)
{
  esp_uart = huart;
  memset((void *)&esp_data, 0, sizeof(esp_data));
  rx_index = 0U;
  line_ready = 0U;
  last_ping_ms = HAL_GetTick();
  ArmEspReceive();
}

void ESP_LinkTask(void)
{
  char line[ESP_LINE_MAX];
  static const uint8_t ping[] = "STM,PING\n";

  /* A low-rate reverse-path probe is also useful for future command framing. */
  if ((esp_uart != 0) && ((HAL_GetTick() - last_ping_ms) >= 1000U))
  {
    (void)HAL_UART_Transmit(esp_uart, (uint8_t *)ping, sizeof(ping) - 1U, 20U);
    last_ping_ms = HAL_GetTick();
  }

  __disable_irq();
  if (line_ready == 0U)
  {
    __enable_irq();
    return;
  }
  strncpy(line, ready_line, ESP_LINE_MAX);
  line[ESP_LINE_MAX - 1U] = '\0';
  line_ready = 0U;
  __enable_irq();

  /* The XIAO test firmware sends C6,READY,<counter> once per second. */
  if (strncmp(line, "C6,READY,", 9U) == 0)
  {
    esp_data.heartbeat_count++;
    esp_data.last_heartbeat_ms = HAL_GetTick();
  }
  else if (strncmp(line, "WD,BEGIN,", 9U) == 0)
  {
    unsigned long scan_id = 0UL;
    unsigned int network_count = 0U;
    if (sscanf(line + 9, "%lu,%u", &scan_id, &network_count) == 2)
    {
      esp_data.scan_id = (uint32_t)scan_id;
      esp_data.network_count = (network_count > 999U) ? 999U : (uint16_t)network_count;
    }
  }
}

ESP_LinkData_t ESP_LinkGetData(void)
{
  ESP_LinkData_t snapshot;

  __disable_irq();
  snapshot = esp_data;
  __enable_irq();
  return snapshot;
}

uint8_t ESP_LinkIsOnline(void)
{
  ESP_LinkData_t snapshot = ESP_LinkGetData();

  if (snapshot.heartbeat_count == 0U)
  {
    return 0U;
  }
  return ((HAL_GetTick() - snapshot.last_heartbeat_ms) <= ESP_ONLINE_TIMEOUT_MS) ? 1U : 0U;
}

void ESP_LinkUartRxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((esp_uart == 0) || (huart != esp_uart))
  {
    return;
  }

  esp_data.rx_byte_count++;
  if ((rx_byte == '\n') || (rx_byte == '\r'))
  {
    if (rx_index > 0U)
    {
      rx_line[rx_index] = '\0';
      strncpy(ready_line, rx_line, ESP_LINE_MAX);
      ready_line[ESP_LINE_MAX - 1U] = '\0';
      line_ready = 1U;
      esp_data.line_count++;
      rx_index = 0U;
    }
  }
  else if (rx_index < (ESP_LINE_MAX - 1U))
  {
    rx_line[rx_index++] = (char)rx_byte;
  }
  else
  {
    rx_index = 0U;
  }

  ArmEspReceive();
}

void ESP_LinkUartErrorCallback(UART_HandleTypeDef *huart)
{
  if ((esp_uart == 0) || (huart != esp_uart))
  {
    return;
  }

  rx_index = 0U;
  esp_data.error_count++;
  ArmEspReceive();
}
