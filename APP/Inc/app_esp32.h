#ifndef APP_ESP32_H
#define APP_ESP32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


typedef enum
{
    ESP32_APP_STATE_DISABLED = 0,//ESP32 功能被禁用
    ESP32_APP_STATE_IDLE,//空闲状态，等待连接触发或配置
    ESP32_APP_STATE_AT_CHECK,//检查 ESP32 AT 指令是否正常
    ESP32_APP_STATE_WIFI_CONNECTING,//正在连接 WiFi
    ESP32_APP_STATE_WIFI_CONNECTED,//WiFi 已连接
    ESP32_APP_STATE_MQTT_CONFIGURING,//正在配置 MQTT 参数
    ESP32_APP_STATE_MQTT_CONNECTING,//正在连接 MQTT 服务器 
    ESP32_APP_STATE_MQTT_SUBSCRIBING,//正在订阅 MQTT 命令主题
    ESP32_APP_STATE_MQTT_CONNECTED,//MQTT 已连接并准备正常工作
    ESP32_APP_STATE_ERROR//出错状态，等待重试或恢复 
} AppESP32State_t;

//ESP32 通信模块状态结构体
typedef struct
{
    AppESP32State_t app_state;//ESP32 应用层状态机当前状态
    uint8_t         at_ok;//AT 指令通信是否正常
    uint8_t         wifi_connected;//WiFi 是否已连接
    uint8_t         mqtt_connected;//MQTT 是否已连接
    uint8_t         mqtt_subscribed;//MQTT 命令主题是否已订阅
    uint32_t        last_ok_tick;//最近一次成功通信的系统 tick
    uint32_t        last_error_tick;//最近一次发生错误的系统 tick
    uint32_t        connect_attempts;//总连接尝试次数
    uint32_t        wifi_fail_count;//WiFi 连接失败次数
    uint32_t        mqtt_fail_count;//MQTT 连接失败次数
    uint32_t        publish_fail_count;//MQTT 发布失败次数
} AppESP32Status_t;

void    App_ESP32_Init(void);
void    App_ESP32Task(void *argument);

uint8_t App_ESP32_AT_Test(void);
uint8_t App_ESP32_GetVersion(void);
uint8_t App_ESP32_ResetModule(void);
uint8_t App_ESP32_SendATCommand(const char *cmd, const char *expect,
                                char *resp_buf, uint16_t resp_buf_size,
                                uint32_t timeout_ms);

uint8_t App_ESP32_SetWiFiModeStation(void);
uint8_t App_ESP32_JoinWiFi(const char *ssid, const char *password);
uint8_t App_ESP32_MQTT_ConfigUser(const char *client_id,
                                  const char *username,
                                  const char *password);
uint8_t App_ESP32_MQTT_Connect(const char *host, uint16_t port);
uint8_t App_ESP32_MQTT_Disconnect(void);
uint8_t App_ESP32_MQTT_Publish(const char *topic, const char *payload,
                               uint8_t qos, uint8_t retain);
uint8_t App_ESP32_MQTT_CheckAlive(void);
uint8_t App_ESP32_MQTT_Subscribe(const char *topic, uint8_t qos);
uint8_t App_ESP32_MQTT_Unsubscribe(const char *topic);
uint8_t App_ESP32_MQTT_IsSubscribed(void);
void    App_ESP32_PollUnsolicited(void);
uint8_t App_ESP32_EscapeATString(const char *in, char *out, uint16_t out_size);

AppESP32State_t  App_ESP32_GetNetState(void);
void             App_ESP32_TriggerConnect(void);
void             App_ESP32_TriggerDisconnect(void);
void             App_ESP32_GetStatus(AppESP32Status_t *status);
const char      *App_ESP32_StateToString(AppESP32State_t state);

#ifdef __cplusplus
}
#endif

#endif /* APP_ESP32_H */
