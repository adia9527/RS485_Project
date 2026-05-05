#include "app_touch.h"
#include "app_backlight.h"
#include "app_display.h"
#include "app_health.h"
#include "app_event_log.h"
#include "bsp_log.h"
#include "cmsis_os.h"

#define TOUCH_TASK_PERIOD_MS  50U

static BSP_TouchPoint_t s_last_point = { 0U, 0U, 0U, 0U };

void App_Touch_Init(void)
{
    BSP_Touch_Init();
    BSP_TouchIc_t ic = BSP_Touch_DetectIc();
    BSP_Log_Printf("[TOUCH] ic=%s\r\n", BSP_Touch_IcToString(ic));
    if (ic != BSP_TOUCH_IC_UNKNOWN) {
        App_EventLog_Add(APP_EVENT_TOUCH_IC_DETECTED, (uint8_t)ic, 0, 0.0f,
                         "touch ic=%s", BSP_Touch_IcToString(ic));
    }
}

uint8_t App_Touch_GetLastPoint(BSP_TouchPoint_t *point)
{
    if (point == NULL) { return 0U; }
    *point = s_last_point;
    return s_last_point.pressed;
}

BSP_TouchIc_t App_Touch_GetIcType(void)
{
    return BSP_Touch_DetectIc();
}

void App_TouchTask(void *argument)
{
    (void)argument;
    BSP_Log_Printf("[APP] TouchTask started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_TOUCH);

        BSP_TouchPoint_t pt;
        if (BSP_Touch_ReadPoint(&pt)) {
            s_last_point = pt;
            App_Backlight_Wakeup();
            App_Display_OnTouchPoint(pt.x, pt.y);
        } else {
            s_last_point.pressed = 0U;
        }

        osDelay(TOUCH_TASK_PERIOD_MS);
    }
}
