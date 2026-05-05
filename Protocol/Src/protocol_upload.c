#include "protocol_upload.h"
#include "app_sensor_config.h"
#include <stdio.h>

/* ---------- sensor status string ---------- */
static const char *s_status_str[] = {
    "UNKNOWN", "ONLINE", "OFFLINE", "TIMEOUT", "CRC_ERR", "DATA_ERR"
};

static const char *Status_Str(SensorStatus_t st)
{
    if ((uint32_t)st < 6U) { return s_status_str[(uint32_t)st]; }
    return "UNKNOWN";
}

/* ---------- alarm source string ---------- */
static const char *AlarmSrc_Str(AlarmSource_t src)
{
    switch (src) {
        case ALARM_SRC_TEMP_HIGH:    return "TEMP_HIGH";
        case ALARM_SRC_TEMP_LOW:     return "TEMP_LOW";
        case ALARM_SRC_HUMI_HIGH:    return "HUMI_HIGH";
        case ALARM_SRC_HUMI_LOW:     return "HUMI_LOW";
        case ALARM_SRC_CO2_HIGH:     return "CO2_HIGH";
        case ALARM_SRC_LIGHT_HIGH:   return "LIGHT_HIGH";
        case ALARM_SRC_LIGHT_LOW:    return "LIGHT_LOW";
        case ALARM_SRC_HUMAN_DETECT: return "HUMAN_DETECT";
        case ALARM_SRC_COMM_FAIL:    return "COMM_FAIL";
        case ALARM_SRC_POWER_LOW:    return "POWER_LOW";
        case ALARM_SRC_AIN1_HIGH:    return "AIN1_HIGH";
        case ALARM_SRC_AIN2_HIGH:    return "AIN2_HIGH";
        default:                     return "NONE";
    }
}

/* ========== DATA ========== */

int Protocol_Upload_FormatDataText(char *buf, uint16_t buf_size, const AppState_t *s)
{
    return snprintf(buf, buf_size,
        "[DATA] temp=%.1f,humi=%.1f,co2=%u,light=%u,human=%u,"
        "vin=%.2f,ain1=%.2f,ain2=%.2f,alarm=%u\r\n",
        (double)s->sensors[SENSOR_IDX_TH].values[0],
        (double)s->sensors[SENSOR_IDX_TH].values[1],
        (unsigned)s->sensors[SENSOR_IDX_CO2].values[0],
        (unsigned)s->sensors[SENSOR_IDX_LIGHT].values[0],
        (unsigned)s->sensors[SENSOR_IDX_HUMAN].values[0],
        (double)s->adc.voltage[0],
        (double)s->adc.voltage[1],
        (double)s->adc.voltage[2],
        (unsigned)s->alarm.active);
}

int Protocol_Upload_FormatDataJson(char *buf, uint16_t buf_size, const AppState_t *s)
{
    return snprintf(buf, buf_size,
        "{\"type\":\"data\",\"id\":%u,"
        "\"temp\":%.1f,\"humi\":%.1f,\"co2\":%u,\"light\":%u,\"human\":%u,"
        "\"vin\":%.2f,\"ain1\":%.2f,\"ain2\":%.2f,\"alarm\":%u}\r\n",
        (unsigned)s->config.device_id,
        (double)s->sensors[SENSOR_IDX_TH].values[0],
        (double)s->sensors[SENSOR_IDX_TH].values[1],
        (unsigned)s->sensors[SENSOR_IDX_CO2].values[0],
        (unsigned)s->sensors[SENSOR_IDX_LIGHT].values[0],
        (unsigned)s->sensors[SENSOR_IDX_HUMAN].values[0],
        (double)s->adc.voltage[0],
        (double)s->adc.voltage[1],
        (double)s->adc.voltage[2],
        (unsigned)s->alarm.active);
}

/* ========== COMM ========== */

int Protocol_Upload_FormatCommText(char *buf, uint16_t buf_size, const AppState_t *s)
{
    return snprintf(buf, buf_size,
        "[COMM] TH=%s,HUMAN=%s,LIGHT=%s,CO2=%s\r\n",
        Status_Str(s->sensors[SENSOR_IDX_TH].status),
        Status_Str(s->sensors[SENSOR_IDX_HUMAN].status),
        Status_Str(s->sensors[SENSOR_IDX_LIGHT].status),
        Status_Str(s->sensors[SENSOR_IDX_CO2].status));
}

int Protocol_Upload_FormatCommJson(char *buf, uint16_t buf_size, const AppState_t *s)
{
    return snprintf(buf, buf_size,
        "{\"type\":\"comm\",\"th\":\"%s\",\"human\":\"%s\","
        "\"light\":\"%s\",\"co2\":\"%s\"}\r\n",
        Status_Str(s->sensors[SENSOR_IDX_TH].status),
        Status_Str(s->sensors[SENSOR_IDX_HUMAN].status),
        Status_Str(s->sensors[SENSOR_IDX_LIGHT].status),
        Status_Str(s->sensors[SENSOR_IDX_CO2].status));
}

/* ========== ALARM ========== */

int Protocol_Upload_FormatAlarmText(char *buf, uint16_t buf_size, const AppState_t *s)
{
    return snprintf(buf, buf_size,
        "[ALARM_STATE] active=%u,src=%s,level=%u,muted=%u\r\n",
        (unsigned)s->alarm.active,
        AlarmSrc_Str(s->alarm.source),
        (unsigned)s->alarm.level,
        (unsigned)s->alarm.muted);
}

int Protocol_Upload_FormatAlarmJson(char *buf, uint16_t buf_size, const AppState_t *s)
{
    return snprintf(buf, buf_size,
        "{\"type\":\"alarm\",\"active\":%u,\"src\":\"%s\","
        "\"level\":%u,\"value\":%.1f,\"threshold\":%.1f,\"muted\":%u}\r\n",
        (unsigned)s->alarm.active,
        AlarmSrc_Str(s->alarm.source),
        (unsigned)s->alarm.level,
        (double)s->alarm.current_value,
        (double)s->alarm.threshold,
        (unsigned)s->alarm.muted);
}

int Protocol_Upload_FormatHeartbeatJson(char *buf, uint16_t buf_size,
                                        uint32_t uptime_s, uint8_t healthy)
{
    return snprintf(buf, buf_size,
        "{\"type\":\"heartbeat\",\"uptime\":%lu,\"healthy\":%u}\r\n",
        (unsigned long)uptime_s, (unsigned)healthy);
}

int Protocol_Upload_FormatUploadTestJson(char *buf, uint16_t buf_size, const char *target)
{
    return snprintf(buf, buf_size,
        "{\"type\":\"upload_test\",\"target\":\"%s\",\"msg\":\"test from stm32\"}\r\n",
        target ? target : "");
}

int Protocol_Upload_FormatStatusJson(char *buf, uint16_t buf_size, const AppState_t *s,
                                     const AppHealthSnapshot_t *hs)
{
    return snprintf(buf, buf_size,
        "{\"type\":\"status\",\"id\":%u,\"healthy\":%u,\"fault\":%lu,"
        "\"alarm\":%u,\"heap\":%lu,\"uptime\":%lu}\r\n",
        (unsigned)s->config.device_id,
        (unsigned)hs->healthy,
        (unsigned long)hs->fault_mask,
        (unsigned)s->alarm.active,
        (unsigned long)hs->free_heap,
        (unsigned long)hs->uptime_s);
}
