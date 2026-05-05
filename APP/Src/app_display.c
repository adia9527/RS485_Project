#include "app_display.h"
#include "app_ui_text.h"
#include "app_types.h"
#include "app_backlight.h"
#include "app_health.h"
#include "app_event_log.h"
#include "app_log_storage.h"
#include "app_sensor_config.h"
#include "app_upload.h"
#include "app_esp32.h"
#include "bsp_lcd.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include <stdio.h>

#if APP_USE_LVGL
#include "lv_port_lcd_stm32.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#endif

#define DISPLAY_REFRESH_MS   2000U //页面数据刷新周期，2 秒更新一次传感器数据显示
#define DISPLAY_TASK_MS      50U //显示任务循环周期，通常每 50 ms 跑一次 LVGL 处理
#define DISPLAY_MARGIN_PX    6 //页面边距，单位是像素 
#define DISPLAY_TITLE_H_PX   22 //标题栏高度
#define DISPLAY_STATUS_H_PX  18 //状态栏高度
#define DISPLAY_ROW_COUNT    8U //最多显示 8 行数据
#define DISPLAY_TEXT_BUF_LEN 320U //普通文本拼接缓冲区长度
#define DISPLAY_VALUE_BUF_LEN 72U //单个数值字符串缓冲区长度

static volatile AppPage_t s_current_page = APP_PAGE_MAIN;
static volatile uint8_t   s_page_changed = 0U;
static volatile uint8_t   s_refresh_requested = 0U;

#if APP_USE_LVGL
//一行数据的 UI 控件
typedef struct
{
    lv_obj_t *row; //这一整行的容器对象
    lv_obj_t *name;//左侧名称标签
    lv_obj_t *value;//右侧数值标签
} DisplayUiRow_t;

//整个显示页面
typedef struct
{
    lv_obj_t       *screen;//当前页面屏幕根对象
    lv_obj_t       *title;//页面标题控件
    lv_obj_t       *content;//内容区域容器
    lv_obj_t       *status;//底部或顶部状态栏控件
    DisplayUiRow_t  rows[DISPLAY_ROW_COUNT];//多行数据显示项
} DisplayUi_t;

static DisplayUi_t s_ui;
#endif

static const char *s_page_names[] = {
    "MAIN", "SENSOR", "COMM", "ALARM", "ADC", "SYSTEM", "LOG"
};

/* ------------------------------------------------------------------ */
/*  Page text builder — delegates to platform-neutral app_ui_text     */
/* ------------------------------------------------------------------ */
void App_Display_BuildPageText(AppPage_t page,
                               const AppState_t *snapshot,
                               char *buf,
                               uint16_t buf_size)
{
    App_UI_BuildPageText((AppUiPage_t)page, snapshot, buf, buf_size);
}

#if APP_USE_LVGL
static const char *Display_QualityToString(SensorDataQuality_t quality)
{
    if (quality == SENSOR_DATA_VALID) {
        return "VALID";
    }
    if (quality == SENSOR_DATA_STALE) {
        return "STALE";
    }
    return "BAD";
}

static const char *Display_SensorOnlineString(const SensorData_t *sensor)
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

static const char *Display_AlarmSourceToString(AlarmSource_t source)
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

static const char *Display_AlarmLevelToString(AlarmLevel_t level)
{
    if (level == ALARM_CRITICAL) {
        return "CRITICAL";
    }
    if (level == ALARM_WARN) {
        return "ALARM";
    }
    return "NONE";
}

static const char *Display_UploadTargetToString(UploadTarget_t target)
{
    switch (target) {
        case UPLOAD_TARGET_NONE:            return "NONE";
        case UPLOAD_TARGET_USART1:          return "UART";
        case UPLOAD_TARGET_MQTT:            return "MQTT";
        case UPLOAD_TARGET_USART1_AND_MQTT: return "UART+MQTT";
        default:                            return "UNKNOWN";
    }
}

