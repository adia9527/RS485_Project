#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

#define BSP_ADC_CHANNELS 4
#define BSP_ADC_VREF     3.3f

void BSP_ADC_Init(void);
void BSP_ADC_Start(void);
void BSP_ADC_Update(void);
uint16_t BSP_ADC_GetRaw(uint8_t ch);
float BSP_ADC_GetVoltage(uint8_t ch);

#endif /* BSP_ADC_H */
