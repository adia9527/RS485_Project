#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef enum {
    BSP_TOUCH_IC_UNKNOWN = 0,
    BSP_TOUCH_IC_GT911,
    BSP_TOUCH_IC_FT6336,
    BSP_TOUCH_IC_CST816
} BSP_TouchIc_t;

typedef struct {
    uint8_t  pressed;
    uint16_t x;
    uint16_t y;
    uint8_t  points;//当前检测到的触摸点数量
} BSP_TouchPoint_t;

void              BSP_Touch_Init(void);
void              BSP_Touch_Reset(void);
uint8_t           BSP_Touch_IsIntActive(void);

HAL_StatusTypeDef BSP_Touch_ReadReg(uint16_t reg, uint8_t *buf, uint16_t len);
HAL_StatusTypeDef BSP_Touch_WriteReg(uint16_t reg, const uint8_t *buf, uint16_t len);

BSP_TouchIc_t     BSP_Touch_DetectIc(void);
uint8_t           BSP_Touch_ReadPoint(BSP_TouchPoint_t *point);
const char       *BSP_Touch_IcToString(BSP_TouchIc_t ic);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TOUCH_H */
