#include "app_main.h"
#include "app_types.h"
#include "app_monitor.h"
#include "app_sensor.h"
#include "app_alarm.h"
#include "app_upload.h"
#include "app_config.h"
#include "app_display.h"
#include "app_backlight.h"
#include "app_cmd.h"
#include "app_health.h"
#include "app_event_log.h"
#include "app_log_storage.h"
#include "app_esp32.h"
#include "app_touch.h"
#include "bsp_led.h"
#include "bsp_watchdog.h"
#include "bsp_beep.h"
#include "bsp_key.h"
#include "bsp_adc.h"
#include "bsp_rs485.h"
#include "bsp_log.h"
#include "bsp_w25q64.h"
#include "bsp_spi_bus.h"
#include "bsp_lcd.h"
#include "bsp_esp32.h"
#include "cmsis_os.h"

AppState_t    g_app_state       = { 0 };
osMutexId_t   g_app_state_mutex = NULL;

/* ---- mutex helpers ---- */
void App_StateLock(void)
{
    if (g_app_state_mutex != NULL) {
        osMutexAcquire(g_app_state_mutex, osWaitForever);
    }
}

void App_StateUnlock(void)
{
    if (g_app_state_mutex != NULL) {
        osMutexRelease(g_app_state_mutex);
    }
}

/* ---- task attributes ---- */
static osThreadId_t s_monitor_task_handle;
static const osThreadAttr_t s_monitor_task_attr = {
    .name       = "MonitorTask",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityNormal
};

static osThreadId_t s_sensor_task_handle;
static const osThreadAttr_t s_sensor_task_attr = {
    .name       = "SensorTask",
    .stack_size = 640U * 4U,
    .priority   = (osPriority_t)osPriorityNormal
};

static osThreadId_t s_alarm_task_handle;
static const osThreadAttr_t s_alarm_task_attr = {
    .name       = "AlarmTask",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityNormal
};

static osThreadId_t s_upload_task_handle;
static const osThreadAttr_t s_upload_task_attr = {
    .name       = "UploadTask",
    .stack_size = 768U * 4U,
    .priority   = (osPriority_t)osPriorityLow
};

static osThreadId_t s_config_task_handle;
static const osThreadAttr_t s_config_task_attr = {
    .name       = "ConfigTask",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityLow
};

static osThreadId_t s_display_task_handle;
static const osThreadAttr_t s_display_task_attr = {
    .name       = "DisplayTask",
    .stack_size = 1024U * 4U,
    .priority   = (osPriority_t)osPriorityNormal
};

static osThreadId_t s_backlight_task_handle;
static const osThreadAttr_t s_backlight_task_attr = {
    .name       = "BacklightTask",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityLow
};

static osThreadId_t s_cmd_task_handle;
static const osThreadAttr_t s_cmd_task_attr = {
    .name       = "CmdTask",
    .stack_size = 768U * 4U,
    .priority   = (osPriority_t)osPriorityNormal
};

static osThreadId_t s_log_storage_task_handle;
static const osThreadAttr_t s_log_storage_task_attr = {
    .name       = "LogStorageTask",
    .stack_size = 1024U * 4U,
    .priority   = (osPriority_t)osPriorityLow
};

static osThreadId_t s_esp32_task_handle;
static const osThreadAttr_t s_esp32_task_attr = {
    .name       = "Esp32Task",
    .stack_size = 512U * 4U,
    .priority   = (osPriority_t)osPriorityLow
};

static osThreadId_t s_touch_task_handle;
static const osThreadAttr_t s_touch_task_attr = {
    .name       = "TouchTask",
    .stack_size = 768U * 4U,
    .priority   = (osPriority_t)osPriorityNormal
};

void App_MainInit(void)
{
    App_Health_Init();
    App_Health_PrintResetReason();
    BSP_Watchdog_Init();

    BSP_Log_Init();
    BSP_LED_Init();
    BSP_Beep_Init();
    BSP_Key_Init();
    BSP_ADC_Init();
    BSP_RS485_Init();
    BSP_SPI1Bus_Init();
    BSP_W25Q64_Init();
    BSP_LCD_Init();

    g_app_state.initialized = 1U;

    App_EventLog_Init();
    App_EventLog_StorageInit();
    App_LogStorage_Init();
    App_EventLog_Add(APP_EVENT_SYSTEM_BOOT, 0U, 0, 0.0f,
        "system boot reset=%s", App_Health_GetResetReasonString());

    App_Config_Init();
    App_Cmd_Init();
    App_Backlight_Init();
    App_Display_Init();
    BSP_ESP32_Init();
    App_ESP32_Init();
    App_Touch_Init();

    BSP_Log_Printf("[SYS] boot ok\r\n");
}

void App_CreateTasks(void)
{
    static const osMutexAttr_t s_mutex_attr = { .name = "AppStateMutex" };
    g_app_state_mutex = osMutexNew(&s_mutex_attr);

    s_monitor_task_handle      = osThreadNew(App_MonitorTask,      NULL, &s_monitor_task_attr);
    s_sensor_task_handle       = osThreadNew(App_SensorTask,       NULL, &s_sensor_task_attr);
    s_alarm_task_handle        = osThreadNew(App_AlarmTask,        NULL, &s_alarm_task_attr);
    s_upload_task_handle       = osThreadNew(App_UploadTask,       NULL, &s_upload_task_attr);
    s_config_task_handle       = osThreadNew(App_ConfigTask,       NULL, &s_config_task_attr);
    s_display_task_handle      = osThreadNew(App_DisplayTask,      NULL, &s_display_task_attr);
    s_backlight_task_handle    = osThreadNew(App_BacklightTask,    NULL, &s_backlight_task_attr);
    s_cmd_task_handle          = osThreadNew(App_CmdTask,          NULL, &s_cmd_task_attr);
    s_log_storage_task_handle  = osThreadNew(App_LogStorageTask,   NULL, &s_log_storage_task_attr);
    s_esp32_task_handle        = osThreadNew(App_ESP32Task,        NULL, &s_esp32_task_attr);
    s_touch_task_handle        = osThreadNew(App_TouchTask,        NULL, &s_touch_task_attr);

    BSP_Log_Printf("[APP] AlarmTask create ok\r\n");
    BSP_Log_Printf("[APP] UploadTask create ok\r\n");
    BSP_Log_Printf("[APP] ConfigTask create ok\r\n");
    BSP_Log_Printf("[APP] DisplayTask create ok\r\n");
    BSP_Log_Printf("[APP] BacklightTask create ok\r\n");
    BSP_Log_Printf("[APP] CmdTask create ok\r\n");
    BSP_Log_Printf("[APP] LogStorageTask create ok\r\n");
    BSP_Log_Printf("[APP] ESP32Task create ok\r\n");

    /* TODO: create DisplayTask when LVGL is integrated */
}
