#include "sim_dashboard.h"
#include "app_sensor_config.h"
#include "lvgl.h"
#include <stdio.h>

#define SIM_LCD_WIDTH       240
#define SIM_LCD_HEIGHT      320
#define SIM_MARGIN_PX       6
#define SIM_TITLE_H_PX      22
#define SIM_STATUS_H_PX     18
#define SIM_ROW_COUNT       8U
#define SIM_VALUE_BUF_LEN   72U

typedef struct
{
    lv_obj_t *row;
    lv_obj_t *name;
    lv_obj_t *value;
} SimDashboardRow_t;

typedef struct
{
    lv_obj_t          *screen;
    lv_obj_t          *title;
    lv_obj_t          *content;
    lv_obj_t          *status;
    SimDashboardRow_t  rows[SIM_ROW_COUNT];
} SimDashboardUi_t;

static SimDashboardUi_t s_ui;

static const lv_color_t s_color_bg = { .red = 0x10, .green = 0x14, .blue = 0x18 };
static const lv_color_t s_color_row = { .red = 0x1A, .green = 0x20, .blue = 0x26 };
static const lv_color_t s_color_border = { .red = 0x30, .green = 0x38, .blue = 0x40 };
static const lv_color_t s_color_text = { .red = 0xE8, .green = 0xE8, .blue = 0xE8 };
static const lv_color_t s_color_dim = { .red = 0x9B, .green = 0xA7, .blue = 0xB2 };
static const lv_color_t s_color_ok = { .red = 0x8F, .green = 0xE3, .blue = 0x8B };
static const lv_color_t s_color_warn = { .red = 0xFF, .green = 0x8A, .blue = 0x8A };

static const char *Sim_QualityToString(SensorDataQuality_t quality)
{
    if (quality == SENSOR_DATA_VALID) {
        return "VALID";
    }
    if (quality == SENSOR_DATA_STALE) {
        return "STALE";
    }
    return "BAD";
}

static const char *Sim_SensorOnlineString(const SensorData_t *sensor)
{
    if (sensor->online != 0U) {
        return "ON";
    }
    if (sensor->status == SENSOR_STATUS_TIMEOUT) {
        return "TO";
    }
    if (sensor->status == SENSOR_STATUS_CRC_ERROR) {
        return "CRC";
    }
    if (sensor->status == SENSOR_STATUS_DATA_ERROR) {
        return "DATA";
    }
    if (sensor->status == SENSOR_STATUS_UNKNOWN) {
        return "UNK";
    }
    return "OFF";
}

static const char *Sim_AlarmSourceToString(AlarmSource_t source)
{
    switch (source) {
        case ALARM_SRC_TEMP_HIGH:    return "TEMP_HIGH";
        case ALARM_SRC_TEMP_LOW:     return "TEMP_LOW";
        case ALARM_SRC_HUMI_HIGH:    return "HUMI_HIGH";
        case ALARM_SRC_HUMI_LOW:     return "HUMI_LOW";
        case ALARM_SRC_CO2_HIGH:     return "CO2_HIGH";
        case ALARM_SRC_LIGHT_HIGH:   return "LIGHT_HIGH";
        case ALARM_SRC_LIGHT_LOW:    return "LIGHT_LOW";
        case ALARM_SRC_HUMAN_DETECT: return "HUMAN";
        case ALARM_SRC_COMM_FAIL:    return "COMM_FAIL";
        case ALARM_SRC_POWER_LOW:    return "POWER_LOW";
        case ALARM_SRC_AIN1_HIGH:    return "AIN1_HIGH";
        case ALARM_SRC_AIN2_HIGH:    return "AIN2_HIGH";
        default:                     return "NONE";
    }
}

static const char *Sim_AlarmLevelToString(AlarmLevel_t level)
{
    if (level == ALARM_CRITICAL) {
        return "CRITICAL";
    }
    if (level == ALARM_WARN) {
        return "ALARM";
    }
    return "NONE";
}

