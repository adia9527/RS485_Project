#include "bsp_adc.h"
#include "adc.h"
#include "stm32f4xx_hal.h"

/* Injected channel ranks map to PA3/PA4/PA5/PA6 (CH3-CH6) */
static uint32_t s_inj_rank[BSP_ADC_CHANNELS] = {
    ADC_INJECTED_RANK_1,
    ADC_INJECTED_RANK_2,
    ADC_INJECTED_RANK_3,
    ADC_INJECTED_RANK_4
};

static uint16_t s_raw[BSP_ADC_CHANNELS];

void BSP_ADC_Init(void)
{
    for (int i = 0; i < BSP_ADC_CHANNELS; i++) s_raw[i] = 0U;
}

void BSP_ADC_Start(void)
{
    HAL_ADCEx_InjectedStart(&hadc1);
}

//采用指数滑动滤波
void BSP_ADC_Update(void)
{
    HAL_ADCEx_InjectedStart(&hadc1); //注入通道转换开始
    if (HAL_ADCEx_InjectedPollForConversion(&hadc1, 10U) == HAL_OK) { //等待注入通道转换完成
        for (int i = 0; i < BSP_ADC_CHANNELS; i++) {
            uint16_t val = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, s_inj_rank[i]); //读取转换结果
            /* simple EMA: filtered = (3*filtered + new) / 4 */
            s_raw[i] = (uint16_t)((3U * s_raw[i] + val) >> 2);
        }
    }
}

uint16_t BSP_ADC_GetRaw(uint8_t ch)
{
    if (ch >= BSP_ADC_CHANNELS) return 0U;
    return s_raw[ch];
}

//根据 ADC 原始采样值，计算对应通道的电压值。
float BSP_ADC_GetVoltage(uint8_t ch)
{
    if (ch >= BSP_ADC_CHANNELS) return 0.0f;
    return (s_raw[ch] * BSP_ADC_VREF) / 4095.0f;
}
