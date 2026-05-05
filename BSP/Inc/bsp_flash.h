#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include "stm32f4xx_hal.h"

HAL_StatusTypeDef BSP_Flash_EraseSector(uint32_t sector);
HAL_StatusTypeDef BSP_Flash_Write(uint32_t address, const uint8_t *data, uint32_t length);
void BSP_Flash_Read(uint32_t address, uint8_t *data, uint32_t length);

#endif /* BSP_FLASH_H */
