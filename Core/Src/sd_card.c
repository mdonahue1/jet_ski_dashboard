#include "sd_card.h"

#define SD_SPI_TIMEOUT_MS       100U
#define SD_INIT_TIMEOUT_MS      1500U
#define SD_TOKEN_TIMEOUT_MS     250U

#define SD_CMD0                 0U
#define SD_CMD8                 8U
#define SD_CMD13                13U
#define SD_CMD16                16U
#define SD_CMD17                17U
#define SD_CMD55                55U
#define SD_CMD58                58U
#define SD_ACMD41               41U

#define SD_R1_IDLE              0x01U
#define SD_R1_ILLEGAL_COMMAND   0x04U
#define SD_DATA_TOKEN           0xFEU

static SPI_HandleTypeDef *sd_spi = 0;
static uint8_t sd_ready = 0U;
static uint8_t sd_high_capacity = 0U;
static uint8_t sd_probe_sector[512];

static void SDCard_Select(void)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
}

static void SDCard_Unselect(void)
{
  uint8_t tx = 0xFFU;
  uint8_t rx;

  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
  (void)HAL_SPI_TransmitReceive(sd_spi, &tx, &rx, 1U, SD_SPI_TIMEOUT_MS);
}

static void SDCard_SetPrescaler(uint32_t prescaler)
{
  uint32_t start = HAL_GetTick();

  while ((__HAL_SPI_GET_FLAG(sd_spi, SPI_FLAG_BSY) != RESET) &&
         ((HAL_GetTick() - start) < SD_SPI_TIMEOUT_MS))
  {
  }

  __HAL_SPI_DISABLE(sd_spi);
  MODIFY_REG(sd_spi->Instance->CR1, SPI_CR1_BR, prescaler);
  __HAL_SPI_ENABLE(sd_spi);
}

static uint8_t SDCard_Byte(uint8_t value)
{
  uint8_t received = 0xFFU;

  if (HAL_SPI_TransmitReceive(sd_spi, &value, &received, 1U, SD_SPI_TIMEOUT_MS) != HAL_OK)
  {
    return 0xFFU;
  }
  return received;
}

static uint8_t SDCard_WaitReady(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  do
  {
    if (SDCard_Byte(0xFFU) == 0xFFU)
    {
      return 1U;
    }
  } while ((HAL_GetTick() - start) < timeout_ms);
  return 0U;
}

/* Send a command and leave CS asserted for any trailing response bytes. */
static uint8_t SDCard_Command(uint8_t command, uint32_t argument, uint8_t crc)
{
  uint8_t response = 0xFFU;
  uint8_t i;

  SDCard_Select();
  if (SDCard_WaitReady(SD_SPI_TIMEOUT_MS) == 0U)
  {
    return 0xFFU;
  }

  (void)SDCard_Byte((uint8_t)(0x40U | command));
  (void)SDCard_Byte((uint8_t)(argument >> 24));
  (void)SDCard_Byte((uint8_t)(argument >> 16));
  (void)SDCard_Byte((uint8_t)(argument >> 8));
  (void)SDCard_Byte((uint8_t)argument);
  (void)SDCard_Byte(crc);

  for (i = 0U; i < 10U; i++)
  {
    response = SDCard_Byte(0xFFU);
    if ((response & 0x80U) == 0U)
    {
      break;
    }
  }
  return response;
}

static uint8_t SDCard_ReadSelected(uint32_t block, uint8_t *data)
{
  uint8_t response;
  uint8_t token = 0xFFU;
  uint16_t i;
  uint32_t start;
  uint32_t address = sd_high_capacity ? block : (block * 512U);

  response = SDCard_Command(SD_CMD17, address, 0x01U);
  if (response != 0x00U)
  {
    SDCard_Unselect();
    return 0U;
  }

  start = HAL_GetTick();
  do
  {
    token = SDCard_Byte(0xFFU);
    if (token == SD_DATA_TOKEN)
    {
      break;
    }
  } while ((HAL_GetTick() - start) < SD_TOKEN_TIMEOUT_MS);

  if (token != SD_DATA_TOKEN)
  {
    SDCard_Unselect();
    return 0U;
  }

  for (i = 0U; i < 512U; i++)
  {
    data[i] = SDCard_Byte(0xFFU);
  }
  (void)SDCard_Byte(0xFFU);
  (void)SDCard_Byte(0xFFU);
  SDCard_Unselect();
  return 1U;
}

