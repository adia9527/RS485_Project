#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdint.h>

typedef enum {
    BSP_LED_1 = 0,
    BSP_LED_2,
    BSP_LED_3
} BSP_LED_t;

void BSP_LED_Init(void);
void BSP_LED_On(BSP_LED_t led);
void BSP_LED_Off(BSP_LED_t led);
void BSP_LED_Toggle(BSP_LED_t led);

#endif /* BSP_LED_H */
