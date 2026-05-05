#include "bsp_led.h"
#include "main.h"
#include "stm32f4xx_hal.h"

static const struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} s_led_map[3] = {
    { LED1_GPIO_Port, LED1_Pin },
    { LED2_GPIO_Port, LED2_Pin },
    { LED3_GPIO_Port, LED3_Pin }
};

void BSP_LED_Init(void)
{
    BSP_LED_Off(BSP_LED_1);
    BSP_LED_Off(BSP_LED_2);
    BSP_LED_Off(BSP_LED_3);
}

void BSP_LED_On(BSP_LED_t led)
{
    if ((uint8_t)led >= 3U) return;
    HAL_GPIO_WritePin(s_led_map[led].port, s_led_map[led].pin, GPIO_PIN_SET);
}

void BSP_LED_Off(BSP_LED_t led)
{
    if ((uint8_t)led >= 3U) return;
    HAL_GPIO_WritePin(s_led_map[led].port, s_led_map[led].pin, GPIO_PIN_RESET);
}

void BSP_LED_Toggle(BSP_LED_t led)
{
    if ((uint8_t)led >= 3U) return;
    HAL_GPIO_TogglePin(s_led_map[led].port, s_led_map[led].pin);
}
