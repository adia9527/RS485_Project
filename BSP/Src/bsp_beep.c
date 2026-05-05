#include "bsp_beep.h"
#include "main.h"
#include "stm32f4xx_hal.h"

void BSP_Beep_Init(void)
{
    BSP_Beep_Off();
}

void BSP_Beep_On(void)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
}

void BSP_Beep_Off(void)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
}

void BSP_Beep_Toggle(void)
{
    HAL_GPIO_TogglePin(BEEP_GPIO_Port, BEEP_Pin);
}
