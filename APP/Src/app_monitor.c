#include "app_monitor.h"
#include "app_types.h"
#include "app_display.h"
#include "app_backlight.h"
#include "app_alarm.h"
#include "app_health.h"
#include "bsp_led.h"
#include "bsp_key.h"
#include "bsp_adc.h"
#include "bsp_log.h"
#include "bsp_watchdog.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"

#define MONITOR_TICK_MS       10U //基准节拍，Monitor 任务每 10ms 执行一次
#define HEARTBEAT_TICKS       (500U  / MONITOR_TICK_MS) //每 50 次 Monitor 循环，执行一次心跳相关操作
#define LOG_PERIOD_TICKS      (5000U / MONITOR_TICK_MS) //日志输出周期
#define ADC_PERIOD_TICKS      (500U  / MONITOR_TICK_MS) //ADC 采样周期
#define HEALTH_CHECK_TICKS    (1000U / MONITOR_TICK_MS) //健康检查周期
#define HEALTH_PRINT_TICKS    (5000U / MONITOR_TICK_MS) //健康状态打印周期

static const char *s_key_names[BSP_KEY_COUNT] = { "KEY1", "KEY2", "KEY3" };
static const char *s_evt_names[] = { "", "PRESS", "LONG_PRESS", "RELEASE" };

void App_MonitorTask(void *arg)
{
    (void)arg;
    uint32_t hb_cnt     = 0U; //心跳计数器
    uint32_t log_cnt    = 0U; //日志计数器
    uint32_t adc_cnt    = 0U; //ADC 采样计数器
    uint32_t health_cnt = 0U; //健康检查计数器
    uint32_t hprint_cnt = 0U; //健康状态打印计数器

    BSP_Log_Printf("[SYS] MonitorTask started\r\n");

    for (;;) {
        /* own heartbeat */
        //更新一次心跳
        App_Health_Beat(APP_TASK_ID_MONITOR);

        /* heartbeat LED1 */
        if (++hb_cnt >= HEARTBEAT_TICKS) {
            hb_cnt = 0U;
            BSP_LED_Toggle(BSP_LED_1);
        }

        /* key scan */
        BSP_KeyInfo_t ki = BSP_Key_Scan();
        if (ki.event != BSP_KEY_EVENT_NONE) {
            BSP_Log_Printf("[KEY] %s %s\r\n",
                s_key_names[ki.key], s_evt_names[ki.event]);

            if (ki.event == BSP_KEY_EVENT_PRESS) {
                App_Backlight_Wakeup(); //按下按键（松开后）LCD亮
                if (ki.key == BSP_KEY_1) {
                    App_Display_RequestPageNext(); //切换LCD下一页
                } else if (ki.key == BSP_KEY_2) {
                    App_Display_RequestPagePrev();
                } else if (ki.key == BSP_KEY_3) {
                    App_Alarm_Mute(); //开启警报功能
                }
            }
        }

        /* ADC update */
        //每隔50次循环更新一次ADC采样值
        if (++adc_cnt >= ADC_PERIOD_TICKS) {
            adc_cnt = 0U;
            BSP_ADC_Update();
        }

        /* health check (1s) */
        //每隔一段时间检查所有任务是否处于存活状态，若都存活，则喂一次狗
        if (++health_cnt >= HEALTH_CHECK_TICKS) {
            health_cnt = 0U;
            App_Health_Check();
            /* Feed watchdog only when system is healthy */
            if (App_Health_IsSystemHealthy()) {
                BSP_Watchdog_Feed();
            }
        }

        /* periodic system log + health summary (5s) */
        //每隔一段时间打印ADC采样值
        if (++log_cnt >= LOG_PERIOD_TICKS) {
            log_cnt = 0U;
            for (uint8_t ch = 0U; ch < BSP_ADC_CHANNELS; ch++) {
                BSP_Log_Printf("[ADC] ch%u raw=%u vol=%.3fV\r\n",
                    ch, BSP_ADC_GetRaw(ch), (double)BSP_ADC_GetVoltage(ch));
            }
        }

        //每隔一段时间打印系统状态：当前运行tick，空闲内存大小，所有任务是否存活，任务存活掩码
        if (++hprint_cnt >= HEALTH_PRINT_TICKS) {
            hprint_cnt = 0U;
            App_Health_PrintStatus();
        }

        osDelay(MONITOR_TICK_MS);
    }
}