static const char *Sim_UploadTargetToString(SimUploadTarget_t target)
{
    switch (target) {
        case SIM_UPLOAD_TARGET_NONE:          return "NONE";
        case SIM_UPLOAD_TARGET_UART:          return "UART";
        case SIM_UPLOAD_TARGET_MQTT:          return "MQTT";
        case SIM_UPLOAD_TARGET_UART_AND_MQTT: return "UART+MQTT";
        default:                              return "UNKNOWN";
    }
}

static void Sim_PrepareRows(uint8_t count)
{
    int32_t content_h;
    int32_t gap;
    int32_t row_h;

    if (count > SIM_ROW_COUNT) {
        count = SIM_ROW_COUNT;
    }

    content_h = SIM_LCD_HEIGHT - SIM_TITLE_H_PX - SIM_STATUS_H_PX - (SIM_MARGIN_PX * 4);
    gap = 4;
    row_h = (count > 0U) ? ((content_h - ((int32_t)(count - 1U) * gap)) / (int32_t)count) : 0;
    if (row_h > 38) {
        row_h = 38;
    }
    if (row_h < 26) {
        row_h = 26;
    }

    for (uint8_t i = 0U; i < SIM_ROW_COUNT; i++) {
        SimDashboardRow_t *row = &s_ui.rows[i];
        if (i < count) {
            lv_obj_clear_flag(row->row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(row->row, 0, (int32_t)i * (row_h + gap));
            lv_obj_set_size(row->row, SIM_LCD_WIDTH - (SIM_MARGIN_PX * 2), row_h);
        } else {
            lv_obj_add_flag(row->row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void Sim_SetRow(uint8_t idx, const char *name, const char *value, lv_color_t value_color)
{
    if (idx >= SIM_ROW_COUNT) {
        return;
    }

    lv_label_set_text(s_ui.rows[idx].name, name);
    lv_label_set_text(s_ui.rows[idx].value, value);
    lv_obj_set_style_text_color(s_ui.rows[idx].value, value_color, LV_PART_MAIN);
}

static void Sim_SetRowText(uint8_t idx, const char *name, const char *value)
{
    Sim_SetRow(idx, name, value, s_color_text);
}

static void Sim_SetTitle(SimPage_t page, const char *title)
{
    char status[48];
    uint8_t page_idx = (uint8_t)page;

    if (page_idx >= SIM_PAGE_COUNT) {
        page_idx = 0U;
    }

    lv_label_set_text(s_ui.title, title);
    snprintf(status, sizeof(status), "%u/%u  N Next  P Prev  Q Quit",
             (unsigned)(page_idx + 1U), (unsigned)SIM_PAGE_COUNT);
    lv_label_set_text(s_ui.status, status);
}

static void Sim_UpdateMain(const SimAppState_t *state)
{
    const SensorData_t *s = state->app.sensors;
    const AlarmState_t *alarm = &state->app.alarm;
    char value[SIM_VALUE_BUF_LEN];

    Sim_SetTitle(SIM_PAGE_MAIN, "ENV TERMINAL");
    Sim_PrepareRows(6U);

    snprintf(value, sizeof(value), "%.1f C", (double)s[SENSOR_IDX_TH].values[0]);
    Sim_SetRowText(0U, "TEMP", value);

    snprintf(value, sizeof(value), "%.1f %%", (double)s[SENSOR_IDX_TH].values[1]);
    Sim_SetRowText(1U, "HUMI", value);

    snprintf(value, sizeof(value), "%.0f ppm", (double)s[SENSOR_IDX_CO2].values[0]);
    Sim_SetRowText(2U, "CO2", value);

    snprintf(value, sizeof(value), "%.0f lux", (double)s[SENSOR_IDX_LIGHT].values[0]);
    Sim_SetRowText(3U, "LIGHT", value);

    snprintf(value, sizeof(value), "%s", s[SENSOR_IDX_HUMAN].values[0] > 0.5f ? "YES" : "NO");
    Sim_SetRowText(4U, "HUMAN", value);

    snprintf(value, sizeof(value), "%s", alarm->active ? "ACTIVE" : "OFF");
    Sim_SetRow(5U, "ALARM", value, alarm->active ? s_color_warn : s_color_ok);
}

static void Sim_UpdateSensor(const SimAppState_t *state)
{
    const SensorData_t *s = state->app.sensors;
    const SysConfig_t *cfg = &state->app.config;
    char value[SIM_VALUE_BUF_LEN];

    Sim_SetTitle(SIM_PAGE_SENSOR, "SENSOR STATUS");
    Sim_PrepareRows(4U);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->temp_humi_bus, cfg->temp_humi_addr,
             Sim_SensorOnlineString(&s[SENSOR_IDX_TH]),
             Sim_QualityToString(s[SENSOR_IDX_TH].quality),
             s[SENSOR_IDX_TH].fail_count);
    Sim_SetRow(0U, "TH", value, s[SENSOR_IDX_TH].online ? s_color_ok : s_color_warn);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->human_bus, cfg->human_addr,
             Sim_SensorOnlineString(&s[SENSOR_IDX_HUMAN]),
             Sim_QualityToString(s[SENSOR_IDX_HUMAN].quality),
             s[SENSOR_IDX_HUMAN].fail_count);
    Sim_SetRow(1U, "HUMAN", value, s[SENSOR_IDX_HUMAN].online ? s_color_ok : s_color_warn);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->light_bus, cfg->light_addr,
             Sim_SensorOnlineString(&s[SENSOR_IDX_LIGHT]),
             Sim_QualityToString(s[SENSOR_IDX_LIGHT].quality),
             s[SENSOR_IDX_LIGHT].fail_count);
    Sim_SetRow(2U, "LIGHT", value, s[SENSOR_IDX_LIGHT].online ? s_color_ok : s_color_warn);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->co2_bus, cfg->co2_addr,
             Sim_SensorOnlineString(&s[SENSOR_IDX_CO2]),
             Sim_QualityToString(s[SENSOR_IDX_CO2].quality),
             s[SENSOR_IDX_CO2].fail_count);
    Sim_SetRow(3U, "CO2", value, s[SENSOR_IDX_CO2].online ? s_color_ok : s_color_warn);
}

static void Sim_UpdateComm(const SimAppState_t *state)
{
    const SensorData_t *s = state->app.sensors;
    char value[SIM_VALUE_BUF_LEN];

    Sim_SetTitle(SIM_PAGE_COMM, "COMM ERRORS");
    Sim_PrepareRows(4U);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_TH].fail_count,
             (unsigned long)s[SENSOR_IDX_TH].crc_err_count,
             (unsigned long)s[SENSOR_IDX_TH].proto_err_count,
             (unsigned long)s[SENSOR_IDX_TH].invalid_count);
    Sim_SetRowText(0U, "TH", value);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_HUMAN].fail_count,
             (unsigned long)s[SENSOR_IDX_HUMAN].crc_err_count,
             (unsigned long)s[SENSOR_IDX_HUMAN].proto_err_count,
             (unsigned long)s[SENSOR_IDX_HUMAN].invalid_count);
    Sim_SetRowText(1U, "HUMAN", value);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_LIGHT].fail_count,
             (unsigned long)s[SENSOR_IDX_LIGHT].crc_err_count,
             (unsigned long)s[SENSOR_IDX_LIGHT].proto_err_count,
             (unsigned long)s[SENSOR_IDX_LIGHT].invalid_count);
    Sim_SetRowText(2U, "LIGHT", value);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_CO2].fail_count,
             (unsigned long)s[SENSOR_IDX_CO2].crc_err_count,
             (unsigned long)s[SENSOR_IDX_CO2].proto_err_count,
             (unsigned long)s[SENSOR_IDX_CO2].invalid_count);
    Sim_SetRowText(3U, "CO2", value);
}

static void Sim_UpdateAlarm(const SimAppState_t *state)
{
    const AlarmState_t *alarm = &state->app.alarm;
    char value[SIM_VALUE_BUF_LEN];

    Sim_SetTitle(SIM_PAGE_ALARM, "ALARM");
    Sim_PrepareRows(6U);

    snprintf(value, sizeof(value), "%s", alarm->active ? "ACTIVE" : "INACTIVE");
    Sim_SetRow(0U, "State", value, alarm->active ? s_color_warn : s_color_ok);

    Sim_SetRowText(1U, "Source", Sim_AlarmSourceToString(alarm->source));
    Sim_SetRowText(2U, "Level", Sim_AlarmLevelToString(alarm->level));

    snprintf(value, sizeof(value), "%.1f", (double)alarm->current_value);
    Sim_SetRowText(3U, "Value", value);

    snprintf(value, sizeof(value), "%.1f", (double)alarm->threshold);
    Sim_SetRowText(4U, "Limit", value);

    Sim_SetRowText(5U, "Muted", alarm->muted ? "YES" : "NO");
}

static void Sim_UpdateAdc(const SimAppState_t *state)
{
    const AdcData_t *adc = &state->app.adc;
    char value[SIM_VALUE_BUF_LEN];

    Sim_SetTitle(SIM_PAGE_ADC, "ADC");
    Sim_PrepareRows(4U);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[0]);
    Sim_SetRowText(0U, "Vin", value);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[1]);
    Sim_SetRowText(1U, "AIN1", value);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[2]);
    Sim_SetRowText(2U, "AIN2", value);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[3]);
    Sim_SetRowText(3U, "LDR", value);
}

