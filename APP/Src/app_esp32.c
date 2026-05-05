#include "app_esp32.h"
#include "bsp_esp32.h"
#include "app_health.h"
#include "app_event_log.h"
#include "app_types.h"
#include "app_config.h"
#include "app_upload.h"
#include "app_mqtt_cmd.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

#define ESP32_TASK_PERIOD_MS      1000U//ESP32 任务循环周期，1 秒执行一次状态机检查
#define ESP32_RESP_BUF_SIZE        256U//ESP32 AT 响应接收缓冲区大小 
#define ESP32_AT_CMD_BUF_SIZE      384U//ESP32 AT 指令发送缓冲区大小 
#define ESP32_ERROR_RETRY_MS     10000U//出错后等待 10 秒再重试 
#define ESP32_WIFI_TIMEOUT_MS    12000U//WiFi 连接超时时间，12 秒 
#define ESP32_MQTT_TIMEOUT_MS     6000U//MQTT 操作超时时间，6 秒
#define ESP32_HEARTBEAT_PERIOD_MS 30000U//心跳发送周期，30 秒 

static AppESP32State_t  s_net_state    = ESP32_APP_STATE_DISABLED; //ESP32通信状态
static uint32_t         s_error_tick   = 0U;//错误处理计时点
static uint8_t          s_trigger_conn = 0U;//连接触发标志
static uint8_t          s_trigger_disconn = 0U;//断开连接触发标志
static uint32_t         s_hb_tick      = 0U;//心跳计时点
static AppESP32Status_t s_status;

/* ------------------------------------------------------------------ */
/*  State string                                                       */
/* ------------------------------------------------------------------ */
const char *App_ESP32_StateToString(AppESP32State_t state)
{
    static const char *s_names[] = {
        "DISABLED", "IDLE", "AT_CHECK",
        "WIFI_CONNECTING", "WIFI_CONNECTED",
        "MQTT_CONFIGURING", "MQTT_CONNECTING",
        "MQTT_SUBSCRIBING", "MQTT_CONNECTED", "ERROR"
    };
    uint32_t idx = (uint32_t)state;
    if (idx >= (uint32_t)(sizeof(s_names) / sizeof(s_names[0]))) { return "UNKNOWN"; }
    return s_names[idx];
}

/* ------------------------------------------------------------------ */
/*  Status accessor                                                    */
/* ------------------------------------------------------------------ */
void App_ESP32_GetStatus(AppESP32Status_t *status)
{
    if (status) { *status = s_status; }
}

/* ------------------------------------------------------------------ */
/*  AT string escape: " -> \" and \ -> \\                              */
//把字符串转换成适合放进 ESP32 AT 指令里的安全字符串，主要将符号：" 和 / 换成转义字符
/* ------------------------------------------------------------------ */
uint8_t App_ESP32_EscapeATString(const char *in, char *out, uint16_t out_size)
{
    if (in == NULL || out == NULL || out_size == 0U) { return 0U; }
    uint16_t wi = 0U;
    while (*in != '\0') {
        if (*in == '\\' || *in == '"') {
            if (wi + 2U >= out_size) { out[wi] = '\0'; return 0U; }
            out[wi++] = '\\';
            out[wi++] = *in;
        } else {
            if (wi + 1U >= out_size) { out[wi] = '\0'; return 0U; }
            out[wi++] = *in;
        }
        in++;
    }
    out[wi] = '\0';
    return 1U;
}

/* ------------------------------------------------------------------ */
/*  Unsolicited line handling                                          */
/* ------------------------------------------------------------------ */
static void App_ESP32_ClearLinkStatus(void)
{
    s_status.wifi_connected  = 0U;
    s_status.mqtt_connected  = 0U;
    s_status.mqtt_subscribed = 0U;
}

//标记ESP32状态错误
static void App_ESP32_MarkError(void)
{
    s_status.last_error_tick = osKernelGetTickCount();
    s_error_tick             = s_status.last_error_tick;
    s_net_state              = ESP32_APP_STATE_ERROR;
    s_status.app_state       = s_net_state;
}