static lv_color_t Display_RowValueColor(const char *value)
{
    if (value == NULL) {
        return lv_color_hex(0xE8E8E8);
    }
    if (value[0] == 'O' || value[0] == 'V') {
        return lv_color_hex(0x8FE38B);
    }
    if (value[0] == 'A' || value[0] == 'C' || value[0] == 'F' ||
        value[0] == 'T' || value[0] == 'S' || value[0] == 'I') {
        return lv_color_hex(0xFF8A8A);
    }
    return lv_color_hex(0xE8E8E8);
}

static void Display_LvglCreateUi(void)
{
    s_ui.screen = lv_screen_active();

    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(0x101418), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.title = lv_label_create(s_ui.screen);
    lv_obj_set_size(s_ui.title, (int32_t)(LCD_WIDTH - (DISPLAY_MARGIN_PX * 2)), DISPLAY_TITLE_H_PX);
    lv_label_set_long_mode(s_ui.title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_ui.title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(s_ui.title, LV_ALIGN_TOP_LEFT, DISPLAY_MARGIN_PX, DISPLAY_MARGIN_PX);

    s_ui.content = lv_obj_create(s_ui.screen);
    lv_obj_set_pos(s_ui.content, DISPLAY_MARGIN_PX, DISPLAY_TITLE_H_PX + DISPLAY_MARGIN_PX);
    lv_obj_set_size(s_ui.content,
                    (int32_t)(LCD_WIDTH - (DISPLAY_MARGIN_PX * 2)),
                    (int32_t)(LCD_HEIGHT - DISPLAY_TITLE_H_PX -
                              DISPLAY_STATUS_H_PX - (DISPLAY_MARGIN_PX * 4)));
    lv_obj_set_style_bg_color(s_ui.content, lv_color_hex(0x101418), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.content, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0U; i < DISPLAY_ROW_COUNT; i++) {
        DisplayUiRow_t *row = &s_ui.rows[i];
        row->row = lv_obj_create(s_ui.content);
        lv_obj_set_style_bg_color(row->row, lv_color_hex(0x1A2026), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row->row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row->row, lv_color_hex(0x303840), LV_PART_MAIN);
        lv_obj_set_style_radius(row->row, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row->row, 0, LV_PART_MAIN);
        lv_obj_clear_flag(row->row, LV_OBJ_FLAG_SCROLLABLE);

        row->name = lv_label_create(row->row);
        lv_obj_set_width(row->name, 72);
        lv_label_set_long_mode(row->name, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(row->name, lv_color_hex(0x9BA7B2), LV_PART_MAIN);
        lv_obj_align(row->name, LV_ALIGN_LEFT_MID, 6, 0);

        row->value = lv_label_create(row->row);
        lv_obj_set_width(row->value, 140);
        lv_label_set_long_mode(row->value, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(row->value, lv_color_hex(0xE8E8E8), LV_PART_MAIN);
        lv_obj_align(row->value, LV_ALIGN_RIGHT_MID, -6, 0);
    }

    s_ui.status = lv_label_create(s_ui.screen);
    lv_obj_set_width(s_ui.status, (int32_t)(LCD_WIDTH - (DISPLAY_MARGIN_PX * 2)));
    lv_label_set_long_mode(s_ui.status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_ui.status, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
    lv_obj_align(s_ui.status, LV_ALIGN_BOTTOM_LEFT, DISPLAY_MARGIN_PX, -DISPLAY_MARGIN_PX);

    lv_label_set_text(s_ui.title, "MAIN");
    lv_label_set_text(s_ui.status, "1/7  KEY1 Next | KEY2 Prev");
}

//根据要显示的行数 count，动态调整 LVGL 页面里每一行的位置、大小和显示/隐藏状态。
static void Display_LvglPrepareRows(uint8_t count) 
{
    if (count > DISPLAY_ROW_COUNT) {
        count = DISPLAY_ROW_COUNT;
    }

    //计算内容区域高度：
    /*
        屏幕总高度是：LCD_HEIGHT,要减掉：DISPLAY_TITLE_H_PX(标题栏高度) / DISPLAY_STATUS_H_PX(状态栏高度) / DISPLAY_MARGIN_PX * 4(上下边距、间隔预留)
        所以：content_h = 屏幕高度 - 标题栏 - 状态栏 - 若干边距
    */
    int32_t content_h = (int32_t)(LCD_HEIGHT - DISPLAY_TITLE_H_PX -
                                  DISPLAY_STATUS_H_PX - (DISPLAY_MARGIN_PX * 4));
    int32_t gap = 4;//设置行间距
    //计算每行高度
    int32_t row_h = (count > 0U) ? ((content_h - ((int32_t)(count - 1U) * gap)) / (int32_t)count) : 0;
    if (row_h > 38) {
        row_h = 38;
    }
    if (row_h < 26) {
        row_h = 26;
    }

    //遍历所有预创建的行，设置位置、大小和是否隐藏（>count）
    for (uint8_t i = 0U; i < DISPLAY_ROW_COUNT; i++) {
        DisplayUiRow_t *row = &s_ui.rows[i];
        if (i < count) {
            lv_obj_clear_flag(row->row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(row->row, 0, (int32_t)i * (row_h + gap));
            lv_obj_set_size(row->row,
                            (int32_t)(LCD_WIDTH - (DISPLAY_MARGIN_PX * 2)),
                            row_h);
        } else {
            lv_obj_add_flag(row->row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

//设置每行的文本内容和字体颜色
static void Display_LvglSetRow(uint8_t idx, const char *name, const char *value, lv_color_t value_color)
{
    if (idx >= DISPLAY_ROW_COUNT) {
        return;
    }

    lv_label_set_text(s_ui.rows[idx].name, name);
    lv_label_set_text(s_ui.rows[idx].value, value);
    lv_obj_set_style_text_color(s_ui.rows[idx].value, value_color, LV_PART_MAIN);
}

static void Display_LvglSetRowText(uint8_t idx, const char *name, const char *value)
{
    Display_LvglSetRow(idx, name, value, Display_RowValueColor(value));
}

static void Display_LvglSetTitle(AppPage_t page, const char *title)
{
    uint8_t page_idx = (uint8_t)page;
    char status[48];

    if (page_idx >= APP_PAGE_COUNT) {
        page_idx = 0U;
    }

    lv_label_set_text(s_ui.title, title);
    snprintf(status, sizeof(status), "%u/%u  KEY1 Next | KEY2 Prev",
             (unsigned)(page_idx + 1U), (unsigned)APP_PAGE_COUNT);
    lv_label_set_text(s_ui.status, status);
}

//各个传感器的数据
static void Display_LvglUpdateMain(const AppState_t *snap)
{
    const SensorData_t *s = snap->sensors;
    const AlarmState_t *alarm = &snap->alarm;
    char value[DISPLAY_VALUE_BUF_LEN];

    Display_LvglSetTitle(APP_PAGE_MAIN, "ENV TERMINAL");
    Display_LvglPrepareRows(6U);

    snprintf(value, sizeof(value), "%.1f C", (double)s[SENSOR_IDX_TH].values[0]);
    Display_LvglSetRowText(0U, "TEMP", value);

    snprintf(value, sizeof(value), "%.1f %%", (double)s[SENSOR_IDX_TH].values[1]);
    Display_LvglSetRowText(1U, "HUMI", value);

    snprintf(value, sizeof(value), "%.0f ppm", (double)s[SENSOR_IDX_CO2].values[0]);
    Display_LvglSetRowText(2U, "CO2", value);

    snprintf(value, sizeof(value), "%.0f lux", (double)s[SENSOR_IDX_LIGHT].values[0]);
    Display_LvglSetRowText(3U, "LIGHT", value);

    snprintf(value, sizeof(value), "%s", s[SENSOR_IDX_HUMAN].values[0] > 0.5f ? "YES" : "NO");
    Display_LvglSetRowText(4U, "HUMAN", value);

    snprintf(value, sizeof(value), "%s", alarm->active ? "ACTIVE" : "OFF");
    Display_LvglSetRow(5U, "ALARM", value,
                       alarm->active ? lv_color_hex(0xFF8A8A) : lv_color_hex(0x8FE38B));
}

//各个传感器的状态、数据有效性
static void Display_LvglUpdateSensor(const AppState_t *snap)
{
    const SensorData_t *s = snap->sensors;
    const SysConfig_t *cfg = &snap->config;
    char value[DISPLAY_VALUE_BUF_LEN];

    Display_LvglSetTitle(APP_PAGE_SENSOR, "SENSOR STATUS");
    Display_LvglPrepareRows(4U);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->temp_humi_bus, cfg->temp_humi_addr,
             Display_SensorOnlineString(&s[SENSOR_IDX_TH]),
             Display_QualityToString(s[SENSOR_IDX_TH].quality),
             s[SENSOR_IDX_TH].fail_count);
    Display_LvglSetRowText(0U, "TH", value);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->human_bus, cfg->human_addr,
             Display_SensorOnlineString(&s[SENSOR_IDX_HUMAN]),
             Display_QualityToString(s[SENSOR_IDX_HUMAN].quality),
             s[SENSOR_IDX_HUMAN].fail_count);
    Display_LvglSetRowText(1U, "HUMAN", value);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->light_bus, cfg->light_addr,
             Display_SensorOnlineString(&s[SENSOR_IDX_LIGHT]),
             Display_QualityToString(s[SENSOR_IDX_LIGHT].quality),
             s[SENSOR_IDX_LIGHT].fail_count);
    Display_LvglSetRowText(2U, "LIGHT", value);

    snprintf(value, sizeof(value), "B%u A%02u %s %s f=%u",
             cfg->co2_bus, cfg->co2_addr,
             Display_SensorOnlineString(&s[SENSOR_IDX_CO2]),
             Display_QualityToString(s[SENSOR_IDX_CO2].quality),
             s[SENSOR_IDX_CO2].fail_count);
    Display_LvglSetRowText(3U, "CO2", value);
}

//各个传感器的通信信息：CRC校验失败次数、协议失败次数和数据无效性次数
static void Display_LvglUpdateComm(const AppState_t *snap)
{
    const SensorData_t *s = snap->sensors;
    char value[DISPLAY_VALUE_BUF_LEN];

    Display_LvglSetTitle(APP_PAGE_COMM, "COMM ERRORS");
    Display_LvglPrepareRows(4U);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_TH].fail_count,
             (unsigned long)s[SENSOR_IDX_TH].crc_err_count,
             (unsigned long)s[SENSOR_IDX_TH].proto_err_count,
             (unsigned long)s[SENSOR_IDX_TH].invalid_count);
    Display_LvglSetRowText(0U, "TH", value);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_HUMAN].fail_count,
             (unsigned long)s[SENSOR_IDX_HUMAN].crc_err_count,
             (unsigned long)s[SENSOR_IDX_HUMAN].proto_err_count,
             (unsigned long)s[SENSOR_IDX_HUMAN].invalid_count);
    Display_LvglSetRowText(1U, "HUMAN", value);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_LIGHT].fail_count,
             (unsigned long)s[SENSOR_IDX_LIGHT].crc_err_count,
             (unsigned long)s[SENSOR_IDX_LIGHT].proto_err_count,
             (unsigned long)s[SENSOR_IDX_LIGHT].invalid_count);
    Display_LvglSetRowText(2U, "LIGHT", value);

    snprintf(value, sizeof(value), "f=%u c=%lu p=%lu i=%lu",
             s[SENSOR_IDX_CO2].fail_count,
             (unsigned long)s[SENSOR_IDX_CO2].crc_err_count,
             (unsigned long)s[SENSOR_IDX_CO2].proto_err_count,
             (unsigned long)s[SENSOR_IDX_CO2].invalid_count);
    Display_LvglSetRowText(3U, "CO2", value);
}

