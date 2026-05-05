#include "app_upload.h"
#include "app_types.h"
#include "app_config.h"
#include "app_cmd.h"
#include "app_health.h"
#include "app_event_log.h"
#include "app_esp32.h"
#include "protocol_upload.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

#define UPLOAD_PERIOD_DEFAULT_MS  1000U
#define UPLOAD_PERIOD_MIN_MS       200U
#define COMM_STATUS_PERIOD_MS     5000U
#define LOG_STAT_PERIOD_MS       10000U
#define UPLOAD_BUF_SIZE            256U
/* MQTT payload buffer: stripped copy without trailing CRLF */
#define MQTT_PAYLOAD_BUF_SIZE      256U

static UploadTarget_t s_target = UPLOAD_TARGET_USART1;
static UploadStats_t  s_stats;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */
const char *App_Upload_ErrorToString(UploadError_t err)
{
    static const char *s_names[] = {
        "NONE",
        "TARGET_NONE",
        "FORMAT_FAILED",
        "UART_FAILED",
        "MQTT_DISABLED",
        "WIFI_NOT_CONNECTED",
        "MQTT_NOT_CONNECTED",
        "MQTT_PUBLISH_FAILED",
        "PAYLOAD_TOO_LONG",
        "TOPIC_TOO_LONG",
        "AT_TIMEOUT",
        "AT_ERROR"
    };
    if ((uint32_t)err >= (uint32_t)(sizeof(s_names) / sizeof(s_names[0]))) {
        return "UNKNOWN";
    }
    return s_names[(uint32_t)err];
}

//删除字符串末尾的 \r 和 \n 换行符。
void App_Upload_StripCrLf(char *s)
{
    if (s == NULL) { return; }
    uint16_t len = (uint16_t)strlen(s);
    while (len > 0U && (s[len - 1U] == '\r' || s[len - 1U] == '\n')) {
        s[--len] = '\0';
    }
}

UploadTarget_t App_Upload_GetTarget(void)  { return s_target; }
void           App_Upload_SetTarget(UploadTarget_t t) { s_target = t; }
void           App_Upload_GetStats(UploadStats_t *out)
{
    if (out) { *out = s_stats; }
}

static uint8_t App_Upload_SanitizeQos(uint8_t qos, uint8_t fallback)
{
    return (qos <= 1U) ? qos : fallback;
}

static uint8_t App_Upload_SanitizeRetain(uint8_t retain, uint8_t fallback)
{
    return (retain <= 1U) ? retain : fallback;
}

//获取不同上传数据类型的QOS质量
uint8_t App_Upload_GetMqttQosForType(MqttMsgType_t type)
{
    uint8_t qos;

    App_StateLock();
    switch (type) {
        case MQTT_MSG_DATA:
            qos = g_app_state.config.mqtt_qos_data;
            break;
        case MQTT_MSG_ALARM:
            qos = g_app_state.config.mqtt_qos_alarm;
            break;
        case MQTT_MSG_RESP:
            qos = g_app_state.config.mqtt_qos_resp;
            break;
        case MQTT_MSG_STATUS:
        case MQTT_MSG_HEARTBEAT:
        case MQTT_MSG_TEST:
        default:
            qos = g_app_state.config.mqtt_qos_status;
            break;
    }
    App_StateUnlock();

    return App_Upload_SanitizeQos(qos, 0U);
}

//获取不同数据类型是否要做retain处理
uint8_t App_Upload_GetMqttRetainForType(MqttMsgType_t type)
{
    uint8_t retain;

    App_StateLock();
    switch (type) {
        case MQTT_MSG_DATA:
            retain = g_app_state.config.mqtt_retain_data;
            break;
        case MQTT_MSG_ALARM:
            retain = g_app_state.config.mqtt_retain_alarm;
            break;
        case MQTT_MSG_RESP:
            retain = g_app_state.config.mqtt_retain_resp;
            break;
        case MQTT_MSG_STATUS:
        case MQTT_MSG_HEARTBEAT:
        case MQTT_MSG_TEST:
        default:
            retain = g_app_state.config.mqtt_retain_status;
            break;
    }
    App_StateUnlock();

    return App_Upload_SanitizeRetain(retain, 0U);
}