/*处理 ESP32 主动发过来的状态提示行，也就是 unsolicited line（所谓 UnsolicitedLine，可以理解成 ESP32 没等 STM32 提问，就自己突然冒出来的一句话。
比如 WiFi 断开、MQTT 断开、收到订阅消息等）*/
static void App_ESP32_HandleUnsolicitedLine(const char *line)
{
    if (line == NULL || line[0] == '\0') { return; }

    if (strncmp(line, "+MQTTSUBRECV:", 13) == 0) { //+MQTTSUBRECV: 一般表示 ESP32 收到了 MQTT 订阅主题的数据
        App_MqttCmd_HandleRaw(line);
        return;
    }

    if (strncmp(line, "+MQTTDISCONNECTED:", 18) == 0) { //判断是否收到 MQTT 断开事件
        if (s_status.mqtt_connected || s_status.mqtt_subscribed) { //只有之前认为 MQTT 是连接状态，或者已经订阅过主题时，才记录一次断开事件
            App_EventLog_Add(APP_EVENT_ESP32_MQTT_DISCONNECTED, 0U, 0, 0.0f,
                             "MQTT disconnected");
            BSP_Log_Printf("[ESP32] MQTT disconnected\r\n");
        }
        s_status.mqtt_connected  = 0U;
        s_status.mqtt_subscribed = 0U;
        App_ESP32_MarkError();
        return;
    }

    if (strncmp(line, "+MQTTCONNECTED:", 15) == 0) {//判断是否收到 MQTT 连接成功事件
        s_status.mqtt_connected = 1U;
        s_status.last_ok_tick   = osKernelGetTickCount();
        return;
    }

    if (strcmp(line, "WIFI DISCONNECT") == 0) { //处理 WiFi 断开
        App_ESP32_ClearLinkStatus();
        App_ESP32_MarkError();
        return;
    }

    if (strcmp(line, "WIFI CONNECTED") == 0 || strcmp(line, "WIFI GOT IP") == 0) { //处理 WiFi 连接成功 / 获取 IP
        s_status.wifi_connected = 1U;
        s_status.last_ok_tick   = osKernelGetTickCount();
    }
}

//处理MQTT回复文本：
/*
    WIFI CONNECTED\r\n
    WIFI GOT IP\r\n
    +MQTTCONNECTED:0,1,"broker.xxx.com","1883","",1\r\n
    +MQTTSUBRECV:0,"device/001/cmd",25,TOKEN abc123 PUMP ON\r\n
*/
static void App_ESP32_ProcessRxText(const char *text)
{
    char     line[256];
    uint16_t li = 0U; //行缓冲区的写入位置，也可以理解成当前这一行已经收了多少个字符

    if (text == NULL) { return; }

    //遍历整段文本，当遇到 \r 或 \n 时，如果当前行（line）里已经有内容，就补字符串结束符'/0'，并把当前行交给消息处理函数，然后清空当前行并准备接收下一行内容
    while (*text != '\0') {
        char c = *text++;
        if (c == '\r' || c == '\n') {
            if (li > 0U) {
                line[li] = '\0';
                App_ESP32_HandleUnsolicitedLine(line);
                li = 0U;
            }
        } else if (li < (uint16_t)(sizeof(line) - 1U)) {
            line[li++] = c;
        }
    }

    //处理最后一行没有换行的情况，有时候接收到的文本最后可能没有 \r\n 结尾
    if (li > 0U) {
        line[li] = '\0';
        App_ESP32_HandleUnsolicitedLine(line);
    }
}

/* ------------------------------------------------------------------ */
/*  Core AT send/receive                                               */
/* ------------------------------------------------------------------ */
uint8_t App_ESP32_SendATCommand(const char *cmd, const char *expect,
                                char *resp_buf, uint16_t resp_buf_size,
                                uint32_t timeout_ms)
{
    if (cmd == NULL || expect == NULL || resp_buf == NULL || resp_buf_size == 0U) {
        return 0U;
    }
    resp_buf[0] = '\0';
    BSP_ESP32_SendString(cmd, 500U);
    BSP_ESP32_SendString("\r\n", 100U);
    uint16_t rx_len = 0U;
    BSP_ESP32_Receive((uint8_t *)resp_buf, resp_buf_size, &rx_len, timeout_ms);
    App_ESP32_ProcessRxText(resp_buf);
    return (strstr(resp_buf, expect) != NULL) ? 1U : 0U;
}