//告警页面：告警状态、来源、等级、值、阈值、是否启动报警
static void Display_LvglUpdateAlarm(const AppState_t *snap)
{
    const AlarmState_t *alarm = &snap->alarm;
    char value[DISPLAY_VALUE_BUF_LEN];

    Display_LvglSetTitle(APP_PAGE_ALARM, "ALARM");
    Display_LvglPrepareRows(6U);

    snprintf(value, sizeof(value), "%s", alarm->active ? "ACTIVE" : "INACTIVE");
    Display_LvglSetRow(0U, "State", value,
                       alarm->active ? lv_color_hex(0xFF8A8A) : lv_color_hex(0x8FE38B));

    Display_LvglSetRowText(1U, "Source", Display_AlarmSourceToString(alarm->source));
    Display_LvglSetRowText(2U, "Level", Display_AlarmLevelToString(alarm->level));

    snprintf(value, sizeof(value), "%.1f", (double)alarm->current_value);
    Display_LvglSetRowText(3U, "Value", value);

    snprintf(value, sizeof(value), "%.1f", (double)alarm->threshold);
    Display_LvglSetRowText(4U, "Limit", value);

    Display_LvglSetRowText(5U, "Muted", alarm->muted ? "YES" : "NO");
}


static void Display_LvglUpdateAdc(const AppState_t *snap)
{
    const AdcData_t *adc = &snap->adc;
    char value[DISPLAY_VALUE_BUF_LEN];

    Display_LvglSetTitle(APP_PAGE_ADC, "ADC");
    Display_LvglPrepareRows(4U);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[0]);
    Display_LvglSetRowText(0U, "Vin", value);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[1]);
    Display_LvglSetRowText(1U, "AIN1", value);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[2]);
    Display_LvglSetRowText(2U, "AIN2", value);

    snprintf(value, sizeof(value), "%.2f V", (double)adc->voltage[3]);
    Display_LvglSetRowText(3U, "LDR", value);
}

