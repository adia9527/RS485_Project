#include "app_cmd.h"
#include "app_types.h"
#include "app_config.h"
#include "app_alarm.h"
#include "app_health.h"
#include "app_touch.h"
#include "app_display.h"
#include "app_esp32.h"
#include "app_upload.h"
#include "app_mqtt_cmd.h"
#include "protocol_upload.h"
#include "app_event_log.h"
#include "app_log_storage.h"
#include "bsp_w25q64.h"
#include "bsp_lcd.h"
#include "bsp_log.h"
#include "usart.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define CMD_LINE_MAX  128U
#define CMD_QUEUE_DEPTH 4U

static osMessageQueueId_t s_cmd_queue;
static uint8_t            s_rx_byte;//串口单字节接收缓冲变量
static char               s_rx_line[CMD_LINE_MAX];//整行命令的接收缓冲区
static uint16_t           s_rx_pos;//当前接收到命令行的写入位置

static UploadFormat_t     s_upload_format = UPLOAD_FORMAT_TEXT;
static AppCmdContext_t   *s_reply_ctx     = NULL;

/* static snapshot buffer — avoids large stack allocation in GET LOG */
#define LOG_BATCH  16U //表示每批处理 16 条日志
static AppEventLogItem_t  s_log_snap[APP_EVENT_LOG_CAPACITY];//日志快照缓冲区

static void Cmd_Reply(const char *msg);

//返回数据上传格式
UploadFormat_t App_Cmd_GetUploadFormat(void)
{
    return s_upload_format;
}

/* ---- string helpers ---- */
//把字符串中的所有字符转换成大写字母
static void Str_ToUpper(char *s)
{
    //'\0' 的数值是 0
    while (*s) { *s = (char)toupper((unsigned char)*s); s++; }
}

//从当前字符串位置 p 开始，跳过当前单词，再跳过后面的空格，最后返回下一个单词的起始位置。
static const char *Str_NextToken(const char *p)
{
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    return p;
}

//判断字符串 token 的开头，是否正好是一个完整的单词 word
static uint8_t Str_TokenEquals(const char *token, const char *word)
{
    size_t len = strlen(word);
    return (strncmp(token, word, len) == 0 &&
            (token[len] == '\0' || token[len] == ' ')) ? 1U : 0U;
}

//把命令参数里的 0 或 1 字符串，解析成 uint8_t 类型的二进制值。
static uint8_t Cmd_ParseBinaryValue(const char *token, uint8_t *value)
{
    if (Str_TokenEquals(token, "0")) {
        *value = 0U;
        return 1U;
    }
    if (Str_TokenEquals(token, "1")) {
        *value = 1U;
        return 1U;
    }
    return 0U;
}

//把字符串形式的 MQTT 消息类型，转换成程序内部使用的枚举类型
static uint8_t Cmd_MqttMsgTypeFromToken(const char *token, MqttMsgType_t *type)
{
    if (Str_TokenEquals(token, "DATA")) {
        *type = MQTT_MSG_DATA;
        return 1U;
    }
    if (Str_TokenEquals(token, "ALARM")) {
        *type = MQTT_MSG_ALARM;
        return 1U;
    }
    if (Str_TokenEquals(token, "STATUS")) {
        *type = MQTT_MSG_STATUS;
        return 1U;
    }
    if (Str_TokenEquals(token, "RESP")) {
        *type = MQTT_MSG_RESP;
        return 1U;
    }
    return 0U;
}

//将所有类型数据的QOS设置为一样的QOS
static void Cmd_SetAllMqttQos(uint8_t qos)
{
    App_StateLock();
    g_app_state.config.mqtt_qos = qos;
    g_app_state.config.mqtt_qos_data = qos;
    g_app_state.config.mqtt_qos_alarm = qos;
    g_app_state.config.mqtt_qos_status = qos;
    g_app_state.config.mqtt_qos_resp = qos;
    App_StateUnlock();

    App_Config_MarkDirty();
    App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, qos, 0.0f,
                     "set MQTT QOS all=%u", (unsigned)qos);
}

//设置某个具体数据类型的QOS
static void Cmd_SetOneMqttQos(MqttMsgType_t type, uint8_t qos)
{
    App_StateLock();
    switch (type) {
        case MQTT_MSG_DATA:
            g_app_state.config.mqtt_qos_data = qos;
            break;
        case MQTT_MSG_ALARM:
            g_app_state.config.mqtt_qos_alarm = qos;
            break;
        case MQTT_MSG_STATUS:
            g_app_state.config.mqtt_qos_status = qos;
            break;
        case MQTT_MSG_RESP:
            g_app_state.config.mqtt_qos_resp = qos;
            break;
        default:
            break;
    }
    App_StateUnlock();

    App_Config_MarkDirty();
    App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, qos, 0.0f,
                     "set MQTT QOS %s=%u",
                     App_Upload_MqttMsgTypeToString(type), (unsigned)qos);
}

//设置所有数据上传的retain类型
static void Cmd_SetAllMqttRetain(uint8_t retain)
{
    App_StateLock();
    g_app_state.config.mqtt_retain = retain;
    g_app_state.config.mqtt_retain_data = retain;
    g_app_state.config.mqtt_retain_alarm = retain;
    g_app_state.config.mqtt_retain_status = retain;
    g_app_state.config.mqtt_retain_resp = retain;
    App_StateUnlock();

    App_Config_MarkDirty();
    App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, retain, 0.0f,
                     "set MQTT RETAIN all=%u", (unsigned)retain);
}

//设置单个数据上传的retain类型
static void Cmd_SetOneMqttRetain(MqttMsgType_t type, uint8_t retain)
{
    App_StateLock();
    switch (type) {
        case MQTT_MSG_DATA:
            g_app_state.config.mqtt_retain_data = retain;
            break;
        case MQTT_MSG_ALARM:
            g_app_state.config.mqtt_retain_alarm = retain;
            break;
        case MQTT_MSG_STATUS:
            g_app_state.config.mqtt_retain_status = retain;
            break;
        case MQTT_MSG_RESP:
            g_app_state.config.mqtt_retain_resp = retain;
            break;
        default:
            break;
    }
    App_StateUnlock();

    App_Config_MarkDirty();
    App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, retain, 0.0f,
                     "set MQTT RETAIN %s=%u",
                     App_Upload_MqttMsgTypeToString(type), (unsigned)retain);
}

//设置QOS：QOS [DATA|ALARM|STATUS|RESP] <0|1>
static void Cmd_SetMqttQos(const char *args)
{
    const char *first = Str_NextToken(args);
    const char *second = Str_NextToken(first);
    uint8_t qos;
    MqttMsgType_t type;

    if (*first == '\0') {
        Cmd_Reply("[CMD] error: invalid qos\r\n");
        return;
    }

    if (*second == '\0') {
        if (!Cmd_ParseBinaryValue(first, &qos)) {
            Cmd_Reply("[CMD] error: invalid qos\r\n");
            return;
        }
        Cmd_SetAllMqttQos(qos);
        Cmd_Reply("[CMD] ok\r\n");
        return;
    }

    if (!Cmd_MqttMsgTypeFromToken(first, &type)) {
        Cmd_Reply("[CMD] error: invalid mqtt msg type\r\n");
        return;
    }
    if (!Cmd_ParseBinaryValue(second, &qos)) {
        Cmd_Reply("[CMD] error: invalid qos\r\n");
        return;
    }

    Cmd_SetOneMqttQos(type, qos);
    Cmd_Reply("[CMD] ok\r\n");
}

static void Cmd_SetMqttRetain(const char *args)
{
    const char *first = Str_NextToken(args);
    const char *second = Str_NextToken(first);
    uint8_t retain;
    MqttMsgType_t type;

    if (*first == '\0') {
        Cmd_Reply("[CMD] error: invalid retain\r\n");
        return;
    }

    if (*second == '\0') {
        if (!Cmd_ParseBinaryValue(first, &retain)) {
            Cmd_Reply("[CMD] error: invalid retain\r\n");
            return;
        }
        Cmd_SetAllMqttRetain(retain);
        Cmd_Reply("[CMD] ok\r\n");
        return;
    }

    if (!Cmd_MqttMsgTypeFromToken(first, &type)) {
        Cmd_Reply("[CMD] error: invalid mqtt msg type\r\n");
        return;
    }
    if (!Cmd_ParseBinaryValue(second, &retain)) {
        Cmd_Reply("[CMD] error: invalid retain\r\n");
        return;
    }

    Cmd_SetOneMqttRetain(type, retain);
    Cmd_Reply("[CMD] ok\r\n");
}

/* ---- reply helper ---- */
//根据要回复的来源选择不同的输出路径
static void Cmd_Reply(const char *msg)
{
    if (s_reply_ctx != NULL && s_reply_ctx->source == APP_CMD_SOURCE_MQTT
        && s_reply_ctx->resp_buf != NULL) {
        uint16_t used = (uint16_t)strlen(s_reply_ctx->resp_buf);
        uint16_t rem  = s_reply_ctx->resp_buf_size - used;
        if (rem > 1U) {
            strncat(s_reply_ctx->resp_buf, msg, (size_t)(rem - 1U));
        }
    } else {
        BSP_Log_Printf("%s", msg);
    }
}

static void Cmd_ProcessLine(char *line, const char *line_raw);

/* ---- App_Cmd_ExecuteLine (public) ---- */
void App_Cmd_ExecuteLine(const char *line, AppCmdContext_t *ctx)
{
    s_reply_ctx = ctx;
    char buf[CMD_LINE_MAX];
    char raw[CMD_LINE_MAX];//对原始内容 line 进行备份
    strncpy(buf, line, CMD_LINE_MAX - 1U);
    buf[CMD_LINE_MAX - 1U] = '\0';
    strncpy(raw, line, CMD_LINE_MAX - 1U);
    raw[CMD_LINE_MAX - 1U] = '\0';
    Cmd_ProcessLine(buf, raw);
    s_reply_ctx = NULL;
}