/* ------------------------------------------------------------------ */
/*  Basic commands                                                     */
/* ------------------------------------------------------------------ */
uint8_t App_ESP32_AT_Test(void)
{
    char buf[64];
    uint8_t ok = App_ESP32_SendATCommand("AT", "OK", buf, sizeof(buf), 1000U);
    s_status.at_ok = ok;
    if (ok) {
        App_EventLog_Add(APP_EVENT_ESP32_AT_OK, 0U, 0, 0.0f, "AT ok");
        s_status.last_ok_tick = osKernelGetTickCount();
    } else {
        App_EventLog_Add(APP_EVENT_ESP32_AT_FAIL, 0U, 0, 0.0f, "AT failed");
        s_status.last_error_tick = osKernelGetTickCount();
    }
    return ok;
}

//获取ESP32固件版本号
uint8_t App_ESP32_GetVersion(void)
{
    char buf[ESP32_RESP_BUF_SIZE];
    uint8_t ok = App_ESP32_SendATCommand("AT+GMR", "OK", buf, sizeof(buf), 2000U);
    BSP_Log_Printf("[ESP32] version:\r\n%s\r\n", buf);
    return ok;
}

uint8_t App_ESP32_ResetModule(void)
{
    BSP_Log_Printf("[ESP32] reset\r\n");
    App_EventLog_Add(APP_EVENT_ESP32_RESET, 0U, 0, 0.0f, "ESP32 reset");
    BSP_ESP32_Reset();
    s_status.at_ok           = 0U;
    s_status.wifi_connected  = 0U;
    s_status.mqtt_connected  = 0U;
    s_status.mqtt_subscribed = 0U;
    s_net_state              = ESP32_APP_STATE_IDLE;
    s_status.app_state       = s_net_state;
    return 1U;
}

/* ------------------------------------------------------------------ */
/*  WiFi                                                               */
//通过 AT 指令把 ESP32 设置为 Station 模式，也就是让 ESP32 作为 WiFi 客户端去连接路由器
/* ------------------------------------------------------------------ */
uint8_t App_ESP32_SetWiFiModeStation(void)
{
    char buf[64];
    return App_ESP32_SendATCommand("AT+CWMODE=1", "OK", buf, sizeof(buf), 2000U);
}

uint8_t App_ESP32_JoinWiFi(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') { return 0U; }

    char esc_ssid[64];
    char esc_pass[128];
    if (!App_ESP32_EscapeATString(ssid, esc_ssid, sizeof(esc_ssid))) { return 0U; }
    if (!App_ESP32_EscapeATString(password ? password : "", esc_pass, sizeof(esc_pass))) {
        return 0U;
    }

    char cmd[ESP32_AT_CMD_BUF_SIZE];
    int n = snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", esc_ssid, esc_pass);
    if (n < 0 || (uint16_t)n >= (uint16_t)sizeof(cmd)) { return 0U; }

    char buf[ESP32_RESP_BUF_SIZE];
    return App_ESP32_SendATCommand(cmd, "OK", buf, sizeof(buf), ESP32_WIFI_TIMEOUT_MS);
}

/* ------------------------------------------------------------------ */
/*  MQTT                                                               */
/* ------------------------------------------------------------------ */
uint8_t App_ESP32_MQTT_ConfigUser(const char *client_id,
                                  const char *username,
                                  const char *password)
{
    char esc_cid[64];
    char esc_usr[64];
    char esc_pwd[64];
    if (!App_ESP32_EscapeATString(client_id ? client_id : "", esc_cid, sizeof(esc_cid))) { return 0U; }
    if (!App_ESP32_EscapeATString(username  ? username  : "", esc_usr, sizeof(esc_usr))) { return 0U; }
    if (!App_ESP32_EscapeATString(password  ? password  : "", esc_pwd, sizeof(esc_pwd))) { return 0U; }

    char cmd[ESP32_AT_CMD_BUF_SIZE];
    int n = snprintf(cmd, sizeof(cmd),
                     "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
                     esc_cid, esc_usr, esc_pwd);
    if (n < 0 || (uint16_t)n >= (uint16_t)sizeof(cmd)) { return 0U; }

    char buf[ESP32_RESP_BUF_SIZE];
    return App_ESP32_SendATCommand(cmd, "OK", buf, sizeof(buf), 3000U);
}