static void Sim_UpdateSystem(const SimAppState_t *state)
{
    char value[SIM_VALUE_BUF_LEN];

    Sim_SetTitle(SIM_PAGE_SYSTEM, "SYSTEM");
    Sim_PrepareRows(5U);

    snprintf(value, sizeof(value), "%lu s", (unsigned long)state->uptime_s);
    Sim_SetRowText(0U, "Run", value);

    snprintf(value, sizeof(value), "%lu B", (unsigned long)state->free_heap);
    Sim_SetRowText(1U, "Heap", value);

    Sim_SetRow(2U, "Health", state->healthy ? "OK" : "FAULT",
               state->healthy ? s_color_ok : s_color_warn);
    Sim_SetRow(3U, "MQTT", state->mqtt_connected ? "CONNECTED" : "OFFLINE",
               state->mqtt_connected ? s_color_ok : s_color_warn);
    Sim_SetRowText(4U, "Upload", Sim_UploadTargetToString(state->upload_target));
}

static void Sim_UpdateLog(const SimAppState_t *state)
{
    char value[SIM_VALUE_BUF_LEN];

    Sim_SetTitle(SIM_PAGE_LOG, "LOG");
    Sim_PrepareRows(5U);

    snprintf(value, sizeof(value), "%lu", (unsigned long)state->ram_log_count);
    Sim_SetRowText(0U, "RAM count", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)state->flash_persist_ok);
    Sim_SetRowText(1U, "Flash OK", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)state->flash_persist_fail);
    Sim_SetRowText(2U, "Flash Fail", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)state->log_drop_count);
    Sim_SetRowText(3U, "Drop", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)state->log_queue_count);
    Sim_SetRowText(4U, "Queue", value);
}

void Sim_Dashboard_Init(void)
{
    s_ui.screen = lv_screen_active();

    lv_obj_set_style_bg_color(s_ui.screen, s_color_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.title = lv_label_create(s_ui.screen);
    lv_obj_set_size(s_ui.title, SIM_LCD_WIDTH - (SIM_MARGIN_PX * 2), SIM_TITLE_H_PX);
    lv_label_set_long_mode(s_ui.title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_ui.title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_ui.title, LV_ALIGN_TOP_LEFT, SIM_MARGIN_PX, SIM_MARGIN_PX);

    s_ui.content = lv_obj_create(s_ui.screen);
    lv_obj_set_pos(s_ui.content, SIM_MARGIN_PX, SIM_TITLE_H_PX + SIM_MARGIN_PX);
    lv_obj_set_size(s_ui.content,
                    SIM_LCD_WIDTH - (SIM_MARGIN_PX * 2),
                    SIM_LCD_HEIGHT - SIM_TITLE_H_PX - SIM_STATUS_H_PX - (SIM_MARGIN_PX * 4));
    lv_obj_set_style_bg_color(s_ui.content, s_color_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.content, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0U; i < SIM_ROW_COUNT; i++) {
        SimDashboardRow_t *row = &s_ui.rows[i];

        row->row = lv_obj_create(s_ui.content);
        lv_obj_set_style_bg_color(row->row, s_color_row, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row->row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row->row, s_color_border, LV_PART_MAIN);
        lv_obj_set_style_radius(row->row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row->row, 0, LV_PART_MAIN);
        lv_obj_clear_flag(row->row, LV_OBJ_FLAG_SCROLLABLE);

        row->name = lv_label_create(row->row);
        lv_obj_set_width(row->name, 72);
        lv_label_set_long_mode(row->name, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(row->name, s_color_dim, LV_PART_MAIN);
        lv_obj_align(row->name, LV_ALIGN_LEFT_MID, 6, 0);

        row->value = lv_label_create(row->row);
        lv_obj_set_width(row->value, 140);
        lv_label_set_long_mode(row->value, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(row->value, s_color_text, LV_PART_MAIN);
        lv_obj_align(row->value, LV_ALIGN_RIGHT_MID, -6, 0);
    }

    s_ui.status = lv_label_create(s_ui.screen);
    lv_obj_set_width(s_ui.status, SIM_LCD_WIDTH - (SIM_MARGIN_PX * 2));
    lv_label_set_long_mode(s_ui.status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_ui.status, s_color_dim, LV_PART_MAIN);
    lv_obj_align(s_ui.status, LV_ALIGN_BOTTOM_LEFT, SIM_MARGIN_PX, -SIM_MARGIN_PX);

    Sim_SetTitle(SIM_PAGE_MAIN, "ENV TERMINAL");
    Sim_PrepareRows(0U);
}

void Sim_Dashboard_Update(SimPage_t page, const SimAppState_t *state)
{
    if (state == NULL) {
        return;
    }

    switch (page) {
        case SIM_PAGE_MAIN:
            Sim_UpdateMain(state);
            break;
        case SIM_PAGE_SENSOR:
            Sim_UpdateSensor(state);
            break;
        case SIM_PAGE_COMM:
            Sim_UpdateComm(state);
            break;
        case SIM_PAGE_ALARM:
            Sim_UpdateAlarm(state);
            break;
        case SIM_PAGE_ADC:
            Sim_UpdateAdc(state);
            break;
        case SIM_PAGE_SYSTEM:
            Sim_UpdateSystem(state);
            break;
        case SIM_PAGE_LOG:
            Sim_UpdateLog(state);
            break;
        default:
            Sim_UpdateMain(state);
            break;
    }
}

SimPage_t Sim_Dashboard_NextPage(SimPage_t page)
{
    return (SimPage_t)(((uint8_t)page + 1U) % SIM_PAGE_COUNT);
}

SimPage_t Sim_Dashboard_PrevPage(SimPage_t page)
{
    if (page == SIM_PAGE_MAIN) {
        return SIM_PAGE_LOG;
    }
    return (SimPage_t)((uint8_t)page - 1U);
}
