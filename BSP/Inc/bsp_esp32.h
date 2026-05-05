#ifndef BSP_ESP32_H
#define BSP_ESP32_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define BSP_ESP32_RX_BUF_SIZE  256U

void              BSP_ESP32_Init(void);
void              BSP_ESP32_Enable(void);
void              BSP_ESP32_Disable(void);
void              BSP_ESP32_Reset(void);
void              BSP_ESP32_SetBootNormal(void);
void              BSP_ESP32_SetBootDownload(void);

HAL_StatusTypeDef BSP_ESP32_SendRaw(const uint8_t *data, uint16_t len, uint32_t timeout_ms);
HAL_StatusTypeDef BSP_ESP32_SendString(const char *str, uint32_t timeout_ms);
HAL_StatusTypeDef BSP_ESP32_Receive(uint8_t *buf, uint16_t buf_size,
                                    uint16_t *rx_len, uint32_t timeout_ms);

void     BSP_ESP32_UartRxCallback(void);
uint8_t  BSP_ESP32_ReadByte(uint8_t *b);
uint16_t BSP_ESP32_RxAvailable(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ESP32_H */