uint8_t App_ESP32_MQTT_Connect(const char *host, uint16_t port)
{
    if (host == NULL || host[0] == '\0') { return 0U; }

    char esc_host[128];
    if (!App_ESP32_EscapeATString(host, esc_host, sizeof(esc_host))) { return 0U; }

    char cmd[ESP32_AT_CMD_BUF_SIZE];
    int n = snprintf(cmd, sizeof(cmd),
                     "AT+MQTTCONN=0,\"%s\",%u,0", esc_host, (unsigned)port);
    if (n < 0 || (uint16_t)n >= (uint16_t)sizeof(cmd)) { return 0U; }

    char buf[ESP32_RESP_BUF_SIZE];
    return App_ESP32_SendATCommand(cmd, "OK", buf, sizeof(buf), ESP32_MQTT_TIMEOUT_MS);
}

uint8_t App_ESP32_MQTT_Disconnect(void)
{
    char buf[64];
    uint8_t ok = App_ESP32_SendATCommand("AT+MQTTCLEAN=0", "OK", buf, sizeof(buf), 3000U);
    s_status.mqtt_connected  = 0U;
    s_status.mqtt_subscribed = 0U;
    if (ok) {
        s_status.last_ok_tick = osKernelGetTickCount();
        App_EventLog_Add(APP_EVENT_ESP32_MQTT_DISCONNECTED, 0U, 0, 0.0f,
                         "MQTT disconnected");
        if (s_net_state != ESP32_APP_STATE_DISABLED) {
            s_net_state = ESP32_APP_STATE_IDLE;
        }
    } else {
        s_status.last_error_tick = osKernelGetTickCount();
    }
    if (s_net_state == ESP32_APP_STATE_MQTT_CONFIGURING ||
        s_net_state == ESP32_APP_STATE_MQTT_CONNECTING ||
        s_net_state == ESP32_APP_STATE_MQTT_SUBSCRIBING ||
        s_net_state == ESP32_APP_STATE_MQTT_CONNECTED) {
        s_net_state = ESP32_APP_STATE_IDLE;
    }
    s_status.app_state = s_net_state;
    return ok;
}