/* ---- command handlers ---- */
static void Cmd_Help(void)
{
    /*
    | 命令           | 含义       |
    | ------------ | -------- |
    | `HELP`       | 查看帮助信息   |
    | `GET DATA`   | 获取当前数据   |
    | `GET CONFIG` | 获取当前配置   |
    | `GET STATUS` | 获取设备状态   |
    | `GET FORMAT` | 获取当前输出格式 |
    | `GET LOG`    | 获取日志     |
    */
    Cmd_Reply("[CMD] commands: HELP GET DATA GET CONFIG GET STATUS GET FORMAT GET LOG\r\n");
    /*
    | 命令              | 含义                                     |
    | --------------- | -------------------------------------- |
    | `GET LOG <N>`   | 获取第 `N` 条日志，或者获取最近 `N` 条日志，具体要看后面的解析代码 |
    | `GET LOG ALARM` | 获取报警日志                                 |
    | `GET LOG COMM`  | 获取通信日志                                 |
    | `CLEAR LOG`     | 清空日志                                   |
    */
    Cmd_Reply("[CMD]           GET LOG <N>  GET LOG ALARM  GET LOG COMM  CLEAR LOG\r\n");
    /*
    | 命令                | 含义                |
    | ----------------- | ----------------- |
    | `SET <key> <val>` | 设置某个配置项的值         |
    | `SAVE`            | 保存当前配置            |
    | `DEFAULT`         | 恢复默认配置            |
    | `MUTE`            | 静音，通常用于关闭蜂鸣器或报警声音 |
    */
    Cmd_Reply("[CMD]           SET <key> <val>  SAVE  DEFAULT  MUTE\r\n");
    /*
    | 命令                | 含义            |
    | ----------------- | ------------- |
    | `SET FORMAT TEXT` | 设置输出为普通文本格式   |
    | `SET FORMAT JSON` | 设置输出为 JSON 格式 |
    */
    Cmd_Reply("[CMD]           SET FORMAT TEXT|JSON\r\n");
    /*
    | 命令                   | 含义              |
    | -------------------- | --------------- |
    | `GET FLASH ID`       | 获取 Flash 芯片 ID  |
    | `GET FLASH LOG STAT` | 获取 Flash 日志存储状态 |
    */
    Cmd_Reply("[CMD]           GET FLASH ID  GET FLASH LOG STAT\r\n");
    /*
    | 命令                  | 含义                           |
    | ------------------- | ---------------------------- |
    | `GET FLASH LOG [N]` | 获取 Flash 中的日志，`[N]` 表示数字参数可选 |
    | `CLEAR FLASH LOG`   | 清空 Flash 中保存的日志              |
    */
    Cmd_Reply("[CMD]           GET FLASH LOG [N]  CLEAR FLASH LOG\r\n");
    /*
    | 命令                | 含义              |
    | ----------------- | --------------- |
    | `GET LOG STORAGE` | 查看日志当前存储方式或存储状态 |
    | `FLUSH LOG`       | 将缓存中的日志立即写入存储器  |
    */
    Cmd_Reply("[CMD]           GET LOG STORAGE  FLUSH LOG\r\n");
    /*
    | 命令                                       | 含义                   |
    | ---------------------------------------- | -------------------- |
    | `GET BUS`                                | 查看当前各传感器使用的总线配置      |
    | `SET BUS <TH\|HUMAN\|LIGHT\|CO2> <1\|2>` | 设置某个传感器使用 1 号或 2 号总线 |
    */ 
    Cmd_Reply("[CMD]           GET BUS  SET BUS <TH|HUMAN|LIGHT|CO2> <1|2>\r\n");
    /*
    | 命令         | 含义         |
    | ---------- | ---------- |
    | `LCD TEST` | 测试 LCD 显示屏 |
    */
    Cmd_Reply("[CMD]           LCD TEST  LVGL TEST\r\n");
    /*
    | 命令              | 含义                       |
    | --------------- | ------------------------ |
    | `ESP AT`        | 发送基础 AT 测试命令，检查 ESP 是否响应 |
    | `ESP RESET`     | 重启 ESP 模块                |
    | `ESP VER`       | 查看 ESP 固件版本              |
    | `ESP RAW <cmd>` | 向 ESP 发送原始 AT 命令         |
    */
    Cmd_Reply("[CMD]           ESP AT  ESP RESET  ESP VER  ESP RAW <cmd>\r\n");
    /*
    | 命令            | 含义       |
    | ------------- | -------- |
    | `TOUCH INFO`  | 查看触摸模块信息 |
    | `TOUCH TEST`  | 测试触摸功能   |
    | `TOUCH RESET` | 重置触摸模块   |
    */
    Cmd_Reply("[CMD]           TOUCH INFO  TOUCH TEST  TOUCH RESET\r\n");
    Cmd_Reply("[CMD]           GET WIFI  SET WIFI SSID <s>  SET WIFI PASS <p>\r\n");
    Cmd_Reply("[CMD]           GET MQTT  SET MQTT HOST/PORT/CLIENT/USER/PASS/ENABLE\r\n");
    Cmd_Reply("[CMD]           SET MQTT TOPIC DATA/ALARM/STATUS/CMD/RESP <topic>\r\n");
    Cmd_Reply("[CMD]           SET MQTT QOS [DATA|ALARM|STATUS|RESP] 0|1\r\n");
    Cmd_Reply("[CMD]           SET MQTT RETAIN [DATA|ALARM|STATUS|RESP] 0|1\r\n");
    Cmd_Reply("[CMD]           SET MQTT TOKEN <tok>  SET MQTT CMD_ENABLE 0|1\r\n");
    Cmd_Reply("[CMD]           MQTT CONNECT  MQTT DISCONNECT  MQTT SUB  MQTT UNSUB\r\n");
    Cmd_Reply("[CMD]           MQTT PUB TEST  MQTT STATUS  MQTT CMD TEST <payload>\r\n");
    Cmd_Reply("[CMD]           GET UPLOAD  SET UPLOAD TARGET USART1|MQTT|USART1_AND_MQTT|NONE\r\n");
    Cmd_Reply("[CMD]           GET NET  NET TEST\r\n");
    Cmd_Reply("[CMD]           UPLOAD TEST USART1|MQTT|BOTH\r\n");
    Cmd_Reply("[CMD]           SET FORMAT JSON|TEXT\r\n");
}

static void Cmd_GetData(void)
{
    App_StateLock();
    AppState_t snap = g_app_state;
    App_StateUnlock();

    char buf[192];
    snprintf(buf, sizeof(buf),
        "[CMD] DATA temp=%.1f,humi=%.1f,co2=%u,light=%u,human=%u,"
        "vin=%.2f,ain1=%.2f,ain2=%.2f,alarm=%u\r\n",
        (double)snap.sensors[0].values[0],
        (double)snap.sensors[0].values[1],
        (unsigned)snap.sensors[3].values[0],
        (unsigned)snap.sensors[2].values[0],
        (unsigned)snap.sensors[1].values[0],
        (double)snap.adc.voltage[0],
        (double)snap.adc.voltage[1],
        (double)snap.adc.voltage[2],
        (unsigned)snap.alarm.active);
    Cmd_Reply(buf);
}

/*
返回：传感器从机地址、上下阈值、轮询周期、上传周期、背光周期、告警开关、蜂鸣器或LED是否处于报警状态
*/
static void Cmd_GetConfig(void)
{
    App_StateLock();
    SysConfig_t c = g_app_state.config;
    App_StateUnlock();

    char buf[256];
    snprintf(buf, sizeof(buf),
        "[CMD] CONFIG id=%u th_addr=%u human_addr=%u light_addr=%u co2_addr=%u\r\n",
        c.device_id, c.temp_humi_addr, c.human_addr, c.light_addr, c.co2_addr);
    Cmd_Reply(buf);

    snprintf(buf, sizeof(buf),
        "[CMD] CONFIG temp_high=%.1f temp_low=%.1f humi_high=%.1f humi_low=%.1f\r\n",
        (double)c.temp_high_threshold, (double)c.temp_low_threshold,
        (double)c.humi_high_threshold, (double)c.humi_low_threshold);
    Cmd_Reply(buf);

    snprintf(buf, sizeof(buf),
        "[CMD] CONFIG co2_high=%u light_high=%u light_low=%u\r\n",
        c.co2_high_threshold, c.light_high_threshold, c.light_low_threshold);
    Cmd_Reply(buf);

    snprintf(buf, sizeof(buf),
        "[CMD] CONFIG vin_low=%.2f ain1_high=%.2f ain2_high=%.2f\r\n",
        (double)c.vin_low_threshold, (double)c.ain1_high_threshold, (double)c.ain2_high_threshold);
    Cmd_Reply(buf);

    snprintf(buf, sizeof(buf),
        "[CMD] CONFIG poll=%u upload=%u backlight=%u\r\n",
        (unsigned)c.sensor_poll_interval_ms, (unsigned)c.upload_period_ms,
        (unsigned)c.backlight_timeout_ms);
    Cmd_Reply(buf);

    snprintf(buf, sizeof(buf),
        "[CMD] CONFIG alarm_en=%u buzzer_en=%u led_alarm_en=%u\r\n",
        c.alarm_enable, c.buzzer_enable, c.led_alarm_enable);
    Cmd_Reply(buf);
}