uint8_t SDCard_Init(SPI_HandleTypeDef *hspi)
{
  uint8_t response;
  uint8_t r7[4];
  uint8_t ocr[4];
  uint8_t i;
  uint8_t version2 = 0U;
  uint32_t start;

  if (hspi == 0)
  {
    return 0U;
  }

  sd_spi = hspi;
  sd_ready = 0U;
  sd_high_capacity = 0U;
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /* 84 MHz APB2 / 256 = 328 kHz, below the SD initialization limit. */
  SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_256);
  for (i = 0U; i < 10U; i++)
  {
    (void)SDCard_Byte(0xFFU);
  }

  response = SDCard_Command(SD_CMD0, 0U, 0x95U);
  SDCard_Unselect();
  if (response != SD_R1_IDLE)
  {
    SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
    return 0U;
  }

  response = SDCard_Command(SD_CMD8, 0x000001AAU, 0x87U);
  if (response == SD_R1_IDLE)
  {
    for (i = 0U; i < 4U; i++)
    {
      r7[i] = SDCard_Byte(0xFFU);
    }
    version2 = ((r7[2] == 0x01U) && (r7[3] == 0xAAU)) ? 1U : 0U;
  }
  else if ((response & SD_R1_ILLEGAL_COMMAND) == 0U)
  {
    SDCard_Unselect();
    SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
    return 0U;
  }
  SDCard_Unselect();

  start = HAL_GetTick();
  do
  {
    response = SDCard_Command(SD_CMD55, 0U, 0x01U);
    SDCard_Unselect();
    if ((response != 0x00U) && (response != SD_R1_IDLE))
    {
      break;
    }

    response = SDCard_Command(SD_ACMD41, version2 ? 0x40000000U : 0U, 0x01U);
    SDCard_Unselect();
    if (response == 0x00U)
    {
      break;
    }
  } while ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS);

  if (response != 0x00U)
  {
    SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
    return 0U;
  }

  response = SDCard_Command(SD_CMD58, 0U, 0x01U);
  if (response == 0x00U)
  {
    for (i = 0U; i < 4U; i++)
    {
      ocr[i] = SDCard_Byte(0xFFU);
    }
    sd_high_capacity = ((ocr[0] & 0x40U) != 0U) ? 1U : 0U;
  }
  SDCard_Unselect();
  if (response != 0x00U)
  {
    SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
    return 0U;
  }

  if (sd_high_capacity == 0U)
  {
    response = SDCard_Command(SD_CMD16, 512U, 0x01U);
    SDCard_Unselect();
    if (response != 0x00U)
    {
      SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
      return 0U;
    }
  }

  /* Prove the reader works by reading sector zero, then return SPI to the TFT. */
  SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_4);
  sd_ready = SDCard_ReadSelected(0U, sd_probe_sector);
  SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
  return sd_ready;
}

uint8_t SDCard_ReadBlock(uint32_t block, uint8_t *data)
{
  uint8_t result;

  if ((sd_ready == 0U) || (sd_spi == 0) || (data == 0))
  {
    return 0U;
  }

  SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_4);
  result = SDCard_ReadSelected(block, data);
  SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
  if (result == 0U)
  {
    sd_ready = 0U;
  }
  return result;
}

uint8_t SDCard_Poll(void)
{
  uint8_t response;
  uint8_t status2;

  if (sd_spi == 0)
  {
    return 0U;
  }
  if (sd_ready == 0U)
  {
    return SDCard_Init(sd_spi);
  }

  SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_4);
  response = SDCard_Command(SD_CMD13, 0U, 0x01U);
  status2 = SDCard_Byte(0xFFU);
  SDCard_Unselect();
  SDCard_SetPrescaler(SPI_BAUDRATEPRESCALER_2);
  sd_ready = ((response == 0x00U) && (status2 == 0x00U)) ? 1U : 0U;
  return sd_ready;
}