uint8_t App_ESP32_MQTT_Publish(const char *topic, const char *payload,
                               uint8_t qos, uint8_t retain)
{
    if (topic == NULL || payload == NULL) { return 0U; }
    if (qos > 1U || retain > 1U) {
        s_status.publish_fail_count++;
        BSP_Log_Printf("[ESP32] invalid MQTT publish policy qos=%u retain=%u\r\n",
                       (unsigned)qos, (unsigned)retain);
        App_EventLog_Add(APP_EVENT_ESP32_MQTT_FAILED, 0U, 0, 0.0f,
                         "invalid publish qos=%u retain=%u",
                         (unsigned)qos, (unsigned)retain);
        return 0U;
    }

    char esc_topic[128];
    char esc_payload[512];
    if (!App_ESP32_EscapeATString(topic,   esc_topic,   sizeof(esc_topic)))   { return 0U; }
    if (!App_ESP32_EscapeATString(payload, esc_payload, sizeof(esc_payload))) { return 0U; }

    char cmd[768];
    int n = snprintf(cmd, sizeof(cmd),
                     "AT+MQTTPUB=0,\"%s\",\"%s\",%u,%u",
                     esc_topic, esc_payload, (unsigned)qos, (unsigned)retain);
    if (n < 0 || (uint16_t)n >= (uint16_t)sizeof(cmd)) {
        s_status.publish_fail_count++;
        return 0U;
    }

    char buf[ESP32_RESP_BUF_SIZE];
    uint8_t ok = App_ESP32_SendATCommand(cmd, "OK", buf, sizeof(buf), 3000U);
    if (!ok) {
        s_status.publish_fail_count++;
        App_EventLog_Add(APP_EVENT_ESP32_MQTT_FAILED, 0U, 0, 0.0f, "publish failed");
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/*  MQTT heartbeat check (publish to status topic)                    */
/* ------------------------------------------------------------------ */
uint8_t App_ESP32_MQTT_CheckAlive(void)
{
    if (s_status.mqtt_connected == 0U) { return 0U; }

    App_StateLock();
    char topic[64];
    uint32_t uptime_s = osKernelGetTickCount() / 1000U;
    strncpy(topic, g_app_state.config.mqtt_topic_status, sizeof(topic) - 1U);
    topic[sizeof(topic) - 1U] = '\0';
    App_StateUnlock();

    uint8_t qos    = App_Upload_GetMqttQosForType(MQTT_MSG_HEARTBEAT);
    uint8_t retain = App_Upload_GetMqttRetainForType(MQTT_MSG_HEARTBEAT);

    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"heartbeat\",\"uptime\":%lu,\"healthy\":1}",
             (unsigned long)uptime_s);

    uint8_t ok = App_ESP32_MQTT_Publish(topic, payload, qos, retain);
    if (!ok) {
        BSP_Log_Printf("[ESP32] MQTT heartbeat failed\r\n");
        App_EventLog_Add(APP_EVENT_ESP32_MQTT_FAILED, 0U, 0, 0.0f, "heartbeat failed");
        s_status.mqtt_connected  = 0U;
        s_status.mqtt_subscribed = 0U;
        s_status.mqtt_fail_count++;
        s_status.last_error_tick = osKernelGetTickCount();
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/*  MQTT Subscribe / Unsubscribe                                       */
/* ------------------------------------------------------------------ */
uint8_t App_ESP32_MQTT_Subscribe(const char *topic, uint8_t qos)
{
    if (topic == NULL || topic[0] == '\0') { return 0U; }
    char esc[128];
    if (!App_ESP32_EscapeATString(topic, esc, sizeof(esc))) { return 0U; }
    char cmd[192];
    int n = snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%u", esc, (unsigned)qos);
    if (n < 0 || (uint16_t)n >= (uint16_t)sizeof(cmd)) { return 0U; }
    char buf[64];
    uint8_t ok = App_ESP32_SendATCommand(cmd, "OK", buf, sizeof(buf), 5000U);
    if (ok) {
        s_status.mqtt_subscribed = 1U;
        s_status.last_ok_tick = osKernelGetTickCount();
        App_EventLog_Add(APP_EVENT_MQTT_SUB_OK, 0U, 0, 0.0f, "sub ok topic=%s", topic);
        BSP_Log_Printf("[ESP32] mqtt subscribed topic=%s\r\n", topic);
    } else {
        s_status.mqtt_subscribed = 0U;
        s_status.last_error_tick = osKernelGetTickCount();
        App_EventLog_Add(APP_EVENT_MQTT_SUB_FAIL, 0U, 0, 0.0f, "sub fail topic=%s", topic);
        BSP_Log_Printf("[ESP32] mqtt subscribe failed\r\n");
    }
    return ok;
}

uint8_t App_ESP32_MQTT_Unsubscribe(const char *topic)
{
    if (topic == NULL || topic[0] == '\0') { return 0U; }
    char esc[128];
    if (!App_ESP32_EscapeATString(topic, esc, sizeof(esc))) { return 0U; }
    char cmd[192];
    int n = snprintf(cmd, sizeof(cmd), "AT+MQTTUNSUB=0,\"%s\"", esc);
    if (n < 0 || (uint16_t)n >= (uint16_t)sizeof(cmd)) { return 0U; }
    char buf[64];
    uint8_t ok = App_ESP32_SendATCommand(cmd, "OK", buf, sizeof(buf), 3000U);
    s_status.mqtt_subscribed = 0U;
    if (ok) {
        s_status.last_ok_tick = osKernelGetTickCount();
    } else {
        s_status.last_error_tick = osKernelGetTickCount();
    }
    return ok;
}

uint8_t App_ESP32_MQTT_IsSubscribed(void)
{
    return s_status.mqtt_subscribed;
}

/* ------------------------------------------------------------------ */
/*  Poll ring buffer for unsolicited +MQTTSUBRECV lines               */
/* ------------------------------------------------------------------ */
void App_ESP32_PollUnsolicited(void)
{
    static char     s_line[256];
    static uint16_t s_li = 0U;
    uint8_t b;
    while (BSP_ESP32_ReadByte(&b)) {
        if (b == '\n') {
            s_line[s_li] = '\0';
            App_ESP32_HandleUnsolicitedLine(s_line);
            s_li = 0U;
        } else if (b != '\r' && s_li < (uint16_t)(sizeof(s_line) - 1U)) {
            s_line[s_li++] = (char)b;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  State accessors                                                    */
/* ------------------------------------------------------------------ */
AppESP32State_t App_ESP32_GetNetState(void)
{
    return s_net_state;
}

void App_ESP32_TriggerConnect(void)
{
    s_trigger_conn = 1U;
}

void App_ESP32_TriggerDisconnect(void)
{
    s_trigger_disconn = 1U;
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */
void App_ESP32_Init(void)
{
    uint8_t mqtt_enable;

    memset(&s_status, 0, sizeof(s_status));
    s_trigger_conn    = 0U;
    s_trigger_disconn = 0U;
    s_error_tick      = 0U;
    s_hb_tick         = 0U;

    App_StateLock();
    mqtt_enable = g_app_state.config.mqtt_enable;
    App_StateUnlock();

    s_net_state        = mqtt_enable ? ESP32_APP_STATE_IDLE : ESP32_APP_STATE_DISABLED;
    s_status.app_state = s_net_state;
    App_MqttCmd_Init();
    App_EventLog_Add(APP_EVENT_ESP32_INIT, 0U, 0, 0.0f, "ESP32 init");
    BSP_Log_Printf("[ESP32] init\r\n");
}

/* ------------------------------------------------------------------ */
/*  Task state machine                                                 */
/* ------------------------------------------------------------------ */
void App_ESP32Task(void *argument)
{
    (void)argument;
    BSP_Log_Printf("[APP] Esp32Task started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_ESP32);
        App_ESP32_PollUnsolicited();

        App_StateLock();
        uint8_t  mqtt_enable = g_app_state.config.mqtt_enable;
        uint8_t  cmd_en      = g_app_state.config.mqtt_cmd_enable;
        char     ssid[32];
        char     pass[64];
        char     host[64];
        uint16_t port;
        char     client_id[32];
        char     username[32];
        char     password[32];
        char     cmd_topic[64];
        strncpy(ssid,      g_app_state.config.wifi_ssid,      sizeof(ssid) - 1U);
        strncpy(pass,      g_app_state.config.wifi_password,  sizeof(pass) - 1U);
        strncpy(host,      g_app_state.config.mqtt_host,      sizeof(host) - 1U);
        port = g_app_state.config.mqtt_port;
        strncpy(client_id, g_app_state.config.mqtt_client_id, sizeof(client_id) - 1U);
        strncpy(username,  g_app_state.config.mqtt_username,  sizeof(username) - 1U);
        strncpy(password,  g_app_state.config.mqtt_password,  sizeof(password) - 1U);
        strncpy(cmd_topic, g_app_state.config.mqtt_topic_cmd, sizeof(cmd_topic) - 1U);
        ssid[sizeof(ssid) - 1U]           = '\0';
        pass[sizeof(pass) - 1U]           = '\0';
        host[sizeof(host) - 1U]           = '\0';
        client_id[sizeof(client_id) - 1U] = '\0';
        username[sizeof(username) - 1U]   = '\0';
        password[sizeof(password) - 1U]   = '\0';
        cmd_topic[sizeof(cmd_topic) - 1U] = '\0';
        App_StateUnlock();

        s_status.app_state = s_net_state;

        if (s_trigger_disconn) {
            s_trigger_disconn = 0U;
            App_ESP32_MQTT_Disconnect();
            App_ESP32_ClearLinkStatus();
            s_net_state = mqtt_enable ? ESP32_APP_STATE_IDLE : ESP32_APP_STATE_DISABLED;
            s_status.app_state = s_net_state;
            osDelay(ESP32_TASK_PERIOD_MS);
            continue;
        }

        /* If MQTT disabled, stay disabled and do not auto-connect. */
        if (!mqtt_enable && !s_trigger_conn) {
            if (s_status.mqtt_connected || s_status.mqtt_subscribed) {
                App_ESP32_MQTT_Disconnect();
                BSP_Log_Printf("[ESP32] mqtt disabled, disconnected\r\n");
            }
            App_ESP32_ClearLinkStatus();
            s_net_state = ESP32_APP_STATE_DISABLED;
            s_status.app_state = s_net_state;
            osDelay(ESP32_TASK_PERIOD_MS);
            continue;
        }

        /* Trigger from command bypasses retry back-off. */
        if (s_trigger_conn) {
            s_trigger_conn = 0U;
            if (s_net_state == ESP32_APP_STATE_MQTT_CONNECTED &&
                s_status.mqtt_connected) {
                BSP_Log_Printf("[ESP32] already connected\r\n");
                osDelay(ESP32_TASK_PERIOD_MS);
                continue;
            }
            s_net_state = ESP32_APP_STATE_AT_CHECK;
        }

        /* Error back-off */
        if (s_net_state == ESP32_APP_STATE_ERROR) {
            uint32_t now  = osKernelGetTickCount();
            if ((now - s_error_tick) < ESP32_ERROR_RETRY_MS) {
                osDelay(ESP32_TASK_PERIOD_MS);
                continue;
            }
            BSP_Log_Printf("[ESP32] retry after error\r\n");
            s_net_state = ESP32_APP_STATE_AT_CHECK;
        }

        /* State machine step */
        switch (s_net_state) {
            case ESP32_APP_STATE_DISABLED:
                if (mqtt_enable && ssid[0] != '\0') {
                    s_net_state = ESP32_APP_STATE_AT_CHECK;
                }
                break;

            case ESP32_APP_STATE_IDLE:
                if (mqtt_enable && ssid[0] != '\0') {
                    s_net_state = ESP32_APP_STATE_AT_CHECK;
                }
                break;

            case ESP32_APP_STATE_AT_CHECK:
                s_status.connect_attempts++;
                BSP_Log_Printf("[ESP32] checking AT...\r\n");
                if (!App_ESP32_AT_Test()) {
                    BSP_Log_Printf("[ESP32] AT no response\r\n");
                    App_ESP32_ResetModule();
                    App_ESP32_MarkError();
                    break;
                }
                if (!App_ESP32_SetWiFiModeStation()) {
                    BSP_Log_Printf("[ESP32] set station mode failed\r\n");
                    s_status.wifi_fail_count++;
                    App_ESP32_MarkError();
                    break;
                }
                s_net_state = ESP32_APP_STATE_WIFI_CONNECTING;
                break;

            case ESP32_APP_STATE_WIFI_CONNECTING:
                if (ssid[0] == '\0') {
                    BSP_Log_Printf("[ESP32] no SSID configured\r\n");
                    App_ESP32_MarkError();
                    break;
                }
                BSP_Log_Printf("[ESP32] joining WiFi ssid=%s\r\n", ssid);
                s_status.mqtt_connected  = 0U;
                s_status.mqtt_subscribed = 0U;
                if (App_ESP32_JoinWiFi(ssid, pass)) {
                    BSP_Log_Printf("[ESP32] WiFi connected\r\n");
                    App_EventLog_Add(APP_EVENT_ESP32_WIFI_CONNECTED, 0U, 0, 0.0f,
                                     "WiFi connected ssid=%s", ssid);
                    s_status.wifi_connected = 1U;
                    s_status.last_ok_tick   = osKernelGetTickCount();
                    s_net_state = ESP32_APP_STATE_WIFI_CONNECTED;
                } else {
                    BSP_Log_Printf("[ESP32] WiFi failed\r\n");
                    App_EventLog_Add(APP_EVENT_ESP32_WIFI_FAILED, 0U, 0, 0.0f,
                                     "WiFi failed ssid=%s", ssid);
                    s_status.wifi_connected  = 0U;
                    s_status.wifi_fail_count++;
                    s_status.last_error_tick = osKernelGetTickCount();
                    s_net_state              = ESP32_APP_STATE_ERROR;
                    s_error_tick             = s_status.last_error_tick;
                }
                break;

            case ESP32_APP_STATE_WIFI_CONNECTED:
                s_net_state = ESP32_APP_STATE_MQTT_CONFIGURING;
                break;

            case ESP32_APP_STATE_MQTT_CONFIGURING:
                if (!App_ESP32_MQTT_ConfigUser(client_id, username, password)) {
                    BSP_Log_Printf("[ESP32] MQTT user config failed\r\n");
                    App_EventLog_Add(APP_EVENT_ESP32_MQTT_FAILED, 0U, 0, 0.0f,
                                     "MQTT user config failed");
                    s_status.mqtt_connected  = 0U;
                    s_status.mqtt_subscribed = 0U;
                    s_status.mqtt_fail_count++;
                    App_ESP32_MarkError();
                    break;
                }
                s_net_state = ESP32_APP_STATE_MQTT_CONNECTING;
                break;

            case ESP32_APP_STATE_MQTT_CONNECTING:
                if (host[0] == '\0') {
                    BSP_Log_Printf("[ESP32] no MQTT host configured\r\n");
                    App_ESP32_MarkError();
                    break;
                }
                BSP_Log_Printf("[ESP32] MQTT connecting %s:%u\r\n", host, (unsigned)port);
                if (App_ESP32_MQTT_Connect(host, port)) {
                    BSP_Log_Printf("[ESP32] MQTT connected\r\n");
                    App_EventLog_Add(APP_EVENT_ESP32_MQTT_CONNECTED, 0U, 0, 0.0f,
                                     "MQTT connected %s:%u", host, (unsigned)port);
                    s_status.mqtt_connected = 1U;
                    s_status.last_ok_tick   = osKernelGetTickCount();
                    s_hb_tick               = s_status.last_ok_tick;
                    s_net_state = (cmd_en && cmd_topic[0] != '\0') ?
                                  ESP32_APP_STATE_MQTT_SUBSCRIBING :
                                  ESP32_APP_STATE_MQTT_CONNECTED;
                    if (!cmd_en || cmd_topic[0] == '\0') {
                        s_status.mqtt_subscribed = 0U;
                    }
                } else {
                    BSP_Log_Printf("[ESP32] MQTT connect failed\r\n");
                    App_EventLog_Add(APP_EVENT_ESP32_MQTT_FAILED, 0U, 0, 0.0f,
                                     "MQTT failed %s:%u", host, (unsigned)port);
                    s_status.mqtt_connected  = 0U;
                    s_status.mqtt_subscribed = 0U;
                    s_status.mqtt_fail_count++;
                    s_status.last_error_tick = osKernelGetTickCount();
                    s_net_state  = ESP32_APP_STATE_ERROR;
                    s_error_tick = s_status.last_error_tick;
                }
                break;

            case ESP32_APP_STATE_MQTT_SUBSCRIBING: {
                uint8_t sub_qos;
                if (!cmd_en || cmd_topic[0] == '\0') {
                    s_status.mqtt_subscribed = 0U;
                    s_net_state = ESP32_APP_STATE_MQTT_CONNECTED;
                    break;
                }
                if (!s_status.mqtt_connected) {
                    s_status.mqtt_fail_count++;
                    App_ESP32_MarkError();
                    break;
                }
                sub_qos = App_Upload_GetMqttQosForType(MQTT_MSG_RESP);
                if (App_ESP32_MQTT_Subscribe(cmd_topic, sub_qos)) {
                    s_net_state = ESP32_APP_STATE_MQTT_CONNECTED;
                } else {
                    s_status.mqtt_fail_count++;
                    App_ESP32_MQTT_Disconnect();
                    App_ESP32_MarkError();
                }
                break;
            }

            case ESP32_APP_STATE_MQTT_CONNECTED: {
                uint32_t now = osKernelGetTickCount();
                if (!s_status.mqtt_connected) {
                    App_ESP32_MarkError();
                    break;
                }
                if (cmd_en && cmd_topic[0] != '\0' && !s_status.mqtt_subscribed) {
                    s_net_state = ESP32_APP_STATE_MQTT_SUBSCRIBING;
                    break;
                }
                if (!cmd_en && s_status.mqtt_subscribed && cmd_topic[0] != '\0') {
                    App_ESP32_MQTT_Unsubscribe(cmd_topic);
                }
                /* Periodic heartbeat check every 30s */
                if ((now - s_hb_tick) >= ESP32_HEARTBEAT_PERIOD_MS) {
                    s_hb_tick = now;
                    if (!App_ESP32_MQTT_CheckAlive()) {
                        s_net_state  = ESP32_APP_STATE_ERROR;
                        s_error_tick = osKernelGetTickCount();
                    }
                }
                break;
            }

            default:
                break;
        }

        s_status.app_state = s_net_state;
        osDelay(ESP32_TASK_PERIOD_MS);
    }
}
