#include "app_health.h"
#include "app_event_log.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Static health table                                                */
/* ------------------------------------------------------------------ */
static AppTaskHealth_t s_tasks[APP_TASK_ID_COUNT] = {
    { "Sensor",     0U, 5000U,  1U, 1U, 0U },
    { "Alarm",      0U, 3000U,  1U, 1U, 0U },
    { "Upload",     0U, 5000U,  1U, 1U, 0U },
    { "Config",     0U, 5000U,  1U, 1U, 0U },
    { "Cmd",        0U, 10000U, 1U, 1U, 0U },
    { "Display",    0U, 5000U,  1U, 1U, 0U },
    { "Backlight",  0U, 5000U,  1U, 1U, 0U },
    { "Monitor",    0U, 3000U,  1U, 1U, 0U },
    { "LogStorage", 0U, 10000U, 1U, 1U, 0U },
    { "Esp32",      0U, 15000U, 1U, 1U, 0U },
    { "Touch",      0U, 3000U,  1U, 1U, 0U },
};

static uint32_t s_fault_mask    = 0U; //故障位掩码变量，记录“哪些任务当前处于异常/超时状态”，0～10bit分别对应上面的每个任务，超时则设置对应bit=1
static char     s_reset_reason[32];

/* ------------------------------------------------------------------ */
/*  Reset reason detection                                             */
/* ------------------------------------------------------------------ */
void App_Health_Init(void)
{
    /* Read RCC reset flags before clearing */
    uint32_t csr = RCC->CSR;//读取复位标志寄存器

    //判断复位原因
    if      (csr & RCC_CSR_IWDGRSTF)  { strncpy(s_reset_reason, "IWDG",     sizeof(s_reset_reason) - 1U); }
    else if (csr & RCC_CSR_WWDGRSTF)  { strncpy(s_reset_reason, "WWDG",     sizeof(s_reset_reason) - 1U); }
    else if (csr & RCC_CSR_SFTRSTF)   { strncpy(s_reset_reason, "Software", sizeof(s_reset_reason) - 1U); }
    else if (csr & RCC_CSR_PORRSTF)   { strncpy(s_reset_reason, "POR/PDR",  sizeof(s_reset_reason) - 1U); }
    else if (csr & RCC_CSR_PINRSTF)   { strncpy(s_reset_reason, "PIN",      sizeof(s_reset_reason) - 1U); }
    else if (csr & RCC_CSR_BORRSTF)   { strncpy(s_reset_reason, "BOR",      sizeof(s_reset_reason) - 1U); }
    else if (csr & RCC_CSR_LPWRRSTF)  { strncpy(s_reset_reason, "LowPower", sizeof(s_reset_reason) - 1U); }
    else                               { strncpy(s_reset_reason, "Unknown",  sizeof(s_reset_reason) - 1U); }

    //手动补字符串结束符
    s_reset_reason[sizeof(s_reset_reason) - 1U] = '\0';

    //清除复位标志
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* Seed last_beat_tick so tasks aren't immediately flagged dead */
    //初始化所有任务的心跳时间
    uint32_t now = osKernelGetTickCount();
    for (uint8_t i = 0U; i < APP_TASK_ID_COUNT; i++) {
        s_tasks[i].last_beat_tick = now;
    }
}

/* ------------------------------------------------------------------ */
/*  Heartbeat — called by each task in its main loop                  */
/* ------------------------------------------------------------------ */
void App_Health_Beat(AppTaskId_t task_id)
{
    if ((uint8_t)task_id >= APP_TASK_ID_COUNT) { return; } //判断当前任务ID是否位于预定的任务列表里
    taskENTER_CRITICAL();
    s_tasks[task_id].last_beat_tick = osKernelGetTickCount(); //初始化最近心跳时间为现在
    taskEXIT_CRITICAL();
}

/* ------------------------------------------------------------------ */
/*  Health check — called by MonitorTask                              */
//检查所有启动了健康检查的任务是否处于存活状态
/* ------------------------------------------------------------------ */
void App_Health_Check(void)
{
    uint32_t now = osKernelGetTickCount();

    for (uint8_t i = 0U; i < APP_TASK_ID_COUNT; i++) {
        AppTaskHealth_t *t = &s_tasks[i];
        if (!t->enabled) { continue; }

        uint32_t elapsed;
        taskENTER_CRITICAL();
        elapsed = now - t->last_beat_tick;
        taskEXIT_CRITICAL();

        uint8_t timed_out = (elapsed >= t->timeout_ms) ? 1U : 0U; //检查该任务是否按时上报心跳

        if (timed_out && t->alive) { //任务超时且处于存活状态
            t->alive = 0U; //更改为离线
            t->timeout_count++; //累计超时次数加1
            taskENTER_CRITICAL();
            s_fault_mask |= (1UL << i);
            taskEXIT_CRITICAL();
            BSP_Log_Printf("[HEALTH] %s timeout\r\n", t->name);
            App_EventLog_Add(APP_EVENT_SYSTEM_HEALTH_FAULT, (uint8_t)i,
                (int32_t)t->timeout_count, 0.0f, "%s timeout", t->name);
        } else if (!timed_out && !t->alive) { //任务没超时但是处于离线状态
            t->alive = 1U;//更改为存活
            taskENTER_CRITICAL();
            s_fault_mask &= ~(1UL << i);
            taskEXIT_CRITICAL();
            BSP_Log_Printf("[HEALTH] %s recovered\r\n", t->name);
            App_EventLog_Add(APP_EVENT_SYSTEM_HEALTH_RECOVERED, (uint8_t)i,
                0, 0.0f, "%s recovered", t->name);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Accessors                                                          */
//检查所有任务是否都处于存活状态
/* ------------------------------------------------------------------ */
uint8_t App_Health_IsSystemHealthy(void)
{
    return (s_fault_mask == 0U) ? 1U : 0U;
}

uint32_t App_Health_GetFaultMask(void)
{
    return s_fault_mask;
}

const char *App_Health_GetResetReasonString(void)
{
    return s_reset_reason;
}

void App_Health_PrintResetReason(void)
{
    BSP_Log_Printf("[SYS] reset reason: %s\r\n", s_reset_reason);
}

void App_Health_PrintStatus(void)
{
    uint32_t uptime_s = osKernelGetTickCount() / 1000U;
    size_t   heap     = xPortGetFreeHeapSize(); //获取当前可申请的空闲内存大小
    uint8_t  healthy  = App_Health_IsSystemHealthy();

    BSP_Log_Printf("[SYS] uptime=%lus heap=%u healthy=%u fault=0x%08lX\r\n",
        uptime_s, (unsigned)heap, healthy, s_fault_mask);
}

//获取系统健康状态快照
void App_Health_GetSnapshot(AppHealthSnapshot_t *snapshot)
{
    if (!snapshot) { return; }
    snapshot->healthy   = App_Health_IsSystemHealthy();
    snapshot->fault_mask = s_fault_mask;
    snapshot->free_heap  = (uint32_t)xPortGetFreeHeapSize();
    snapshot->uptime_s   = osKernelGetTickCount() / 1000U;
    taskENTER_CRITICAL();
    for (uint8_t i = 0U; i < APP_TASK_ID_COUNT; i++) {
        snapshot->tasks[i] = s_tasks[i];
    }
    taskEXIT_CRITICAL();
}
