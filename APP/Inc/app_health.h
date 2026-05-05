#ifndef APP_HEALTH_H
#define APP_HEALTH_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Task IDs — must match the order in App_Health_Init table           */
//给系统里的每个任务分配一个固定编号，用来做任务管理 / 健康监控 / 状态统计。
/* ------------------------------------------------------------------ */
typedef enum
{
    APP_TASK_ID_SENSOR    = 0,
    APP_TASK_ID_ALARM,
    APP_TASK_ID_UPLOAD,
    APP_TASK_ID_CONFIG,
    APP_TASK_ID_CMD,
    APP_TASK_ID_DISPLAY,
    APP_TASK_ID_BACKLIGHT,
    APP_TASK_ID_MONITOR,
    APP_TASK_ID_LOG_STORAGE,
    APP_TASK_ID_ESP32,
    APP_TASK_ID_TOUCH,
    APP_TASK_ID_COUNT //任务总数
} AppTaskId_t;

/* ------------------------------------------------------------------ */
/*  Per-task health record                                             */
//记录每个任务的“健康状态”，也就是判断某个 FreeRTOS 任务有没有正常运行、有没有卡死、多久没上报心跳
/* ------------------------------------------------------------------ */
typedef struct
{
    const char *name;
    uint32_t    last_beat_tick; //上一次任务“报平安”的系统 tick 时间
    uint32_t    timeout_ms; //超时时间，超过这个时间没报心跳就认为异常
    uint8_t     enabled; //是否启用该任务健康检查
    uint8_t     alive; //当前是否存活
    uint32_t    timeout_count; //累计超时次数
} AppTaskHealth_t;

/* ------------------------------------------------------------------ */
/*  Health snapshot (for GET STATUS / UploadTask)                     */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint8_t          healthy; //系统整体是否健康
    uint32_t         fault_mask; //故障位掩码
    uint32_t         free_heap; //当前剩余堆内存大小
    uint32_t         uptime_s; //系统已运行时间，单位是秒
    AppTaskHealth_t  tasks[APP_TASK_ID_COUNT]; //每个任务的健康状态数组
} AppHealthSnapshot_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
void        App_Health_Init(void);
void        App_Health_Beat(AppTaskId_t task_id);
void        App_Health_Check(void);
uint8_t     App_Health_IsSystemHealthy(void);
uint32_t    App_Health_GetFaultMask(void);
const char *App_Health_GetResetReasonString(void);
void        App_Health_PrintResetReason(void);
void        App_Health_PrintStatus(void);
void        App_Health_GetSnapshot(AppHealthSnapshot_t *snapshot);

#endif /* APP_HEALTH_H */