/*
- 返回：传感器的状态、失败次数、CRC校验失败次数和协议握手失败次数
- 告警状态、用户是否静音、当前世界和剩余堆大小
- 系统整体是否健康、错误掩码、此次系统启动（复位）的原因
- 系统中存在的任务名称、存活状态以及超时次数
*/
static void Cmd_GetStatus(void)
{
    App_StateLock();
    AppState_t snap = g_app_state;
    App_StateUnlock();

    static const char *s_st[] = { "UNKNOWN","ONLINE","OFFLINE","TIMEOUT","CRC_ERR","DATA_ERR" };
    char buf[192];

    for (uint8_t i = 0U; i < SENSOR_COUNT; i++) {
        uint32_t st = (uint32_t)snap.sensors[i].status;
        if (st >= 6U) st = 0U;
        snprintf(buf, sizeof(buf),
            "[CMD] SENSOR[%u] %s fail=%u crc_err=%u proto_err=%u\r\n",
            i, s_st[st],
            snap.sensors[i].fail_count,
            (unsigned)snap.sensors[i].crc_err_count,
            (unsigned)snap.sensors[i].proto_err_count);
        Cmd_Reply(buf);
    }

    uint32_t uptime_s = osKernelGetTickCount() / 1000U;
    snprintf(buf, sizeof(buf),
        "[CMD] STATUS alarm=%u muted=%u uptime=%us heap=%u\r\n",
        snap.alarm.active, snap.alarm.muted,
        (unsigned)uptime_s,
        (unsigned)xPortGetFreeHeapSize());
    Cmd_Reply(buf);

    AppHealthSnapshot_t hs;
    App_Health_GetSnapshot(&hs);
    snprintf(buf, sizeof(buf),
        "[CMD] HEALTH healthy=%u fault=0x%08lX reset=%s\r\n",
        hs.healthy, hs.fault_mask, App_Health_GetResetReasonString());
    Cmd_Reply(buf);

    static const char *s_task_names[] = {
        "Sensor","Alarm","Upload","Config","Cmd","Display","Backlight","Monitor"
    };
    for (uint8_t i = 0U; i < APP_TASK_ID_COUNT; i++) {
        snprintf(buf, sizeof(buf),
            "[CMD] TASK %-9s alive=%u timeout_cnt=%lu\r\n",
            s_task_names[i], hs.tasks[i].alive, hs.tasks[i].timeout_count);
        Cmd_Reply(buf);
    }
}

//返回系统上传数据格式
static void Cmd_GetFormat(void)
{
    BSP_Log_Printf("[CMD] upload format=%s\r\n",
        s_upload_format == UPLOAD_FORMAT_JSON ? "json" : "text");
}

static void Cmd_Save(void)
{
    SysConfig_t snap;
    App_StateLock();
    snap = g_app_state.config;
    App_StateUnlock();

    if (App_Config_Save(&snap)) {
        Cmd_Reply("[CMD] save ok\r\n");
        App_EventLog_Add(APP_EVENT_CONFIG_SAVED, 0U, 0, 0.0f, "config saved via cmd");
    } else {
        Cmd_Reply("[CMD] save failed\r\n");
        App_EventLog_Add(APP_EVENT_CMD_ERROR, 0U, 0, 0.0f, "save failed");
    }
}

static void Cmd_Default(void)
{
    App_Config_RestoreDefault();
    Cmd_Reply("[CMD] restore default ok\r\n");
    App_EventLog_Add(APP_EVENT_CONFIG_RESTORE_DEFAULT, 0U, 0, 0.0f, "restore default via cmd");
}

static void Cmd_Mute(void)
{
    App_Alarm_Mute();
    Cmd_Reply("[CMD] alarm muted\r\n");
}

/* forward declarations for handlers called from Cmd_Set */
static void Cmd_SetUploadTarget(const char *val);
static void Cmd_SetWifiSsid(const char *val);
static void Cmd_SetWifiPass(const char *val);
static void Cmd_SetMqtt(const char *args, const char *args_raw);

/* ---- SET dispatcher ---- */
static void Cmd_Set(const char *line_upper, const char *line_raw)
{   
    /* line_upper already uppercased, starts at "SET ..." */
    const char *key = Str_NextToken(line_upper); /* skip "SET" */
    const char *val = Str_NextToken(key);

    const char *key_raw = Str_NextToken(line_raw);
    const char *val_raw = Str_NextToken(key_raw);

    /* SET UPLOAD TARGET — delegate to dedicated handler */
    //设置数据上传对象：SET UPLOAD TARGET UART/MQTT/BOTH/NONE
    if (strncmp(key, "UPLOAD", 6) == 0) {
        const char *sub = val;
        const char *tgt = Str_NextToken(sub);
        if (strncmp(sub, "TARGET", 6) == 0) {
            Cmd_SetUploadTarget(tgt);
        } else {
            Cmd_Reply("[CMD] error: invalid upload parameter\r\n");
        }
        return;
    }

    /* SET WIFI SSID / PASS */
    //设置WIFI信息：SET WIFI SSID MyHomeWiFi   SET WIFI PASS abcDEF123
    if (strncmp(key, "WIFI", 4) == 0) {
        const char *sub = val;
        const char *sub_raw = val_raw;
        const char *arg_raw = Str_NextToken(sub_raw);
        if      (strncmp(sub, "SSID", 4) == 0) { Cmd_SetWifiSsid(arg_raw); }
        else if (strncmp(sub, "PASS", 4) == 0) { Cmd_SetWifiPass(arg_raw); }
        else { Cmd_Reply("[CMD] error: invalid wifi parameter\r\n"); }
        return;
    }

    /* SET MQTT ... */
   //设置MQTT配置
    if (strncmp(key, "MQTT", 4) == 0) {
        Cmd_SetMqtt(val, val_raw);
        return;
    }

    /* SET FORMAT TEXT|JSON */
    //设置数据上传格式
    if (strncmp(key, "FORMAT", 6) == 0) {
        if (strncmp(val, "JSON", 4) == 0) {
            s_upload_format = UPLOAD_FORMAT_JSON;
            App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, 0, 0.0f, "set FORMAT=json");
            Cmd_Reply("[CMD] upload format=json\r\n");
        } else {
            s_upload_format = UPLOAD_FORMAT_TEXT;
            App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, 0, 0.0f, "set FORMAT=text");
            Cmd_Reply("[CMD] upload format=text\r\n");
        }
        return;
    }

    /* SET ADDR TH|HUMAN|LIGHT|CO2 <n> */
    //设置传感器地址
    if (strncmp(key, "ADDR", 4) == 0) {
        const char *which = val;
        const char *nstr  = Str_NextToken(val);
        long n = strtol(nstr, NULL, 10);
        if (n < 1 || n > 247) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; }//传感器地址范围为1~247
        App_StateLock();
        if      (strncmp(which, "TH",    2) == 0) g_app_state.config.temp_humi_addr = (uint8_t)n;
        else if (strncmp(which, "HUMAN", 5) == 0) g_app_state.config.human_addr     = (uint8_t)n;
        else if (strncmp(which, "LIGHT", 5) == 0) g_app_state.config.light_addr     = (uint8_t)n;
        else if (strncmp(which, "CO2",   3) == 0) g_app_state.config.co2_addr       = (uint8_t)n;
        else { App_StateUnlock(); Cmd_Reply("[CMD] error: invalid parameter\r\n"); return; }
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] ok\r\n");
        return;
    }

    /* Numeric SET commands */
    //处理数值型 SET 命令，如 SET TEMP_HIGH 35.5
    double fval = strtod(val, NULL);
    long   ival = (long)fval;

    //设置温湿度告警阈值，电源和ADC输入阈值
#define SET_FLOAT_RANGE(field, lo, hi) \
    do { if (fval < (lo) || fval > (hi)) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; } \
         App_StateLock(); g_app_state.config.field = (float)fval; App_StateUnlock(); \
         App_Config_MarkDirty(); \
         App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, 0, (float)fval, "set %s=%.3f", key, fval); \
         Cmd_Reply("[CMD] ok\r\n"); return; } while(0)

    //设置CO2和光照告警阈值、服务器端口、配置版本号
#define SET_U16_RANGE(field, lo, hi) \
    do { if (ival < (lo) || ival > (hi)) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; } \
         App_StateLock(); g_app_state.config.field = (uint16_t)ival; App_StateUnlock(); \
         App_Config_MarkDirty(); \
         App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, ival, 0.0f, "set %s=%ld", key, ival); \
         Cmd_Reply("[CMD] ok\r\n"); return; } while(0)
    
    //设置系统配置魔术值、传感器轮询间隔、数据上传周期、背光自动关闭时间
#define SET_U32_RANGE(field, lo, hi) \
    do { if (ival < (lo) || ival > (hi)) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; } \
         App_StateLock(); g_app_state.config.field = (uint32_t)ival; App_StateUnlock(); \
         App_Config_MarkDirty(); \
         App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, ival, 0.0f, "set %s=%ld", key, ival); \
         Cmd_Reply("[CMD] ok\r\n"); return; } while(0)

    //设置本设备 ID、传感器总线配置、告警开关配置、日志开关、是否启用 MQTT 功能、QoS 等级、MQTT 命令功能
         #define SET_U8_RANGE(field, lo, hi) \
    do { if (ival < (lo) || ival > (hi)) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; } \
         App_StateLock(); g_app_state.config.field = (uint8_t)ival; App_StateUnlock(); \
         App_Config_MarkDirty(); \
         App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, ival, 0.0f, "set %s=%ld", key, ival); \
         Cmd_Reply("[CMD] ok\r\n"); return; } while(0)
    
    //设置温湿度上下限（特殊处理）
    if (strncmp(key, "TEMP_HIGH", 9) == 0) {
        App_StateLock(); float lo = g_app_state.config.temp_low_threshold; App_StateUnlock();
        if (fval < -20.0 || fval > 100.0 || fval <= (double)lo) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; }
        App_StateLock(); g_app_state.config.temp_high_threshold = (float)fval; App_StateUnlock();
        App_Config_MarkDirty();
        App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, 0, (float)fval, "set TEMP_HIGH=%.1f", fval);
        Cmd_Reply("[CMD] ok\r\n"); return;
    }   
    if (strncmp(key, "TEMP_LOW", 8) == 0) {
        App_StateLock(); float hi = g_app_state.config.temp_high_threshold; App_StateUnlock();
        if (fval < -40.0 || fval > 80.0 || fval >= (double)hi) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; }
        App_StateLock(); g_app_state.config.temp_low_threshold = (float)fval; App_StateUnlock();
        App_Config_MarkDirty();
        App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, 0, (float)fval, "set TEMP_LOW=%.1f", fval);
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    if (strncmp(key, "HUMI_HIGH", 9) == 0) {
        App_StateLock(); float lo = g_app_state.config.humi_low_threshold; App_StateUnlock();
        if (fval < 0.0 || fval > 100.0 || fval <= (double)lo) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; }
        App_StateLock(); g_app_state.config.humi_high_threshold = (float)fval; App_StateUnlock();
        App_Config_MarkDirty();
        App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, 0, (float)fval, "set HUMI_HIGH=%.1f", fval);
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    if (strncmp(key, "HUMI_LOW", 8) == 0) {
        App_StateLock(); float hi = g_app_state.config.humi_high_threshold; App_StateUnlock();
        if (fval < 0.0 || fval > 100.0 || fval >= (double)hi) { Cmd_Reply("[CMD] error: value out of range\r\n"); return; }
        App_StateLock(); g_app_state.config.humi_low_threshold = (float)fval; App_StateUnlock();
        App_Config_MarkDirty();
        App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, 0, (float)fval, "set HUMI_LOW=%.1f", fval);
        Cmd_Reply("[CMD] ok\r\n"); return;
    }


    if (strncmp(key, "CO2_HIGH",  8) == 0) SET_U16_RANGE(co2_high_threshold,   400,   10000);
    if (strncmp(key, "LIGHT_HIGH",10) == 0) SET_U16_RANGE(light_high_threshold,   0,   65535);
    if (strncmp(key, "LIGHT_LOW", 9) == 0)  SET_U16_RANGE(light_low_threshold,    0,   65535);
    if (strncmp(key, "VIN_LOW",   7) == 0)  SET_FLOAT_RANGE(vin_low_threshold,  5.0,    24.0);
    if (strncmp(key, "AIN1_HIGH", 9) == 0)  SET_FLOAT_RANGE(ain1_high_threshold, 0.0,    3.3);
    if (strncmp(key, "AIN2_HIGH", 9) == 0)  SET_FLOAT_RANGE(ain2_high_threshold, 0.0,    3.3);
    if (strncmp(key, "UPLOAD",    6) == 0)  SET_U32_RANGE(upload_period_ms,      200,  60000);
    if (strncmp(key, "POLL",      4) == 0)  SET_U32_RANGE(sensor_poll_interval_ms, 100, 10000);
    if (strncmp(key, "BACKLIGHT", 9) == 0)  SET_U32_RANGE(backlight_timeout_ms, 5000, 600000);
    if (strncmp(key, "DEVICE_ID", 9) == 0)  SET_U8_RANGE(device_id,               1,     255);
    if (strncmp(key, "ALARM_ENABLE",    12) == 0) SET_U8_RANGE(alarm_enable,      0, 1);
    if (strncmp(key, "BUZZER_ENABLE",   13) == 0) SET_U8_RANGE(buzzer_enable,     0, 1);
    if (strncmp(key, "LED_ALARM_ENABLE",16) == 0) SET_U8_RANGE(led_alarm_enable,  0, 1);

    Cmd_Reply("[CMD] error: invalid parameter\r\n");
    App_EventLog_Add(APP_EVENT_CMD_ERROR, 0U, 0, 0.0f, "invalid param: %.32s", line_upper);
}

/* ---- print one log item ---- */
static void Cmd_PrintLogItem(const AppEventLogItem_t *e)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
        "[LOG] #%lu tick=%lu type=%s src=%u i=%ld f=%.2f msg=%s\r\n",
        (unsigned long)e->id, (unsigned long)e->tick,
        App_EventLog_TypeToString(e->type),
        (unsigned)e->source, (long)e->value_i,
        (double)e->value_f, e->message);
    Cmd_Reply(buf);
}

/* ---- GET LOG [N | ALARM | COMM] ---- */
//从事件日志里取出对应日志
static void Cmd_GetLog(const char *args)
{
    static const AppEventType_t s_alarm_types[] = {
        APP_EVENT_ALARM_ACTIVE, APP_EVENT_ALARM_CLEARED, APP_EVENT_ALARM_MUTED
    };
    static const AppEventType_t s_comm_types[] = {
        APP_EVENT_COMM_OFFLINE, APP_EVENT_COMM_RECOVERED,
        APP_EVENT_COMM_CRC_ERROR, APP_EVENT_COMM_PROTO_ERROR,
        APP_EVENT_SENSOR_DATA_INVALID
    };

    uint16_t total;
    if (strncmp(args, "ALARM", 5) == 0) {
        total = App_EventLog_CopyFiltered(s_log_snap, APP_EVENT_LOG_CAPACITY,
            s_alarm_types, (uint8_t)(sizeof(s_alarm_types)/sizeof(s_alarm_types[0])));
    } else if (strncmp(args, "COMM", 4) == 0) {
        total = App_EventLog_CopyFiltered(s_log_snap, APP_EVENT_LOG_CAPACITY,
            s_comm_types, (uint8_t)(sizeof(s_comm_types)/sizeof(s_comm_types[0])));
    } else if (*args >= '1' && *args <= '9') {//取出指定数量的日志
        long n = strtol(args, NULL, 10);
        uint16_t req = (n > 0 && (uint32_t)n <= (uint32_t)APP_EVENT_LOG_CAPACITY) ? (uint16_t)n : APP_EVENT_LOG_CAPACITY;//判断请求数量是否合法
        total = App_EventLog_CopyLatest(s_log_snap, req);
    } else {
        total = App_EventLog_CopyLatest(s_log_snap, APP_EVENT_LOG_CAPACITY);//复制全部日志
    }

    if (total == 0U) { Cmd_Reply("[CMD] log empty\r\n"); return; }

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "[CMD] log count=%u\r\n", (unsigned)total);
    Cmd_Reply(hdr);
    for (uint16_t i = 0U; i < total; i++) { Cmd_PrintLogItem(&s_log_snap[i]); }
}

/* ---- CLEAR LOG ---- */
//清楚系统运行时保存的日志（不是清除Flash所保存的）
static void Cmd_ClearLog(void)
{
    App_EventLog_Clear();
    Cmd_Reply("[CMD] log cleared\r\n");
}

/* ---- GET FLASH ID ---- */
static void Cmd_GetFlashId(void)
{
    uint32_t id = BSP_W25Q64_ReadID();
    char buf[64];
    if (id == 0U || id == 0xFFFFFFUL) {
        Cmd_Reply("[CMD] flash id invalid or not detected\r\n");
    } else {
        snprintf(buf, sizeof(buf), "[CMD] W25Q64 JEDEC ID=0x%06lX\r\n", (unsigned long)id);
        Cmd_Reply(buf);
    }
}

/* ---- GET FLASH LOG STAT ---- */
static void Cmd_GetFlashLogStat(void)
{
    char buf[96];
    snprintf(buf, sizeof(buf),
        "[CMD] flash_log records=%lu next=0x%06lX size=%lu\r\n",
        (unsigned long)App_EventLog_StorageGetCount(),
        (unsigned long)App_EventLog_StorageGetNextAddr(),
        (unsigned long)EVENT_LOG_FLASH_SIZE);
    Cmd_Reply(buf);
}

/* ---- GET FLASH LOG [N] ---- */
#define FLASH_LOG_BATCH 32U
static AppEventFlashRecord_t s_flash_snap[FLASH_LOG_BATCH];

static void Cmd_GetFlashLog(const char *args)
{
    uint16_t req = 16U;
    if (*args >= '1' && *args <= '9') {
        long n = strtol(args, NULL, 10);
        req = (n > 0 && n <= (long)FLASH_LOG_BATCH) ? (uint16_t)n : FLASH_LOG_BATCH;
    }
    uint16_t count = App_EventLog_StorageReadLatest(s_flash_snap, req);
    if (count == 0U) { Cmd_Reply("[CMD] flash log empty\r\n"); return; }

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "[CMD] flash log count=%u\r\n", (unsigned)count);
    Cmd_Reply(hdr);

    char buf[160];
    for (uint16_t i = 0U; i < count; i++) {
        const AppEventFlashRecord_t *r = &s_flash_snap[i];
        snprintf(buf, sizeof(buf),
            "[FLOG] #%lu tick=%lu type=%lu src=%u i=%ld f=%.2f msg=%s\r\n",
            (unsigned long)r->id, (unsigned long)r->tick,
            (unsigned long)r->type, (unsigned)r->source,
            (long)r->value_i, (double)r->value_f, r->message);
        Cmd_Reply(buf);
    }
}

/* ---- GET BUS ---- */
static void Cmd_GetBus(void)
{
    App_StateLock();
    uint8_t th = g_app_state.config.temp_humi_bus;
    uint8_t hu = g_app_state.config.human_bus;
    uint8_t li = g_app_state.config.light_bus;
    uint8_t co = g_app_state.config.co2_bus;
    App_StateUnlock();
    char buf[80];
    snprintf(buf, sizeof(buf),
        "[CMD] BUS TH=%u HUMAN=%u LIGHT=%u CO2=%u\r\n",
        (unsigned)(th + 1U), (unsigned)(hu + 1U),
        (unsigned)(li + 1U), (unsigned)(co + 1U));
    Cmd_Reply(buf);
}

/* ---- SET BUS <sensor> <1|2> ---- */
static void Cmd_SetBus(const char *args)
{
    /* args points past "SET BUS " */
    const char *val = Str_NextToken(args);
    long bus = strtol(val, NULL, 10);
    if (bus < 1 || bus > 2) { Cmd_Reply("[CMD] error: invalid bus\r\n"); return; }
    uint8_t b = (uint8_t)(bus - 1L);

    App_StateLock();
    if      (strncmp(args, "TH",    2) == 0) g_app_state.config.temp_humi_bus = b;
    else if (strncmp(args, "HUMAN", 5) == 0) g_app_state.config.human_bus     = b;
    else if (strncmp(args, "LIGHT", 5) == 0) g_app_state.config.light_bus     = b;
    else if (strncmp(args, "CO2",   3) == 0) g_app_state.config.co2_bus       = b;
    else { App_StateUnlock(); Cmd_Reply("[CMD] error: invalid bus\r\n"); return; }
    App_StateUnlock();

    App_Config_MarkDirty();
    App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, (int32_t)bus, 0.0f,
                     "set BUS %.5s=%ld", args, bus);
    Cmd_Reply("[CMD] ok\r\n");
}
static void Cmd_GetLogStorage(void)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
        "[CMD] log_storage enabled=%u queued=%lu ok=%lu fail=%lu drop=%lu\r\n",
        (unsigned)App_LogStorage_IsEnabled(),
        (unsigned long)App_LogStorage_GetQueuedCount(),
        (unsigned long)App_LogStorage_GetPersistOkCount(),
        (unsigned long)App_LogStorage_GetPersistFailCount(),
        (unsigned long)App_LogStorage_GetDropCount());
    Cmd_Reply(buf);
}

