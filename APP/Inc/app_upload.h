#ifndef APP_UPLOAD_H
#define APP_UPLOAD_H

#include <stdint.h>

//发布对象
typedef enum
{
    UPLOAD_TARGET_NONE = 0,
    UPLOAD_TARGET_USART1,
    UPLOAD_TARGET_MQTT,
    UPLOAD_TARGET_USART1_AND_MQTT
} UploadTarget_t;

typedef enum
{
    UPLOAD_ERR_NONE = 0,
    UPLOAD_ERR_TARGET_NONE,
    UPLOAD_ERR_FORMAT_FAILED,
    UPLOAD_ERR_UART_FAILED,
    UPLOAD_ERR_MQTT_DISABLED,
    UPLOAD_ERR_WIFI_NOT_CONNECTED,
    UPLOAD_ERR_MQTT_NOT_CONNECTED,
    UPLOAD_ERR_MQTT_PUBLISH_FAILED,
    UPLOAD_ERR_PAYLOAD_TOO_LONG,
    UPLOAD_ERR_TOPIC_TOO_LONG,
    UPLOAD_ERR_AT_TIMEOUT,
    UPLOAD_ERR_AT_ERROR
} UploadError_t;

typedef enum
{
    MQTT_MSG_DATA = 0,
    MQTT_MSG_ALARM,
    MQTT_MSG_STATUS,
    MQTT_MSG_RESP,
    MQTT_MSG_HEARTBEAT,
    MQTT_MSG_TEST
} MqttMsgType_t;


typedef struct
{
    UploadTarget_t target;//当前上传目标，例如 UART、MQTT、两者都上传等

    uint32_t tx_count;//总上传次数
    uint32_t tx_fail_count;//总上传失败次数 

    uint32_t uart_tx_count;//UART 上传次数
    uint32_t uart_fail_count;//UART 上传失败次数

    uint32_t mqtt_tx_count;//MQTT 上传次数
    uint32_t mqtt_fail_count;//MQTT 上传失败次数

    uint32_t last_tx_tick;//最近一次上传的系统 tick 
    uint32_t last_uart_tx_tick;//最近一次 UART 上传的系统 tick
    uint32_t last_mqtt_tx_tick;//最近一次 MQTT 上传的系统 tick

    UploadError_t last_error;//最近一次上传错误类型
} UploadStats_t;

void           App_UploadTask(void *arg);
UploadTarget_t App_Upload_GetTarget(void);
void           App_Upload_SetTarget(UploadTarget_t t);
void           App_Upload_GetStats(UploadStats_t *out);
const char    *App_Upload_ErrorToString(UploadError_t err);
void           App_Upload_StripCrLf(char *s);
uint8_t        App_Upload_GetMqttQosForType(MqttMsgType_t type);
uint8_t        App_Upload_GetMqttRetainForType(MqttMsgType_t type);
const char    *App_Upload_MqttMsgTypeToString(MqttMsgType_t type);

#endif /* APP_UPLOAD_H */
