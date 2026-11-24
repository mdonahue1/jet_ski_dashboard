#ifndef SD_CARD_H
#define SD_CARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

uint8_t SDCard_Init(SPI_HandleTypeDef *hspi);
uint8_t SDCard_Poll(void);
uint8_t SDCard_ReadBlock(uint32_t block, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */
