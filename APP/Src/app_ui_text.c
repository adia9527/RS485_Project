#include "app_ui_text.h"
#include "app_sensor_config.h"
#include "app_event_log.h"
#include "app_health.h"
#include "app_log_storage.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

static const char *Quality_Str(SensorDataQuality_t q)
{
    if (q == SENSOR_DATA_VALID) return "valid";
    if (q == SENSOR_DATA_STALE) return "stale";
    return "invalid";
}

static const char *AlarmSrc_Str(AlarmSource_t s)
{
    switch (s) {
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

static const char *SensorStatus_Str(SensorStatus_t s)
{
    switch (s) {
        case SENSOR_STATUS_ONLINE:     return "ONLINE";
        case SENSOR_STATUS_OFFLINE:    return "OFFLINE";
        case SENSOR_STATUS_TIMEOUT:    return "TIMEOUT";
        case SENSOR_STATUS_CRC_ERROR:  return "CRC_ERR";
        case SENSOR_STATUS_DATA_ERROR: return "DATA_ERR";
        default:                       return "UNKNOWN";
    }
}

static const char *AlarmLevel_Str(AlarmLevel_t level)
{
    if (level == ALARM_CRITICAL) {
        return "CRITICAL";
    }
    if (level == ALARM_WARN) {
        return "WARN";
    }
    return "NONE";
}

void App_UI_BuildPageText(AppUiPage_t page,
                          const AppState_t *snapshot,
                          char *buf,
                          uint16_t buf_size)
{
    if (buf == NULL || buf_size == 0U) {
        return;
    }

    buf[0] = '\0';
    if (snapshot == NULL) {
        snprintf(buf, buf_size, "State unavailable\n");
        return;
    }

    const SensorData_t *s = snapshot->sensors;
    const AdcData_t    *a = &snapshot->adc;
    const AlarmState_t *al = &snapshot->alarm;
    const SysConfig_t  *c  = &snapshot->config;
    size_t n = 0U;

#define APPEND(...) \
    do { \
        if (n < (size_t)buf_size) { \
            int _w = snprintf(&buf[n], (size_t)buf_size - n, __VA_ARGS__); \
            if (_w > 0) { \
                size_t _remain = (size_t)buf_size - n; \
                n += ((size_t)_w >= _remain) ? (_remain - 1U) : (size_t)_w; \
            } \
        } \
    } while (0)

    switch (page) {
    case APP_UI_PAGE_MAIN:
        APPEND("Temp : %.1f C\n",  (double)s[SENSOR_IDX_TH].values[0]);
        APPEND("Humi : %.1f %%\n", (double)s[SENSOR_IDX_TH].values[1]);
        APPEND("CO2  : %.0f ppm\n", (double)s[SENSOR_IDX_CO2].values[0]);
        APPEND("Light: %.0f lux\n", (double)s[SENSOR_IDX_LIGHT].values[0]);
        APPEND("Human: %s\n", s[SENSOR_IDX_HUMAN].values[0] > 0.5f ? "YES" : "NO");
        APPEND("Alarm: %s\n", al->active ? "ACTIVE" : "CLEAR");
        break;

    case APP_UI_PAGE_SENSOR:
        APPEND("TH    addr=%u bus=%u %s %s\n",
               c->temp_humi_addr, c->temp_humi_bus,
               SensorStatus_Str(s[SENSOR_IDX_TH].status),
               Quality_Str(s[SENSOR_IDX_TH].quality));
        APPEND("HUMAN addr=%u bus=%u %s %s\n",
               c->human_addr, c->human_bus,
               SensorStatus_Str(s[SENSOR_IDX_HUMAN].status),
               Quality_Str(s[SENSOR_IDX_HUMAN].quality));
        APPEND("LIGHT addr=%u bus=%u %s %s\n",
               c->light_addr, c->light_bus,
               SensorStatus_Str(s[SENSOR_IDX_LIGHT].status),
               Quality_Str(s[SENSOR_IDX_LIGHT].quality));
        APPEND("CO2   addr=%u bus=%u %s %s\n",
               c->co2_addr, c->co2_bus,
               SensorStatus_Str(s[SENSOR_IDX_CO2].status),
               Quality_Str(s[SENSOR_IDX_CO2].quality));
        break;

    case APP_UI_PAGE_COMM:
        for (uint8_t i = 0U; i < RS485_PORT_COUNT; i++) {
            APPEND("RS485-%u tx=%lu rx=%lu err=%lu to=%lu\n",
                   (unsigned)(i + 1U),
                   (unsigned long)snapshot->comm[i].tx_count,
                   (unsigned long)snapshot->comm[i].rx_count,
                   (unsigned long)snapshot->comm[i].err_count,
                   (unsigned long)snapshot->comm[i].timeout_count);
        }
        APPEND("TH fail=%u crc=%lu proto=%lu\n",
               s[SENSOR_IDX_TH].fail_count,
               (unsigned long)s[SENSOR_IDX_TH].crc_err_count,
               (unsigned long)s[SENSOR_IDX_TH].proto_err_count);
        APPEND("CO2 fail=%u crc=%lu proto=%lu\n",
               s[SENSOR_IDX_CO2].fail_count,
               (unsigned long)s[SENSOR_IDX_CO2].crc_err_count,
               (unsigned long)s[SENSOR_IDX_CO2].proto_err_count);
        break;

    case APP_UI_PAGE_ALARM:
        APPEND("State : %s\n", al->active ? "ACTIVE" : "CLEAR");
        APPEND("Source: %s\n", AlarmSrc_Str(al->source));
        APPEND("Level : %s\n", AlarmLevel_Str(al->level));
        APPEND("Value : %.1f\n", (double)al->current_value);
        APPEND("Limit : %.1f\n", (double)al->threshold);
        APPEND("Muted : %s\n", al->muted ? "YES" : "NO");
        break;

    case APP_UI_PAGE_ADC:
        APPEND("Vin : raw=%u %.2f V\n", a->raw[0], (double)a->voltage[0]);
        APPEND("AIN1: raw=%u %.2f V\n", a->raw[1], (double)a->voltage[1]);
        APPEND("AIN2: raw=%u %.2f V\n", a->raw[2], (double)a->voltage[2]);
        APPEND("LDR : raw=%u %.2f V\n", a->raw[3], (double)a->voltage[3]);
        break;

    case APP_UI_PAGE_SYSTEM: {
        AppHealthSnapshot_t hs;
        App_Health_GetSnapshot(&hs);
        APPEND("Device ID : %u\n", c->device_id);
        APPEND("Uptime   : %lu s\n", (unsigned long)(osKernelGetTickCount() / 1000U));
        APPEND("Heap     : %lu B\n", (unsigned long)hs.free_heap);
        APPEND("Health   : %s\n", hs.healthy ? "OK" : "FAULT");
        APPEND("Fault    : 0x%08lX\n", (unsigned long)hs.fault_mask);
        APPEND("Reset    : %s\n", App_Health_GetResetReasonString());
        break;
    }

    case APP_UI_PAGE_LOG:
        APPEND("RAM log   : %u/%u\n",
               App_EventLog_GetCount(), App_EventLog_GetCapacity());
        APPEND("Flash log : %lu\n",
               (unsigned long)App_EventLog_StorageGetCount());
        APPEND("Queue     : %lu\n",
               (unsigned long)App_LogStorage_GetQueuedCount());
        APPEND("Persist OK: %lu\n",
               (unsigned long)App_LogStorage_GetPersistOkCount());
        APPEND("Persist NG: %lu\n",
               (unsigned long)App_LogStorage_GetPersistFailCount());
        APPEND("Dropped   : %lu\n",
               (unsigned long)App_LogStorage_GetDropCount());
        break;

    default:
        APPEND("Unknown page\n");
        break;
    }

#undef APPEND

    buf[buf_size - 1U] = '\0';
}
