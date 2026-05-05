#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

typedef enum {
    BSP_RS485_PORT_1 = 0,
    BSP_RS485_PORT_2
} BSP_RS485_Port_t;

void     BSP_RS485_Init(void);
void     BSP_RS485_Send(BSP_RS485_Port_t port, uint8_t *data, uint16_t len);
HAL_StatusTypeDef BSP_RS485_Receive(BSP_RS485_Port_t port, uint8_t *buf, uint16_t len, uint32_t timeout_ms);
void     BSP_RS485_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);

#endif /* BSP_RS485_H */
