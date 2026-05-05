#ifndef BSP_LCD_H
#define BSP_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define LCD_WIDTH   240U
#define LCD_HEIGHT  320U

#define LCD_COLOR_BLACK   0x0000U   //黑色
#define LCD_COLOR_WHITE   0xFFFFU   //白色
#define LCD_COLOR_RED     0xF800U   //红色
#define LCD_COLOR_GREEN   0x07E0U   //绿色
#define LCD_COLOR_BLUE    0x001FU   //蓝色
#define LCD_COLOR_YELLOW  0xFFE0U   //黄色

void BSP_LCD_Init(void);
void BSP_LCD_Reset(void);
void BSP_LCD_BacklightOn(void);
void BSP_LCD_BacklightOff(void);
void BSP_LCD_Select(void);
void BSP_LCD_Unselect(void);

HAL_StatusTypeDef BSP_LCD_WriteCommand(uint8_t cmd);
HAL_StatusTypeDef BSP_LCD_WriteData(const uint8_t *data, uint32_t len);
HAL_StatusTypeDef BSP_LCD_WriteData8(uint8_t data);

void BSP_LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void BSP_LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void BSP_LCD_FillScreen(uint16_t color);
void BSP_LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void BSP_LCD_TestPattern(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LCD_H */