static void Display_LvglUpdateSystem(const AppState_t *snap)
{
    AppHealthSnapshot_t health;
    AppESP32Status_t esp;
    char value[DISPLAY_VALUE_BUF_LEN];

    (void)snap;
    App_Health_GetSnapshot(&health);
    App_ESP32_GetStatus(&esp);

    Display_LvglSetTitle(APP_PAGE_SYSTEM, "SYSTEM");
    Display_LvglPrepareRows(5U);

    snprintf(value, sizeof(value), "%lu s", (unsigned long)health.uptime_s);
    Display_LvglSetRowText(0U, "Run", value);

    snprintf(value, sizeof(value), "%lu B", (unsigned long)health.free_heap);
    Display_LvglSetRowText(1U, "Heap", value);

    Display_LvglSetRow(2U, "Health", health.healthy ? "OK" : "FAULT",
                       health.healthy ? lv_color_hex(0x8FE38B) : lv_color_hex(0xFF8A8A));

    Display_LvglSetRow(3U, "MQTT", esp.mqtt_connected ? "CONNECTED" : "OFFLINE",
                       esp.mqtt_connected ? lv_color_hex(0x8FE38B) : lv_color_hex(0xFF8A8A));

    Display_LvglSetRowText(4U, "Upload", Display_UploadTargetToString(App_Upload_GetTarget()));
}

