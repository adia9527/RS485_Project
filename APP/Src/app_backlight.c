#include "app_backlight.h"
#include "app_types.h"
#include "app_health.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include "main.h"

#define BACKLIGHT_TASK_PERIOD_MS   200U
#define BACKLIGHT_TIMEOUT_DEFAULT  30000U
#define BACKLIGHT_TIMEOUT_MIN       5000U

static volatile uint8_t  s_bl_on         = 0U;
static volatile uint32_t s_last_tick     = 0U;
static volatile uint32_t s_timeout_ms    = BACKLIGHT_TIMEOUT_DEFAULT;

void App_Backlight_Init(void)
{
    s_last_tick  = osKernelGetTickCount();
    s_timeout_ms = BACKLIGHT_TIMEOUT_DEFAULT;
    App_Backlight_On();
}

void App_Backlight_On(void)
{
    if (!s_bl_on) {
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
        s_bl_on = 1U;
        BSP_Log_Printf("[BL] on\r\n");
    }
}

void App_Backlight_Off(void)
{
    if (s_bl_on) {
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);
        s_bl_on = 0U;
        BSP_Log_Printf("[BL] off timeout\r\n");
    }
}

void App_Backlight_Wakeup(void)
{
    s_last_tick = osKernelGetTickCount();
    if (!s_bl_on) {
        App_Backlight_On();
        BSP_Log_Printf("[BL] wakeup\r\n");
    }
}

void App_Backlight_SetTimeout(uint32_t timeout_ms)
{
    s_timeout_ms = timeout_ms;
}

void App_Backlight_OnUserActivity(void)
{
    App_Backlight_Wakeup();
}

void App_BacklightTask(void *argument)
{
    (void)argument;
    BSP_Log_Printf("[APP] BacklightTask started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_BACKLIGHT);
        App_StateLock();
        uint32_t cfg_timeout = g_app_state.config.backlight_timeout_ms;
        App_StateUnlock();

        if (cfg_timeout < BACKLIGHT_TIMEOUT_MIN) {
            cfg_timeout = BACKLIGHT_TIMEOUT_DEFAULT;
        }
        s_timeout_ms = cfg_timeout;

        if (s_bl_on) {
            uint32_t now  = osKernelGetTickCount();
            uint32_t idle = now - s_last_tick;
            if (idle >= s_timeout_ms) {
                App_Backlight_Off();
            }
        }

        osDelay(BACKLIGHT_TASK_PERIOD_MS);
    }
}