/* ---- FLUSH LOG ---- */
static void Cmd_FlushLog(void)
{
    if (App_LogStorage_Flush(2000U)) {
        Cmd_Reply("[CMD] log storage flushed\r\n");
    } else {
        Cmd_Reply("[CMD] log storage flush timeout\r\n");
    }
}

/* ---- CLEAR FLASH LOG ---- */
static void Cmd_ClearFlashLog(void)
{
    App_EventLog_StorageClear();
    Cmd_Reply("[CMD] flash log cleared\r\n");
}

/* ---- LCD TEST ---- */
static void Cmd_LcdTest(void)
{
    Cmd_Reply("[CMD] lcd test pattern\r\n");
    BSP_LCD_TestPattern();
}

/* ---- LVGL TEST ---- */
static void Cmd_LvglTest(void)
{
#if APP_USE_LVGL
    App_Display_RequestRefresh();
    Cmd_Reply("[CMD] lvgl test refresh\r\n");
#else
    Cmd_Reply("[CMD] lvgl disabled\r\n");
#endif
}

/* ---- ESP commands ---- */
//测试ESP32 AT指令：发送：AT  期望回复：OK 
static void Cmd_EspAT(void)
{
    Cmd_Reply(App_ESP32_AT_Test() ? "[CMD] esp at ok\r\n" : "[CMD] esp at failed\r\n");
}

static void Cmd_EspReset(void)
{
    Cmd_Reply("[CMD] esp reset\r\n");
    App_ESP32_ResetModule();
}

//获取ESP32固件版本号
static void Cmd_EspVer(void)
{
    App_ESP32_GetVersion();
}

//往ESP32发送AT命令并返回ESP32执行命令的结果
static void Cmd_EspRaw(const char *cmd)
{
    char buf[256];
    App_ESP32_SendATCommand(cmd, "OK", buf, sizeof(buf), 2000U);
    BSP_Log_Printf("[ESP32] response:\r\n%s\r\n", buf);
}

/* ---- TOUCH commands ---- */
//读取LCD 触摸芯片驱动类型
static void Cmd_TouchInfo(void)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "[CMD] touch ic=%s\r\n",
             BSP_Touch_IcToString(App_Touch_GetIcType()));
    Cmd_Reply(buf);
}

static void Cmd_TouchTest(void)
{
    BSP_TouchPoint_t pt;
    App_Touch_GetLastPoint(&pt);
    char buf[64];
    if (pt.pressed) {
        snprintf(buf, sizeof(buf), "[CMD] touch pressed=1 x=%u y=%u points=%u\r\n",
                 (unsigned)pt.x, (unsigned)pt.y, (unsigned)pt.points);
    } else {
        snprintf(buf, sizeof(buf), "[CMD] touch pressed=0\r\n");
    }
    Cmd_Reply(buf);
}

static void Cmd_TouchReset(void)
{
    Cmd_Reply("[CMD] touch reset\r\n");
    BSP_Touch_Reset();
    BSP_TouchIc_t ic = BSP_Touch_DetectIc();
    char buf[48];
    snprintf(buf, sizeof(buf), "[CMD] touch ic=%s\r\n", BSP_Touch_IcToString(ic));
    Cmd_Reply(buf);
}

/* ---- WiFi commands ---- */
static void Cmd_GetWifi(void)
{
    App_StateLock();
    char ssid[32];
    strncpy(ssid, g_app_state.config.wifi_ssid, sizeof(ssid) - 1U);
    ssid[sizeof(ssid) - 1U] = '\0';
    App_StateUnlock();
    char buf[64];
    snprintf(buf, sizeof(buf), "[CMD] wifi ssid=%s pass=******\r\n", ssid);
    Cmd_Reply(buf);
}

//设置WiFi名称
static void Cmd_SetWifiSsid(const char *val)
{
    if (val == NULL || *val == '\0') { Cmd_Reply("[CMD] error: empty ssid\r\n"); return; }
    App_StateLock();
    strncpy(g_app_state.config.wifi_ssid, val, sizeof(g_app_state.config.wifi_ssid) - 1U);
    g_app_state.config.wifi_ssid[sizeof(g_app_state.config.wifi_ssid) - 1U] = '\0';
    App_StateUnlock();
    App_Config_MarkDirty();
    Cmd_Reply("[CMD] ok\r\n");
}

//设置WiFi密码
static void Cmd_SetWifiPass(const char *val)
{
    if (val == NULL) { val = ""; }
    App_StateLock();
    strncpy(g_app_state.config.wifi_password, val, sizeof(g_app_state.config.wifi_password) - 1U);
    g_app_state.config.wifi_password[sizeof(g_app_state.config.wifi_password) - 1U] = '\0';
    App_StateUnlock();
    App_Config_MarkDirty();
    Cmd_Reply("[CMD] ok\r\n");
}

/* ---- MQTT commands ---- */
static void Cmd_GetMqtt(void)
{
    App_StateLock();
    uint8_t  en      = g_app_state.config.mqtt_enable;
    uint8_t  cmd_en  = g_app_state.config.mqtt_cmd_enable;
    uint8_t  q_data  = g_app_state.config.mqtt_qos_data;
    uint8_t  q_alarm = g_app_state.config.mqtt_qos_alarm;
    uint8_t  q_stat  = g_app_state.config.mqtt_qos_status;
    uint8_t  q_resp  = g_app_state.config.mqtt_qos_resp;
    uint8_t  r_data  = g_app_state.config.mqtt_retain_data;
    uint8_t  r_alarm = g_app_state.config.mqtt_retain_alarm;
    uint8_t  r_stat  = g_app_state.config.mqtt_retain_status;
    uint8_t  r_resp  = g_app_state.config.mqtt_retain_resp;
    char     host[64];
    uint16_t port    = g_app_state.config.mqtt_port;
    char     cid[32];
    char     td[64], ta[64], ts[64], tc[64], tr[64];
    strncpy(host, g_app_state.config.mqtt_host,         sizeof(host) - 1U);
    strncpy(cid,  g_app_state.config.mqtt_client_id,    sizeof(cid)  - 1U);
    strncpy(td,   g_app_state.config.mqtt_topic_data,   sizeof(td)   - 1U);
    strncpy(ta,   g_app_state.config.mqtt_topic_alarm,  sizeof(ta)   - 1U);
    strncpy(ts,   g_app_state.config.mqtt_topic_status, sizeof(ts)   - 1U);
    strncpy(tc,   g_app_state.config.mqtt_topic_cmd,    sizeof(tc)   - 1U);
    strncpy(tr,   g_app_state.config.mqtt_topic_resp,   sizeof(tr)   - 1U);
    host[sizeof(host)-1U]='\0'; cid[sizeof(cid)-1U]='\0';
    td[sizeof(td)-1U]='\0'; ta[sizeof(ta)-1U]='\0'; ts[sizeof(ts)-1U]='\0';
    tc[sizeof(tc)-1U]='\0'; tr[sizeof(tr)-1U]='\0';
    App_StateUnlock();

    AppESP32Status_t esp_st;
    App_ESP32_GetStatus(&esp_st);

    char buf[128];
    snprintf(buf, sizeof(buf), "[CMD] mqtt enable=%u port=%u\r\n", (unsigned)en, (unsigned)port);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt host=%s client=%s\r\n", host, cid);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] topic data=%s\r\n", td);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] topic alarm=%s\r\n", ta);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] topic status=%s\r\n", ts);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] topic cmd=%s\r\n", tc);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] topic resp=%s\r\n", tr);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt qos data=%u alarm=%u status=%u resp=%u\r\n",
             (unsigned)q_data, (unsigned)q_alarm, (unsigned)q_stat, (unsigned)q_resp);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt retain data=%u alarm=%u status=%u resp=%u\r\n",
             (unsigned)r_data, (unsigned)r_alarm, (unsigned)r_stat, (unsigned)r_resp);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] cmd_enable=%u subscribed=%u token=******\r\n",
             (unsigned)cmd_en, (unsigned)esp_st.mqtt_subscribed);
    Cmd_Reply(buf);
}