//当前日志数量、总共保存过多少条日志、保存失败的日志数量、被丢弃的日志数量、待处理的日志数量
static void Display_LvglUpdateLog(const AppState_t *snap)
{
    char value[DISPLAY_VALUE_BUF_LEN];

    (void)snap;
    Display_LvglSetTitle(APP_PAGE_LOG, "LOG");
    Display_LvglPrepareRows(5U);

    snprintf(value, sizeof(value), "%u", App_EventLog_GetCount());
    Display_LvglSetRowText(0U, "RAM count", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)App_LogStorage_GetPersistOkCount());
    Display_LvglSetRowText(1U, "Flash OK", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)App_LogStorage_GetPersistFailCount());
    Display_LvglSetRowText(2U, "Flash Fail", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)App_LogStorage_GetDropCount());
    Display_LvglSetRowText(3U, "Drop", value);

    snprintf(value, sizeof(value), "%lu", (unsigned long)App_LogStorage_GetQueuedCount());
    Display_LvglSetRowText(4U, "Queue", value);
}

static void Display_LvglUpdatePage(AppPage_t page, const AppState_t *snap)
{
    switch (page) {
        case APP_PAGE_MAIN:
            Display_LvglUpdateMain(snap);
            break;
        case APP_PAGE_SENSOR:
            Display_LvglUpdateSensor(snap);
            break;
        case APP_PAGE_COMM:
            Display_LvglUpdateComm(snap);
            break;
        case APP_PAGE_ALARM:
            Display_LvglUpdateAlarm(snap);
            break;
        case APP_PAGE_ADC:
            Display_LvglUpdateAdc(snap);
            break;
        case APP_PAGE_SYSTEM:
            Display_LvglUpdateSystem(snap);
            break;
        case APP_PAGE_LOG:
            Display_LvglUpdateLog(snap);
            break;
        default:
            Display_LvglUpdateMain(snap);
            break;
    }
}
#endif