const char *App_Upload_MqttMsgTypeToString(MqttMsgType_t type)
{
    switch (type) {
        case MQTT_MSG_DATA:      return "DATA";
        case MQTT_MSG_ALARM:     return "ALARM";
        case MQTT_MSG_STATUS:    return "STATUS";
        case MQTT_MSG_RESP:      return "RESP";
        case MQTT_MSG_HEARTBEAT: return "HEARTBEAT";
        case MQTT_MSG_TEST:      return "TEST";
        default:                 return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/*  Send helpers                                                       */
/* ------------------------------------------------------------------ */
//通过串口发送数据到上位机
static uint8_t App_Upload_SendToUSART1(const char *payload)
{
    BSP_Log_Printf("%s", payload);
    s_stats.uart_tx_count++;
    s_stats.last_uart_tx_tick = osKernelGetTickCount();
    return 1U;
}

//通过mqtt上传数据
static uint8_t App_Upload_SendToMqtt(const char *topic,
                                     const char *payload,
                                     MqttMsgType_t msg_type)
{
    AppESP32Status_t esp_st;
    App_ESP32_GetStatus(&esp_st);

    if (!esp_st.mqtt_connected) {
        s_stats.mqtt_fail_count++;
        s_stats.last_error = UPLOAD_ERR_MQTT_NOT_CONNECTED;
        return 0U;
    }

    /* Strip CRLF for MQTT payload */
    static char mqtt_buf[MQTT_PAYLOAD_BUF_SIZE];
    strncpy(mqtt_buf, payload, MQTT_PAYLOAD_BUF_SIZE - 1U);
    mqtt_buf[MQTT_PAYLOAD_BUF_SIZE - 1U] = '\0';
    App_Upload_StripCrLf(mqtt_buf);

    uint8_t qos    = App_Upload_GetMqttQosForType(msg_type);
    uint8_t retain = App_Upload_GetMqttRetainForType(msg_type);

    uint8_t ok = App_ESP32_MQTT_Publish(topic, mqtt_buf, qos, retain);
    if (ok) {
        s_stats.mqtt_tx_count++;
        s_stats.last_mqtt_tx_tick = osKernelGetTickCount();
    } else {
        s_stats.mqtt_fail_count++;
        s_stats.last_error = UPLOAD_ERR_MQTT_PUBLISH_FAILED;
        App_EventLog_Add(APP_EVENT_ESP32_MQTT_FAILED, 0U, 0, 0.0f,
                         "publish failed type=%s",
                         App_Upload_MqttMsgTypeToString(msg_type));
    }
    return ok;
}

static void App_Upload_SendPacket(const char *topic,
                                  const char *payload,
                                  MqttMsgType_t msg_type)
{
    uint8_t ok = 1U;
    switch (s_target) {
        case UPLOAD_TARGET_NONE:
            return;
        case UPLOAD_TARGET_USART1:
            ok = App_Upload_SendToUSART1(payload);
            if (!ok) { s_stats.last_error = UPLOAD_ERR_UART_FAILED; }
            break;
        case UPLOAD_TARGET_MQTT:
            ok = App_Upload_SendToMqtt(topic, payload, msg_type);
            break;
        case UPLOAD_TARGET_USART1_AND_MQTT:
            App_Upload_SendToUSART1(payload);
            ok = App_Upload_SendToMqtt(topic, payload, msg_type);
            break;
        default:
            return;
    }
    s_stats.tx_count++;
    if (!ok) { s_stats.tx_fail_count++; }
    s_stats.last_tx_tick = osKernelGetTickCount();
    s_stats.target = s_target;
}

/* ------------------------------------------------------------------ */
/*  Task                                                               */
/* ------------------------------------------------------------------ */
void App_UploadTask(void *arg)
{
    (void)arg;
    uint32_t comm_tick     = 0U;
    uint32_t log_stat_tick = 0U;
    char buf[UPLOAD_BUF_SIZE];

    memset(&s_stats, 0, sizeof(s_stats));
    BSP_Log_Printf("[APP] UploadTask started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_UPLOAD);

        App_StateLock();
        uint32_t period_ms = g_app_state.config.upload_period_ms;
        char topic_data[64];
        char topic_alarm[64];
        char topic_status[64];
        //mqtt上传数据的主题：传感器数据、告警、设备状态
        strncpy(topic_data,   g_app_state.config.mqtt_topic_data,   sizeof(topic_data) - 1U);
        strncpy(topic_alarm,  g_app_state.config.mqtt_topic_alarm,  sizeof(topic_alarm) - 1U);
        strncpy(topic_status, g_app_state.config.mqtt_topic_status, sizeof(topic_status) - 1U);
        topic_data[sizeof(topic_data) - 1U]     = '\0';
        topic_alarm[sizeof(topic_alarm) - 1U]   = '\0';
        topic_status[sizeof(topic_status) - 1U] = '\0';
        AppState_t snap = g_app_state;
        App_StateUnlock();

        if (period_ms < UPLOAD_PERIOD_MIN_MS) { period_ms = UPLOAD_PERIOD_DEFAULT_MS; }

        UploadFormat_t fmt = App_Cmd_GetUploadFormat();
        /* MQTT always uses JSON */
        if (s_target == UPLOAD_TARGET_MQTT ||
            s_target == UPLOAD_TARGET_USART1_AND_MQTT) {
            fmt = UPLOAD_FORMAT_JSON;
        }

        if (s_target != UPLOAD_TARGET_NONE) {
            int n;
            if (fmt == UPLOAD_FORMAT_JSON) {
                n = Protocol_Upload_FormatDataJson(buf, UPLOAD_BUF_SIZE, &snap);
            } else {
                n = Protocol_Upload_FormatDataText(buf, UPLOAD_BUF_SIZE, &snap);
            }
            if (n > 0 && (uint16_t)n < UPLOAD_BUF_SIZE) {
                App_Upload_SendPacket(topic_data, buf, MQTT_MSG_DATA);
            } else {
                s_stats.tx_fail_count++;
                s_stats.last_error = UPLOAD_ERR_FORMAT_FAILED;
            }
        }

        uint32_t now = osKernelGetTickCount();
        if ((now - comm_tick) >= COMM_STATUS_PERIOD_MS) {
            comm_tick = now;

            if (s_target != UPLOAD_TARGET_NONE) {
                int n;
                if (fmt == UPLOAD_FORMAT_JSON) {
                    n = Protocol_Upload_FormatCommJson(buf, UPLOAD_BUF_SIZE, &snap);
                } else {
                    n = Protocol_Upload_FormatCommText(buf, UPLOAD_BUF_SIZE, &snap);
                }
                if (n > 0 && (uint16_t)n < UPLOAD_BUF_SIZE) {
                    App_Upload_SendPacket(topic_status, buf, MQTT_MSG_STATUS);
                }

                if (fmt == UPLOAD_FORMAT_JSON) {
                    n = Protocol_Upload_FormatAlarmJson(buf, UPLOAD_BUF_SIZE, &snap);
                } else {
                    n = Protocol_Upload_FormatAlarmText(buf, UPLOAD_BUF_SIZE, &snap);
                }
                if (n > 0 && (uint16_t)n < UPLOAD_BUF_SIZE) {
                    App_Upload_SendPacket(topic_alarm, buf, MQTT_MSG_ALARM);
                }
            }
        }

        if ((now - log_stat_tick) >= LOG_STAT_PERIOD_MS) {
            log_stat_tick = now;
            if (s_target != UPLOAD_TARGET_NONE) {
                uint16_t log_cnt = App_EventLog_GetCount();
                uint16_t log_cap = App_EventLog_GetCapacity();
                if (fmt == UPLOAD_FORMAT_JSON) {
                    snprintf(buf, UPLOAD_BUF_SIZE,
                        "{\"type\":\"log_stat\",\"count\":%u,\"capacity\":%u}\r\n",
                        (unsigned)log_cnt, (unsigned)log_cap);
                } else {
                    snprintf(buf, UPLOAD_BUF_SIZE,
                        "[LOG_STAT] count=%u capacity=%u\r\n",
                        (unsigned)log_cnt, (unsigned)log_cap);
                }
                App_Upload_SendPacket(topic_status, buf, MQTT_MSG_STATUS);
            }
        }

        osDelay(period_ms);
    }
}