static void Cmd_SetMqtt(const char *args, const char *args_raw)
{
    /* args: HOST <v> | PORT <v> | CLIENT <v> | USER <v> | PASS <v> |
             ENABLE <0|1> | QOS [DATA|ALARM|STATUS|RESP] <0|1> |
             RETAIN [DATA|ALARM|STATUS|RESP] <0|1> |
             TOPIC DATA <v> | TOPIC ALARM <v> | TOPIC STATUS <v> 
    识别 MQTT 子参数 / 校验参数是否合法 / 写入 g_app_state.config / 标记配置已修改 / 必要时触发 ESP32 连接/断开 / 回复命令执行结果       
    */

    const char *val = Str_NextToken(args);
    const char *val_raw = Str_NextToken(args_raw);
    char buf[80];

    //设置 MQTT 服务器地址
    if (strncmp(args, "HOST", 4) == 0) {
        if (*val_raw == '\0') { Cmd_Reply("[CMD] error: empty host\r\n"); return; }
        App_StateLock();
        strncpy(g_app_state.config.mqtt_host, val_raw, sizeof(g_app_state.config.mqtt_host) - 1U);
        g_app_state.config.mqtt_host[sizeof(g_app_state.config.mqtt_host) - 1U] = '\0';
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    //设置 MQTT 端口
    if (strncmp(args, "PORT", 4) == 0) {
        long p = strtol(val, NULL, 10);
        if (p < 1 || p > 65535) { Cmd_Reply("[CMD] error: invalid port\r\n"); return; }
        App_StateLock();
        g_app_state.config.mqtt_port = (uint16_t)p;
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    //设置 Client ID
    if (strncmp(args, "CLIENT", 6) == 0) {
        if (*val_raw == '\0') { Cmd_Reply("[CMD] error: empty client id\r\n"); return; }
        App_StateLock();
        strncpy(g_app_state.config.mqtt_client_id, val_raw, sizeof(g_app_state.config.mqtt_client_id) - 1U);
        g_app_state.config.mqtt_client_id[sizeof(g_app_state.config.mqtt_client_id) - 1U] = '\0';
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    //设置 MQTT 用户名和密码
    if (strncmp(args, "USER", 4) == 0) {
        App_StateLock();
        strncpy(g_app_state.config.mqtt_username, val_raw, sizeof(g_app_state.config.mqtt_username) - 1U);
        g_app_state.config.mqtt_username[sizeof(g_app_state.config.mqtt_username) - 1U] = '\0';
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    if (strncmp(args, "PASS", 4) == 0) {
        App_StateLock();
        strncpy(g_app_state.config.mqtt_password, val_raw, sizeof(g_app_state.config.mqtt_password) - 1U);
        g_app_state.config.mqtt_password[sizeof(g_app_state.config.mqtt_password) - 1U] = '\0';
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    //启用或关闭 MQTT
    if (strncmp(args, "ENABLE", 6) == 0) {
        long en = strtol(val, NULL, 10);
        if (en != 0 && en != 1) { Cmd_Reply("[CMD] error: use 0 or 1\r\n"); return; }
        App_StateLock();
        g_app_state.config.mqtt_enable = (uint8_t)en;
        App_StateUnlock();
        App_Config_MarkDirty();
        if (en == 1) {
            App_ESP32_TriggerConnect();
        } else {
            App_ESP32_TriggerDisconnect();
        }
        snprintf(buf, sizeof(buf), "[CMD] mqtt enable=%ld\r\n", en);
        Cmd_Reply(buf); return;
    }
    //设置 QoS
    if (Str_TokenEquals(args, "QOS")) {
        Cmd_SetMqttQos(args);
        return;
    }
    //设置 Retain
    if (Str_TokenEquals(args, "RETAIN")) {
        Cmd_SetMqttRetain(args);
        return;
    }
    //设置 MQTT Topic
    if (strncmp(args, "TOPIC", 5) == 0) {
        const char *which = val;
        const char *which_raw = val_raw;
        const char *topic_raw = Str_NextToken(which_raw);
        if (*topic_raw == '\0') { Cmd_Reply("[CMD] error: empty topic\r\n"); return; }
        App_StateLock();
        if      (strncmp(which, "DATA",   4) == 0) {
            strncpy(g_app_state.config.mqtt_topic_data, topic_raw,
                    sizeof(g_app_state.config.mqtt_topic_data) - 1U);
            g_app_state.config.mqtt_topic_data[sizeof(g_app_state.config.mqtt_topic_data) - 1U] = '\0';
        } else if (strncmp(which, "ALARM",  5) == 0) {
            strncpy(g_app_state.config.mqtt_topic_alarm, topic_raw,
                    sizeof(g_app_state.config.mqtt_topic_alarm) - 1U);
            g_app_state.config.mqtt_topic_alarm[sizeof(g_app_state.config.mqtt_topic_alarm) - 1U] = '\0';
        } else if (strncmp(which, "STATUS", 6) == 0) {
            strncpy(g_app_state.config.mqtt_topic_status, topic_raw,
                    sizeof(g_app_state.config.mqtt_topic_status) - 1U);
            g_app_state.config.mqtt_topic_status[sizeof(g_app_state.config.mqtt_topic_status) - 1U] = '\0';
        } else if (strncmp(which, "CMD",    3) == 0) {
            strncpy(g_app_state.config.mqtt_topic_cmd, topic_raw,
                    sizeof(g_app_state.config.mqtt_topic_cmd) - 1U);
            g_app_state.config.mqtt_topic_cmd[sizeof(g_app_state.config.mqtt_topic_cmd) - 1U] = '\0';
        } else if (strncmp(which, "RESP",   4) == 0) {
            strncpy(g_app_state.config.mqtt_topic_resp, topic_raw,
                    sizeof(g_app_state.config.mqtt_topic_resp) - 1U);
            g_app_state.config.mqtt_topic_resp[sizeof(g_app_state.config.mqtt_topic_resp) - 1U] = '\0';
        } else {
            App_StateUnlock();
            Cmd_Reply("[CMD] error: invalid topic type\r\n"); return;
        }
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] ok\r\n"); return;
    }
    //设置TOKEN
    if (strncmp(args, "TOKEN", 5) == 0) {
        if (*val_raw == '\0') { Cmd_Reply("[CMD] error: empty token\r\n"); return; }
        if (strlen(val_raw) >= sizeof(g_app_state.config.mqtt_cmd_token)) {
            Cmd_Reply("[CMD] error: token too long\r\n"); return;
        }
        App_StateLock();
        strncpy(g_app_state.config.mqtt_cmd_token, val_raw,
                sizeof(g_app_state.config.mqtt_cmd_token) - 1U);
        g_app_state.config.mqtt_cmd_token[sizeof(g_app_state.config.mqtt_cmd_token) - 1U] = '\0';
        App_StateUnlock();
        App_Config_MarkDirty();
        Cmd_Reply("[CMD] token updated\r\n"); return;
    }
    //启用MQTT CMD命令功能
    if (strncmp(args, "CMD_ENABLE", 10) == 0) {
        long en = strtol(val, NULL, 10);
        if (en != 0 && en != 1) { Cmd_Reply("[CMD] error: use 0 or 1\r\n"); return; }
        App_StateLock();
        g_app_state.config.mqtt_cmd_enable = (uint8_t)en;
        App_StateUnlock();
        App_Config_MarkDirty();
        snprintf(buf, sizeof(buf), "[CMD] mqtt cmd_enable=%ld\r\n", en);
        Cmd_Reply(buf); return;
    }
    Cmd_Reply("[CMD] error: invalid mqtt parameter\r\n");
}

/* ---- MQTT operation commands ---- */
static void Cmd_MqttConnect(void)
{
    App_ESP32_TriggerConnect();
    Cmd_Reply("[CMD] mqtt connect triggered\r\n");
}

static void Cmd_MqttDisconnect(void)
{
    App_ESP32_MQTT_Disconnect();
    Cmd_Reply("[CMD] mqtt disconnect sent\r\n");
}

static void Cmd_MqttPubTest(void)
{
    if (App_ESP32_GetNetState() != ESP32_APP_STATE_MQTT_CONNECTED) {
        Cmd_Reply("[CMD] error: mqtt not connected\r\n");
        return;
    }
    App_StateLock();
    char topic[64];
    strncpy(topic, g_app_state.config.mqtt_topic_status, sizeof(topic) - 1U);
    topic[sizeof(topic) - 1U] = '\0';
    App_StateUnlock();

    uint8_t qos    = App_Upload_GetMqttQosForType(MQTT_MSG_TEST);
    uint8_t retain = App_Upload_GetMqttRetainForType(MQTT_MSG_TEST);
    const char *payload = "{\"type\":\"test\",\"msg\":\"hello from stm32\"}\r\n";
    uint8_t ok = App_ESP32_MQTT_Publish(topic, payload, qos, retain);
    Cmd_Reply(ok ? "[CMD] mqtt pub test ok\r\n" : "[CMD] mqtt pub test failed\r\n");
}

static void Cmd_MqttStatus(void)
{
    AppESP32Status_t esp_st;
    App_ESP32_GetStatus(&esp_st);

    App_StateLock();
    char     host[64];
    uint16_t port      = g_app_state.config.mqtt_port;
    char     cid[32];
    char     usr[32];
    char     td[64], ta[64], ts[64], tc[64], tr[64];
    uint8_t  en        = g_app_state.config.mqtt_enable;
    uint8_t  cmd_en    = g_app_state.config.mqtt_cmd_enable;
    uint8_t  q_data    = g_app_state.config.mqtt_qos_data;
    uint8_t  q_alarm   = g_app_state.config.mqtt_qos_alarm;
    uint8_t  q_stat    = g_app_state.config.mqtt_qos_status;
    uint8_t  q_resp    = g_app_state.config.mqtt_qos_resp;
    uint8_t  r_data    = g_app_state.config.mqtt_retain_data;
    uint8_t  r_alarm   = g_app_state.config.mqtt_retain_alarm;
    uint8_t  r_stat    = g_app_state.config.mqtt_retain_status;
    uint8_t  r_resp    = g_app_state.config.mqtt_retain_resp;
    strncpy(host, g_app_state.config.mqtt_host,         sizeof(host) - 1U);
    strncpy(cid,  g_app_state.config.mqtt_client_id,    sizeof(cid)  - 1U);
    strncpy(usr,  g_app_state.config.mqtt_username,     sizeof(usr)  - 1U);
    strncpy(td,   g_app_state.config.mqtt_topic_data,   sizeof(td)   - 1U);
    strncpy(ta,   g_app_state.config.mqtt_topic_alarm,  sizeof(ta)   - 1U);
    strncpy(ts,   g_app_state.config.mqtt_topic_status, sizeof(ts)   - 1U);
    strncpy(tc,   g_app_state.config.mqtt_topic_cmd,    sizeof(tc)   - 1U);
    strncpy(tr,   g_app_state.config.mqtt_topic_resp,   sizeof(tr)   - 1U);
    host[sizeof(host)-1U]='\0'; cid[sizeof(cid)-1U]='\0'; usr[sizeof(usr)-1U]='\0';
    td[sizeof(td)-1U]='\0'; ta[sizeof(ta)-1U]='\0'; ts[sizeof(ts)-1U]='\0';
    tc[sizeof(tc)-1U]='\0'; tr[sizeof(tr)-1U]='\0';
    App_StateUnlock();

    MqttCmdStats_t cs;
    App_MqttCmd_GetStats(&cs);

    char buf[128];
    snprintf(buf, sizeof(buf), "[CMD] mqtt enable=%u port=%u\r\n",
             (unsigned)en, (unsigned)port);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt qos data=%u alarm=%u status=%u resp=%u\r\n",
             (unsigned)q_data, (unsigned)q_alarm, (unsigned)q_stat, (unsigned)q_resp);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt retain data=%u alarm=%u status=%u resp=%u\r\n",
             (unsigned)r_data, (unsigned)r_alarm, (unsigned)r_stat, (unsigned)r_resp);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt host=%s\r\n", host);          Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt client=%s user=%s\r\n", cid, usr); Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt state=%s at=%u wifi=%u mqtt=%u subscribed=%u\r\n",
             App_ESP32_StateToString(esp_st.app_state),
             (unsigned)esp_st.at_ok,
             (unsigned)esp_st.wifi_connected,
             (unsigned)esp_st.mqtt_connected,
             (unsigned)esp_st.mqtt_subscribed);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt attempts=%lu wifi_fail=%lu mqtt_fail=%lu pub_fail=%lu\r\n",
             (unsigned long)esp_st.connect_attempts,
             (unsigned long)esp_st.wifi_fail_count,
             (unsigned long)esp_st.mqtt_fail_count,
             (unsigned long)esp_st.publish_fail_count);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt last_ok=%lu last_error=%lu\r\n",
             (unsigned long)esp_st.last_ok_tick,
             (unsigned long)esp_st.last_error_tick);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt topic_data=%s\r\n", td);   Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt topic_alarm=%s\r\n", ta);  Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt topic_status=%s\r\n", ts); Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt topic_cmd=%s\r\n", tc);    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] mqtt topic_resp=%s\r\n", tr);   Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] cmd_enable=%u rx=%lu ok=%lu fail=%lu auth_fail=%lu\r\n",
             (unsigned)cmd_en,
             (unsigned long)cs.rx_count, (unsigned long)cs.ok_count,
             (unsigned long)cs.fail_count, (unsigned long)cs.auth_fail_count);
    Cmd_Reply(buf);
}

