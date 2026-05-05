#include "app_mqtt_cmd.h"
#include "app_cmd.h"
#include "app_esp32.h"
#include "app_upload.h"
#include "app_event_log.h"
#include "app_types.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

static MqttCmdStats_t s_stats;

void App_MqttCmd_Init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

void App_MqttCmd_GetStats(MqttCmdStats_t *out)
{
    if (out) { *out = s_stats; }
}

/* Escape '"' → '\'' in-place for embedding in JSON msg field 转义响应内容，方便放入 JSON*/
static void EscapeForJson(char *s)
{
    for (; *s; s++) {
        if (*s == '"') { *s = '\''; }
    }
}

//从payload中提取出命令，然后执行
static void Dispatch(const char *payload)
{
    s_stats.rx_count++;
    s_stats.last_rx_tick = osKernelGetTickCount();

    App_StateLock();
    uint8_t cmd_en = g_app_state.config.mqtt_cmd_enable;
    char    token[24];
    char    resp_topic[64];
    strncpy(token,      g_app_state.config.mqtt_cmd_token,  sizeof(token)      - 1U);
    strncpy(resp_topic, g_app_state.config.mqtt_topic_resp, sizeof(resp_topic) - 1U);
    token[sizeof(token)-1U]           = '\0';
    resp_topic[sizeof(resp_topic)-1U] = '\0';
    App_StateUnlock();

    uint8_t qos    = App_Upload_GetMqttQosForType(MQTT_MSG_RESP);
    uint8_t retain = App_Upload_GetMqttRetainForType(MQTT_MSG_RESP);

    //如果配置里关闭了 MQTT 命令控制，它会发布一条响应：命令失败，原因是 MQTT 命令功能被禁用
    if (!cmd_en) {
        App_ESP32_MQTT_Publish(resp_topic,
            "{\"type\":\"resp\",\"ok\":0,\"err\":\"CMD_DISABLED\"}", qos, retain);
        s_stats.fail_count++;
        return;
    }

    /* Expect: TOKEN <tok> <cmd...> */
    //检查接收到的 payload 格式是否正确，若不正确则发布响应：{"type":"resp","ok":0,"err":"BAD_FORMAT"}
    if (strncmp(payload, "TOKEN ", 6) != 0) {
        App_ESP32_MQTT_Publish(resp_topic,
            "{\"type\":\"resp\",\"ok\":0,\"err\":\"BAD_FORMAT\"}", qos, retain);
        s_stats.auth_fail_count++;
        App_EventLog_Add(APP_EVENT_MQTT_AUTH_FAIL, 0U, 0, 0.0f, "bad format");
        return;
    }

    //找出 payload 里的 token
    const char *p = payload + 6;//"TOKEN " 长度是 6，所以 p 指向真正 token 的开头
    /* find end of token */
    const char *sp = p;
    while (*sp && *sp != ' ') { sp++; }//找 token 的结束位置，也就是第一个空格
    uint16_t tok_len = (uint16_t)(sp - p);//得到 token 长度

    //校验 token 是否正确：先判断长度是否相等，再判断内容是否一致
    if (tok_len != (uint16_t)strlen(token) || strncmp(p, token, tok_len) != 0) {
        App_ESP32_MQTT_Publish(resp_topic,
            "{\"type\":\"resp\",\"ok\":0,\"err\":\"AUTH_FAILED\"}", qos, retain);
        s_stats.auth_fail_count++;
        App_EventLog_Add(APP_EVENT_MQTT_AUTH_FAIL, 0U, 0, 0.0f, "auth fail");
        BSP_Log_Printf("[MQTTCMD] auth fail\r\n");
        return;
    }

    const char *cmd = (*sp == ' ') ? sp + 1 : sp;//提取真正的命令
    App_EventLog_Add(APP_EVENT_MQTT_CMD_RECEIVED, 0U, 0, 0.0f, "cmd=%s", cmd);

    char resp_raw[256];
    resp_raw[0] = '\0';
    AppCmdContext_t ctx = { APP_CMD_SOURCE_MQTT, resp_raw, sizeof(resp_raw) };
    App_Cmd_ExecuteLine(cmd, &ctx);//执行命令

    /* strip trailing \r\n from resp_raw 去掉响应末尾的换行 */
    uint16_t rlen = (uint16_t)strlen(resp_raw);
    while (rlen > 0U && (resp_raw[rlen-1U] == '\r' || resp_raw[rlen-1U] == '\n')) {
        resp_raw[--rlen] = '\0';
    }
    EscapeForJson(resp_raw);

    //组装 MQTT 响应 JSON
    char resp_json[512];
    int n = snprintf(resp_json, sizeof(resp_json),
                     "{\"type\":\"resp\",\"ok\":1,\"cmd\":\"%s\",\"msg\":\"%s\"}",
                     cmd, resp_raw);
    if (n < 0 || (uint16_t)n >= sizeof(resp_json)) {
        resp_json[sizeof(resp_json)-1U] = '\0';
    }

    uint8_t ok = App_ESP32_MQTT_Publish(resp_topic, resp_json, qos, retain);
    BSP_Log_Printf("[MQTTCMD] cmd=%s ok=%u\r\n", cmd, (unsigned)ok);

    if (ok) {
        s_stats.ok_count++;
        s_stats.last_ok_tick = osKernelGetTickCount();
        App_EventLog_Add(APP_EVENT_MQTT_CMD_OK, 0U, 0, 0.0f, "cmd=%s", cmd);
    } else {
        s_stats.fail_count++;
        s_stats.last_fail_tick = osKernelGetTickCount();
        App_EventLog_Add(APP_EVENT_MQTT_CMD_FAIL, 0U, 0, 0.0f, "cmd=%s", cmd);
    }
}

void App_MqttCmd_HandleDirect(const char *payload)
{
    if (payload && payload[0] != '\0') { Dispatch(payload); }
}

//从 ESP32 接收到的原始字符串中，提取出 MQTT payload，然后交给 Dispatch() 处理
void App_MqttCmd_HandleRaw(const char *subrecv_line)
{
    /* +MQTTSUBRECV:0,"topic",len,payload 判断是不是 MQTT 订阅接收数据*/
    if (strncmp(subrecv_line, "+MQTTSUBRECV:", 13) != 0) { return; }
    const char *p = subrecv_line + 13;

    /* skip link_id MQTT 跳过 link_id*/
    while (*p && *p != ',') { p++; }
    if (*p != ',') { return; }
    p++; /* skip ',' */

    /* skip topic in quotes 跳过 topic 字符串 */
    if (*p != '"') { return; }
    p++;
    while (*p && *p != '"') { p++; }
    if (*p != '"') { return; }
    p++; /* skip closing '"' */

    /* skip ',len,' 跳过 ,len, */
    if (*p != ',') { return; }
    p++;
    while (*p && *p != ',') { p++; }
    if (*p != ',') { return; }
    p++; /* now at payload */

    if (*p != '\0') { Dispatch(p); }
}
