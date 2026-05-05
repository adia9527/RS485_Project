#include "app_event_log.h"
#include "app_log_storage.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static AppEventLogItem_t s_log[APP_EVENT_LOG_CAPACITY]; //采用环形缓冲区
static uint16_t          s_head;   /* next write index 下一条日志要写入的位置*/
static uint16_t          s_count; //当前已有多少条有效日志
static uint32_t          s_next_id;
static osMutexId_t       s_mutex;

void App_EventLog_Init(void)
{
    static const osMutexAttr_t attr = { .name = "EventLogMutex" };
    s_mutex   = osMutexNew(&attr);
    s_head    = 0U;
    s_count   = 0U;
    s_next_id = 1U;
    memset(s_log, 0, sizeof(s_log));
}

void App_EventLog_Add(AppEventType_t type, uint8_t source,
                      int32_t value_i, float value_f,
                      const char *fmt, ...)
{
    osMutexAcquire(s_mutex, osWaitForever);

    AppEventLogItem_t *e = &s_log[s_head];
    e->id      = s_next_id++;
    e->tick    = osKernelGetTickCount();
    e->type    = type;
    e->source  = source;
    e->value_i = value_i;
    e->value_f = value_f;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->message, APP_EVENT_MSG_LEN, fmt, ap);
    va_end(ap);

    s_head = (uint16_t)((s_head + 1U) % APP_EVENT_LOG_CAPACITY);
    if (s_count < APP_EVENT_LOG_CAPACITY) { s_count++; }

    App_LogStorage_Enqueue(e);

    osMutexRelease(s_mutex);
}

uint16_t App_EventLog_GetCount(void)
{
    osMutexAcquire(s_mutex, osWaitForever);
    uint16_t n = s_count;
    osMutexRelease(s_mutex);
    return n;
}

uint16_t App_EventLog_GetCapacity(void)
{
    return APP_EVENT_LOG_CAPACITY;
}

//从事件日志环形缓冲区中，复制最近保存的指定数量的日志到 out 数组里。
uint16_t App_EventLog_CopyLatest(AppEventLogItem_t *out, uint16_t max_count)
{
    osMutexAcquire(s_mutex, osWaitForever);

    uint16_t n = (s_count < max_count) ? s_count : max_count;
    /* oldest entry index */
    uint16_t start = (uint16_t)((s_head + APP_EVENT_LOG_CAPACITY - s_count) % APP_EVENT_LOG_CAPACITY);
    for (uint16_t i = 0U; i < n; i++) {
        out[i] = s_log[(start + i) % APP_EVENT_LOG_CAPACITY];
    }

    osMutexRelease(s_mutex);
    return n;
}

//从事件日志缓冲区里，按指定事件类型筛选日志，并复制到 out 数组中。
//max_count： 最多复制多少条事件，防止 out 越界 ； type_count：types 数组里有多少个事件类型
uint16_t App_EventLog_CopyFiltered(AppEventLogItem_t *out, uint16_t max_count,
                                   const AppEventType_t *types, uint8_t type_count)
{
    osMutexAcquire(s_mutex, osWaitForever);

    uint16_t start = (uint16_t)((s_head + APP_EVENT_LOG_CAPACITY - s_count) % APP_EVENT_LOG_CAPACITY);//计算环形缓冲区中最早一条日志的位置
    uint16_t found = 0U;//记录已经复制了多少条
    for (uint16_t i = 0U; i < s_count && found < max_count; i++) {
        const AppEventLogItem_t *e = &s_log[(start + i) % APP_EVENT_LOG_CAPACITY];
        for (uint8_t t = 0U; t < type_count; t++) { //检查当前日志 e 的类型是否属于用户想要的类型，是就复制
            if (e->type == types[t]) {
                out[found++] = *e;
                break;
            }
        }
    }

    osMutexRelease(s_mutex);
    return found;
}

//
void App_EventLog_Clear(void)
{
    osMutexAcquire(s_mutex, osWaitForever);
    s_head  = 0U;
    s_count = 0U;
    memset(s_log, 0, sizeof(s_log));
    osMutexRelease(s_mutex);
}

const char *App_EventLog_TypeToString(AppEventType_t type)
{
    static const char *s_names[] = {
        "NONE",
        "SYS_BOOT", "SYS_FAULT", "SYS_RECOVERED",
        "ALARM_ACTIVE", "ALARM_CLEARED", "ALARM_MUTED",
        "COMM_OFFLINE", "COMM_RECOVERED", "COMM_CRC_ERR", "COMM_PROTO_ERR",
        "SENSOR_INVALID", "SENSOR_VALID",
        "CFG_CHANGED", "CFG_SAVED", "CFG_DEFAULT",
        "CMD_RECEIVED", "CMD_ERROR",
        "ESP32_AT_OK", "ESP32_AT_FAIL", "ESP32_INIT", "ESP32_RESET",
        "ESP32_WIFI_CONNECTED", "ESP32_WIFI_FAILED",
        "ESP32_MQTT_CONNECTED", "ESP32_MQTT_FAILED", "ESP32_MQTT_DISCONNECTED",
        "TOUCH_IC_DETECTED", "TOUCH_ERROR",
        "MQTT_SUB_OK", "MQTT_SUB_FAIL",
        "MQTT_CMD_RECEIVED", "MQTT_CMD_OK", "MQTT_CMD_FAIL", "MQTT_AUTH_FAIL"
    };
    if ((uint32_t)type >= APP_EVENT_TYPE_COUNT) { return "UNKNOWN"; }
    return s_names[(uint32_t)type];
}

void App_EventLog_Persist(const AppEventLogItem_t *item)
{
    (void)item;
}