/* ---- UPLOAD commands ---- */
//获取系统上传信息
static void Cmd_GetUpload(void)
{
    static const char *s_target_str[] = { "NONE", "USART1", "MQTT", "USART1_AND_MQTT" };

    UploadStats_t st;
    App_Upload_GetStats(&st);
    AppESP32Status_t esp_st;
    App_ESP32_GetStatus(&esp_st);
    UploadFormat_t fmt = App_Cmd_GetUploadFormat();

    App_StateLock();
    uint32_t period_ms = g_app_state.config.upload_period_ms;
    uint8_t  q_data    = g_app_state.config.mqtt_qos_data;
    uint8_t  q_alarm   = g_app_state.config.mqtt_qos_alarm;
    uint8_t  q_stat    = g_app_state.config.mqtt_qos_status;
    uint8_t  q_resp    = g_app_state.config.mqtt_qos_resp;
    uint8_t  r_data    = g_app_state.config.mqtt_retain_data;
    uint8_t  r_alarm   = g_app_state.config.mqtt_retain_alarm;
    uint8_t  r_stat    = g_app_state.config.mqtt_retain_status;
    uint8_t  r_resp    = g_app_state.config.mqtt_retain_resp;
    App_StateUnlock();

    uint32_t tidx = (uint32_t)st.target;
    if (tidx > 3U) { tidx = 0U; }

    char buf[128];
    snprintf(buf, sizeof(buf),
        "[CMD] upload target=%s format=%s period=%lu\r\n",
        s_target_str[tidx],
        (fmt == UPLOAD_FORMAT_JSON) ? "JSON" : "TEXT",
        (unsigned long)period_ms);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf),
        "[CMD] upload tx=%lu fail=%lu uart_tx=%lu mqtt_tx=%lu mqtt_fail=%lu\r\n",
        (unsigned long)st.tx_count, (unsigned long)st.tx_fail_count,
        (unsigned long)st.uart_tx_count,
        (unsigned long)st.mqtt_tx_count, (unsigned long)st.mqtt_fail_count);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] upload qos data=%u alarm=%u status=%u resp=%u\r\n",
             (unsigned)q_data, (unsigned)q_alarm, (unsigned)q_stat, (unsigned)q_resp);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf), "[CMD] upload retain data=%u alarm=%u status=%u resp=%u\r\n",
             (unsigned)r_data, (unsigned)r_alarm, (unsigned)r_stat, (unsigned)r_resp);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf),
        "[CMD] upload last_error=%s wifi=%u mqtt=%u net=%s\r\n",
        App_Upload_ErrorToString(st.last_error),
        (unsigned)esp_st.wifi_connected,
        (unsigned)esp_st.mqtt_connected,
        App_ESP32_StateToString(esp_st.app_state));
    Cmd_Reply(buf);
}

//设置数据上传对象
static void Cmd_SetUploadTarget(const char *val)
{
    UploadTarget_t t;
    if      (strncmp(val, "USART1_AND_MQTT", 15) == 0) { t = UPLOAD_TARGET_USART1_AND_MQTT; }
    else if (strncmp(val, "USART1",  6) == 0)          { t = UPLOAD_TARGET_USART1; }
    else if (strncmp(val, "MQTT",    4) == 0)          { t = UPLOAD_TARGET_MQTT; }
    else if (strncmp(val, "NONE",    4) == 0)          { t = UPLOAD_TARGET_NONE; }
    else if (strncmp(val, "UART7",   5) == 0)          { Cmd_Reply("[CMD] error: UART7 is RS485; use USART1\r\n"); return; }
    else { Cmd_Reply("[CMD] error: invalid target\r\n"); return; }

    App_Upload_SetTarget(t);
    App_EventLog_Add(APP_EVENT_CONFIG_CHANGED, 0U, (int32_t)t, 0.0f,
                     "set UPLOAD TARGET=%s", val);
    Cmd_Reply("[CMD] ok\r\n");
}

/* ---- GET NET ---- */
//获取ESP32的通信状态
static void Cmd_GetNet(void)
{
    AppESP32Status_t st;
    App_ESP32_GetStatus(&st);

    App_StateLock();
    uint8_t cmd_en = g_app_state.config.mqtt_cmd_enable;
    char cmd_topic[64];
    char resp_topic[64];
    strncpy(cmd_topic,  g_app_state.config.mqtt_topic_cmd,  sizeof(cmd_topic) - 1U);
    strncpy(resp_topic, g_app_state.config.mqtt_topic_resp, sizeof(resp_topic) - 1U);
    cmd_topic[sizeof(cmd_topic) - 1U]   = '\0';
    resp_topic[sizeof(resp_topic) - 1U] = '\0';
    App_StateUnlock();

    char buf[128];
    snprintf(buf, sizeof(buf),
        "[CMD] net state=%s at=%u wifi=%u mqtt=%u subscribed=%u\r\n",
        App_ESP32_StateToString(st.app_state),
        (unsigned)st.at_ok,
        (unsigned)st.wifi_connected,
        (unsigned)st.mqtt_connected,
        (unsigned)st.mqtt_subscribed);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf),
        "[CMD] net attempts=%lu wifi_fail=%lu mqtt_fail=%lu pub_fail=%lu\r\n",
        (unsigned long)st.connect_attempts,
        (unsigned long)st.wifi_fail_count,
        (unsigned long)st.mqtt_fail_count,
        (unsigned long)st.publish_fail_count);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf),
        "[CMD] net cmd_enable=%u cmd_topic=%s\r\n",
        (unsigned)cmd_en, cmd_topic);
    Cmd_Reply(buf);
    snprintf(buf, sizeof(buf),
        "[CMD] net resp_topic=%s last_ok=%lu last_error=%lu\r\n",
        resp_topic,
        (unsigned long)st.last_ok_tick,
        (unsigned long)st.last_error_tick);
    Cmd_Reply(buf);
}

/* ---- NET TEST ---- */
static void Cmd_NetTest(void)
{
    uint8_t ok = App_ESP32_AT_Test();
    Cmd_Reply(ok ? "[CMD] net test at=ok\r\n" : "[CMD] net test at=fail\r\n");
}

/* ---- UPLOAD TEST ---- */
//USART1或 MQTT串口数据上传测试
static void Cmd_UploadTest(const char *target_str, UploadTarget_t tgt)
{
    char buf[128];
    int n = Protocol_Upload_FormatUploadTestJson(buf, sizeof(buf), target_str);
    if (n <= 0 || (uint16_t)n >= sizeof(buf)) {
        Cmd_Reply("[CMD] error: format failed\r\n"); return;
    }
    App_Upload_StripCrLf(buf);

    App_StateLock();
    char topic[64];
    strncpy(topic, g_app_state.config.mqtt_topic_status, sizeof(topic) - 1U);
    topic[sizeof(topic) - 1U] = '\0';
    App_StateUnlock();

    uint8_t qos    = App_Upload_GetMqttQosForType(MQTT_MSG_TEST);
    uint8_t retain = App_Upload_GetMqttRetainForType(MQTT_MSG_TEST);
    uint8_t ok = 1U;
    if (tgt == UPLOAD_TARGET_USART1 || tgt == UPLOAD_TARGET_USART1_AND_MQTT) {
        BSP_Log_Printf("%s\r\n", buf);
    }
    if (tgt == UPLOAD_TARGET_MQTT || tgt == UPLOAD_TARGET_USART1_AND_MQTT) {
        ok = App_ESP32_MQTT_Publish(topic, buf, qos, retain);
    }
    Cmd_Reply(ok ? "[CMD] upload test ok\r\n" : "[CMD] upload test failed\r\n");
}

/* ---- MQTT SUB / UNSUB / CMD TEST ---- */
static void Cmd_MqttSub(void)
{
    AppESP32Status_t esp_st;
    App_ESP32_GetStatus(&esp_st);
    if (!esp_st.mqtt_connected) {
        Cmd_Reply("[CMD] error: mqtt not connected\r\n");
        return;
    }

    App_StateLock();
    char    topic[64];
    strncpy(topic, g_app_state.config.mqtt_topic_cmd, sizeof(topic) - 1U);
    topic[sizeof(topic) - 1U] = '\0';
    App_StateUnlock();

    if (topic[0] == '\0') { Cmd_Reply("[CMD] error: mqtt_topic_cmd not set\r\n"); return; }
    uint8_t qos = App_Upload_GetMqttQosForType(MQTT_MSG_RESP);
    uint8_t ok = App_ESP32_MQTT_Subscribe(topic, qos);
    Cmd_Reply(ok ? "[CMD] mqtt sub ok\r\n" : "[CMD] mqtt sub failed\r\n");
}

