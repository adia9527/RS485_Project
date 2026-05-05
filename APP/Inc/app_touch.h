#ifndef APP_TOUCH_H
#define APP_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp_touch.h"

void          App_Touch_Init(void);
void          App_TouchTask(void *argument);
uint8_t       App_Touch_GetLastPoint(BSP_TouchPoint_t *point);
BSP_TouchIc_t App_Touch_GetIcType(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TOUCH_H */