/* ------------------------------------------------------------------ */
/*  Page control                                                       */
/* ------------------------------------------------------------------ */
void App_Display_Init(void)
{
    s_current_page = APP_PAGE_MAIN;
    s_page_changed = 1U;
    s_refresh_requested = 1U;
#if APP_USE_LVGL
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    Display_LvglCreateUi();
    lv_refr_now(NULL);
#endif
    BSP_Log_Printf("[DISPLAY] init ok\r\n");
}

void App_Display_SetPage(uint8_t page)
{
    if (page >= APP_PAGE_COUNT) { return; }
    s_current_page = (AppPage_t)page;
    s_page_changed = 1U;
    BSP_Log_Printf("[DISPLAY] page=%s\r\n", s_page_names[page]);
}

void App_Display_RequestPageNext(void)
{
    App_Display_SetPage((uint8_t)((s_current_page + 1U) % APP_PAGE_COUNT));
}

void App_Display_RequestPagePrev(void)
{
    uint8_t prev = (s_current_page == 0U) ? (APP_PAGE_COUNT - 1U) : (s_current_page - 1U);
    App_Display_SetPage(prev);
}

void App_Display_RequestRefresh(void)
{
    s_refresh_requested = 1U;
}

/* ------------------------------------------------------------------ */
/*  Touch stubs                                                        */
/* ------------------------------------------------------------------ */
void App_Display_OnTouchWakeup(void)
{
    App_Backlight_Wakeup();
}

void App_Display_OnTouchPoint(uint16_t x, uint16_t y)
{
    (void)x; (void)y;
    App_Backlight_Wakeup();
}

/* ------------------------------------------------------------------ */
/*  Render one page via UART fallback or LVGL dashboard               */
/* ------------------------------------------------------------------ */
static void Display_RenderPage(AppPage_t page)
{
    AppState_t snap;
    App_StateLock();
    snap = g_app_state;
    App_StateUnlock();

#if APP_USE_LVGL
    Display_LvglUpdatePage(page, &snap);
#else
    char buf[DISPLAY_TEXT_BUF_LEN];
    App_Display_BuildPageText(page, &snap, buf, sizeof(buf));
    BSP_Log_Printf("=== %s ===\r\n%s\r\n", s_page_names[(uint8_t)page], buf);
#endif
}

/* ------------------------------------------------------------------ */
/*  DisplayTask                                                        */
/* ------------------------------------------------------------------ */
void App_DisplayTask(void *argument)
{
    (void)argument;
    BSP_Log_Printf("[APP] DisplayTask started\r\n");

    uint32_t last_refresh = osKernelGetTickCount();
#if APP_USE_LVGL
    uint32_t last_lv_tick = last_refresh;
#endif

    for (;;) {
        App_Health_Beat(APP_TASK_ID_DISPLAY);

        uint32_t now = osKernelGetTickCount();

#if APP_USE_LVGL
        uint32_t elapsed = now - last_lv_tick;
        if (elapsed > 0U) {
            lv_tick_inc(elapsed);
            last_lv_tick = now;
        }
        lv_timer_handler();
#endif

        uint8_t changed = s_page_changed;
        uint8_t refresh = s_refresh_requested;

        if (changed || refresh) {
            s_page_changed = 0U;
            s_refresh_requested = 0U;
            last_refresh = now;
            Display_RenderPage(s_current_page);
        } else if ((now - last_refresh) >= DISPLAY_REFRESH_MS) {
            last_refresh = now;
            Display_RenderPage(s_current_page);
        }

        osDelay(DISPLAY_TASK_MS);
    }
}