static void Cmd_MqttUnsub(void)
{
    AppESP32Status_t esp_st;
    App_ESP32_GetStatus(&esp_st);
    if (!esp_st.mqtt_connected) {
        Cmd_Reply("[CMD] error: mqtt not connected\r\n");
        return;
    }

    App_StateLock();
    char topic[64];
    strncpy(topic, g_app_state.config.mqtt_topic_cmd, sizeof(topic) - 1U);
    topic[sizeof(topic) - 1U] = '\0';
    App_StateUnlock();

    if (topic[0] == '\0') { Cmd_Reply("[CMD] error: mqtt_topic_cmd not set\r\n"); return; }
    uint8_t ok = App_ESP32_MQTT_Unsubscribe(topic);
    Cmd_Reply(ok ? "[CMD] mqtt unsub ok\r\n" : "[CMD] mqtt unsub failed\r\n");
}

//测试ESP32 AT MQTT指令
static void Cmd_MqttCmdTest(const char *payload)
{
    if (payload == NULL || payload[0] == '\0') {
        Cmd_Reply("[CMD] error: usage: MQTT CMD TEST TOKEN <tok> <cmd>\r\n");
        return;
    }
    App_MqttCmd_HandleDirect(payload);
    Cmd_Reply("[CMD] mqtt cmd test dispatched\r\n");
}

/* ---- process one complete line ---- */
//line：解析后的命名内容 ；raw：原始的命名内容备份
static void Cmd_ProcessLine(char *line, const char *line_raw)
{
    /* strip trailing \r\n */
    uint16_t len = (uint16_t)strlen(line);
    while (len > 0U && (line[len-1U] == '\r' || line[len-1U] == '\n')) {
        line[--len] = '\0';
    }
    if (len == 0U) { return; }

    char raw[CMD_LINE_MAX];
    strncpy(raw, line_raw, CMD_LINE_MAX - 1U);
    raw[CMD_LINE_MAX - 1U] = '\0';
    uint16_t raw_len = (uint16_t)strlen(raw);
    while (raw_len > 0U && (raw[raw_len-1U] == '\r' || raw[raw_len-1U] == '\n')) {
        raw[--raw_len] = '\0';
    }

    Str_ToUpper(line);

    if (strcmp(line, "HELP") == 0)                          { Cmd_Help();      return; }
    if (strcmp(line, "GET DATA") == 0)                      { Cmd_GetData();   return; }
    if (strcmp(line, "GET CONFIG") == 0)                    { Cmd_GetConfig(); return; }
    if (strcmp(line, "GET STATUS") == 0)                    { Cmd_GetStatus(); return; }
    if (strcmp(line, "GET FORMAT") == 0)                    { Cmd_GetFormat(); return; }
    if (strcmp(line, "SAVE") == 0)                          { Cmd_Save();      return; }
    if (strcmp(line, "DEFAULT") == 0)                       { Cmd_Default();   return; }
    if (strcmp(line, "MUTE") == 0)                          { Cmd_Mute();      return; }
    if (strncmp(line, "SET ", 4) == 0)                      { Cmd_Set(line, raw); return; }
    if (strcmp(line, "CLEAR LOG") == 0)                     { Cmd_ClearLog();  return; }
    if (strcmp(line, "GET LOG") == 0)                       { Cmd_GetLog("");  return; }
    if (strncmp(line, "GET LOG ", 8) == 0)                  { Cmd_GetLog(line + 8); return; }
    if (strcmp(line, "GET FLASH ID") == 0)                  { Cmd_GetFlashId(); return; }
    if (strcmp(line, "GET FLASH LOG STAT") == 0)            { Cmd_GetFlashLogStat(); return; }
    if (strcmp(line, "GET FLASH LOG") == 0)                 { Cmd_GetFlashLog(""); return; }
    if (strncmp(line, "GET FLASH LOG ", 14) == 0)           { Cmd_GetFlashLog(line + 14); return; }
    if (strcmp(line, "CLEAR FLASH LOG") == 0)               { Cmd_ClearFlashLog(); return; }
    if (strcmp(line, "GET BUS") == 0)                       { Cmd_GetBus(); return; }
    if (strncmp(line, "SET BUS ", 8) == 0)                  { Cmd_SetBus(line + 8); return; }
    if (strcmp(line, "GET LOG STORAGE") == 0)               { Cmd_GetLogStorage(); return; }
    if (strcmp(line, "FLUSH LOG") == 0)                     { Cmd_FlushLog(); return; }
    if (strcmp(line, "LCD TEST") == 0)                      { Cmd_LcdTest();  return; }
    if (strcmp(line, "LVGL TEST") == 0)                     { Cmd_LvglTest(); return; }
    if (strcmp(line, "ESP AT") == 0)                        { Cmd_EspAT();    return; }
    if (strcmp(line, "ESP RESET") == 0)                     { Cmd_EspReset(); return; }
    if (strcmp(line, "ESP VER") == 0)                       { Cmd_EspVer();   return; }
    if (strncmp(line, "ESP RAW ", 8) == 0)                  { Cmd_EspRaw(raw + 8); return; }
    if (strcmp(line, "TOUCH INFO") == 0)                    { Cmd_TouchInfo();  return; }
    if (strcmp(line, "TOUCH TEST") == 0)                    { Cmd_TouchTest();  return; }
    if (strcmp(line, "TOUCH RESET") == 0)                   { Cmd_TouchReset(); return; }
    /* WiFi */
    if (strcmp(line, "GET WIFI") == 0)                      { Cmd_GetWifi(); return; }
    if (strncmp(line, "SET WIFI SSID ", 14) == 0)           { Cmd_SetWifiSsid(raw + 14); return; }
    if (strncmp(line, "SET WIFI PASS ", 14) == 0)           { Cmd_SetWifiPass(raw + 14); return; }
    /* MQTT config */
    if (strcmp(line, "GET MQTT") == 0)                      { Cmd_GetMqtt(); return; }
    if (strncmp(line, "SET MQTT ", 9) == 0)                 { Cmd_SetMqtt(line + 9, raw + 9); return; }
    /* MQTT operations */
    if (strcmp(line, "MQTT CONNECT") == 0)                  { Cmd_MqttConnect();    return; }
    if (strcmp(line, "MQTT DISCONNECT") == 0)               { Cmd_MqttDisconnect(); return; }
    if (strcmp(line, "MQTT PUB TEST") == 0)                 { Cmd_MqttPubTest();    return; }
    if (strcmp(line, "MQTT STATUS") == 0)                   { Cmd_MqttStatus();     return; }
    if (strcmp(line, "MQTT SUB") == 0)                      { Cmd_MqttSub();        return; }
    if (strcmp(line, "MQTT UNSUB") == 0)                    { Cmd_MqttUnsub();      return; }
    if (strncmp(line, "MQTT CMD TEST ", 14) == 0)           { Cmd_MqttCmdTest(raw + 14); return; }
    /* Upload target */
    if (strcmp(line, "GET UPLOAD") == 0)                    { Cmd_GetUpload(); return; }
    if (strncmp(line, "SET UPLOAD TARGET ", 18) == 0)       { Cmd_SetUploadTarget(line + 18); return; }
    /* Network diagnostics */
    if (strcmp(line, "GET NET") == 0)                       { Cmd_GetNet(); return; }
    if (strcmp(line, "NET TEST") == 0)                      { Cmd_NetTest(); return; }
    /* Upload test */
    if (strcmp(line, "UPLOAD TEST USART1") == 0)            { Cmd_UploadTest("USART1", UPLOAD_TARGET_USART1); return; }
    if (strcmp(line, "UPLOAD TEST UART7") == 0)             { Cmd_Reply("[CMD] error: UART7 is RS485; use UPLOAD TEST USART1\r\n"); return; }
    if (strcmp(line, "UPLOAD TEST MQTT") == 0)              { Cmd_UploadTest("MQTT",  UPLOAD_TARGET_MQTT);   return; }
    if (strcmp(line, "UPLOAD TEST BOTH") == 0)              { Cmd_UploadTest("BOTH",  UPLOAD_TARGET_USART1_AND_MQTT); return; }

    Cmd_Reply("[CMD] error: unknown command\r\n");
}

/* ---- USART1 single-byte IT callback ---- */
/*
每次 USART1 收到 1 个字节，就进入这个函数，把字符逐个存入 s_rx_line；
当收到回车 \r 或换行 \n 时，认为一条命令接收完成，然后把整条命令放入 s_cmd_queue 命令队列，最后重新开启下一次串口中断接收。
*/
void App_Cmd_UartRxCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != huart1.Instance) { return; }

    char c = (char)s_rx_byte;

    if (c == '\r' || c == '\n') {
        if (s_rx_pos > 0U) {
            s_rx_line[s_rx_pos] = '\0';
            char line_copy[CMD_LINE_MAX];
            strncpy(line_copy, s_rx_line, CMD_LINE_MAX - 1U);
            line_copy[CMD_LINE_MAX - 1U] = '\0';
            s_rx_pos = 0U;
            osMessageQueuePut(s_cmd_queue, line_copy, 0U, 0U);
        }
    } else if (s_rx_pos < (CMD_LINE_MAX - 1U)) {
        s_rx_line[s_rx_pos++] = c;
    }

    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U);
}

/* ---- init ---- */
void App_Cmd_Init(void)
{
    s_cmd_queue = osMessageQueueNew(CMD_QUEUE_DEPTH, CMD_LINE_MAX, NULL);
    s_rx_pos    = 0U;
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U);
    BSP_Log_Printf("[CMD] uart1 rx start ok\r\n");
}

/* ---- task ---- */
void App_CmdTask(void *argument)
{
    (void)argument;
    char line[CMD_LINE_MAX];
    BSP_Log_Printf("[APP] CmdTask started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_CMD);
        if (osMessageQueueGet(s_cmd_queue, line, NULL, 9000U) == osOK) {
            App_Cmd_ExecuteLine(line, NULL);
        }
    }
}
