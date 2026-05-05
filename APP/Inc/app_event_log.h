#ifndef APP_EVENT_LOG_H
#define APP_EVENT_LOG_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Capacity / sizing                                                   */
/* ------------------------------------------------------------------ */
#define APP_EVENT_LOG_CAPACITY  64U //事件日志最多保存 64 条
#define APP_EVENT_MSG_LEN       64U //每条事件日志的消息字符串最多 64 字节

/* ------------------------------------------------------------------ */
/*  Event types 系统事件类型                                             */
/* ------------------------------------------------------------------ */
typedef enum
{
    APP_EVENT_NONE = 0,//没有事件

    APP_EVENT_SYSTEM_BOOT,//系统启动
    APP_EVENT_SYSTEM_HEALTH_FAULT,//系统健康检查发现异常
    APP_EVENT_SYSTEM_HEALTH_RECOVERED,//系统健康状态恢复正常

    APP_EVENT_ALARM_ACTIVE,//告警触发
    APP_EVENT_ALARM_CLEARED,//告警解除
    APP_EVENT_ALARM_MUTED,//告警静音

    APP_EVENT_COMM_OFFLINE,//通信离线
    APP_EVENT_COMM_RECOVERED,//通信恢复
    APP_EVENT_COMM_CRC_ERROR,//CRC 校验错误
    APP_EVENT_COMM_PROTO_ERROR,//协议格式错误

    APP_EVENT_SENSOR_DATA_INVALID,//传感器数据无效
    APP_EVENT_SENSOR_DATA_VALID,//传感器数据恢复有效

    APP_EVENT_CONFIG_CHANGED,//配置被修改
    APP_EVENT_CONFIG_SAVED,//配置已保存
    APP_EVENT_CONFIG_RESTORE_DEFAULT,//恢复默认配置

    APP_EVENT_CMD_RECEIVED,//收到命令
    APP_EVENT_CMD_ERROR,//命令错误

    APP_EVENT_ESP32_AT_OK,//ESP32 AT 指令执行成功
    APP_EVENT_ESP32_AT_FAIL,//ESP32 AT 指令执行失败
    APP_EVENT_ESP32_INIT,//ESP32 初始化
    APP_EVENT_ESP32_RESET,//ESP32 复位 
    APP_EVENT_ESP32_WIFI_CONNECTED,//WiFi 连接成功 
    APP_EVENT_ESP32_WIFI_FAILED,//WiFi 连接失败
    APP_EVENT_ESP32_MQTT_CONNECTED,//MQTT 连接成功 
    APP_EVENT_ESP32_MQTT_FAILED,//MQTT 连接失败
    APP_EVENT_ESP32_MQTT_DISCONNECTED,// MQTT 断开连接

    APP_EVENT_TOUCH_IC_DETECTED,//检测到触摸芯片
    APP_EVENT_TOUCH_ERROR,//触摸芯片错误

    APP_EVENT_MQTT_SUB_OK,//MQTT 订阅成功
    APP_EVENT_MQTT_SUB_FAIL,//MQTT 订阅失败
    APP_EVENT_MQTT_CMD_RECEIVED,//收到 MQTT 远程命令
    APP_EVENT_MQTT_CMD_OK,//MQTT 命令处理成功
    APP_EVENT_MQTT_CMD_FAIL,//QTT 命令处理失败
    APP_EVENT_MQTT_AUTH_FAIL,//MQTT 命令鉴权失败

    APP_EVENT_TYPE_COUNT//事件类型总数
} AppEventType_t;

/* ------------------------------------------------------------------ */
/*  Event log entry  事件日志记录                                       */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint32_t       id; //日志编号
    uint32_t       tick; //事件发生时的系统 tick 时间
    AppEventType_t type; //事件类型
    uint8_t        source; //事件来源
    int32_t        value_i; //整数型附加数据
    float          value_f; //浮点型附加数据
    char           message[APP_EVENT_MSG_LEN]; //日志文字说明
} AppEventLogItem_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */
void     App_EventLog_Init(void);

void     App_EventLog_Add(AppEventType_t type,
                          uint8_t        source,
                          int32_t        value_i,
                          float          value_f,
                          const char    *fmt, ...);

uint16_t App_EventLog_GetCount(void);
uint16_t App_EventLog_CopyLatest(AppEventLogItem_t *out, uint16_t max_count);
uint16_t App_EventLog_CopyFiltered(AppEventLogItem_t *out, uint16_t max_count,
                                   const AppEventType_t *types, uint8_t type_count);
void     App_EventLog_Clear(void);

const char *App_EventLog_TypeToString(AppEventType_t type);

/* for UploadTask periodic stat */
uint16_t App_EventLog_GetCapacity(void);

/* persistence hook — not implemented in this phase, reserved for W25Q64 */
void App_EventLog_Persist(const AppEventLogItem_t *item);

#endif /* APP_EVENT_LOG_H */
