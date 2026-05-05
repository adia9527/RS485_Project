#include "app_config.h"
#include "app_types.h"
#include "app_health.h"
#include "app_event_log.h"
#include "bsp_flash.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include <string.h>

#define CONFIG_DIRTY_DELAY_MS  3000U
#define CONFIG_TASK_PERIOD_MS  500U

static volatile uint8_t  s_dirty     = 0U;
static volatile uint32_t s_dirty_tick = 0U;

/* ---- checksum ---- */
uint32_t App_Config_CalcChecksum(const SysConfig_t *cfg)
{
    const uint8_t *p   = (const uint8_t *)cfg;
    uint32_t       sum = 0U;
    uint32_t       len = offsetof(SysConfig_t, checksum);
    for (uint32_t i = 0U; i < len; i++) sum += p[i];
    return sum;
}

/* ---- default values ---- */
void App_Config_LoadDefault(SysConfig_t *cfg)
{
    memset(cfg, 0, sizeof(SysConfig_t));
    cfg->magic   = APP_CONFIG_MAGIC;
    cfg->version = APP_CONFIG_VERSION;
    cfg->size    = (uint16_t)sizeof(SysConfig_t);

    cfg->device_id = 1U;

    cfg->temp_high_threshold  = 30.0f;
    cfg->temp_low_threshold   = 5.0f;
    cfg->humi_high_threshold  = 80.0f;
    cfg->humi_low_threshold   = 20.0f;

    cfg->co2_high_threshold   = 1000U;
    cfg->light_high_threshold = 2000U;
    cfg->light_low_threshold  = 50U;

    cfg->vin_low_threshold    = 10.5f;
    cfg->ain1_high_threshold  = 3.0f;
    cfg->ain2_high_threshold  = 3.0f;

    cfg->temp_humi_addr = 0x01U;
    cfg->human_addr     = 0x02U;
    cfg->light_addr     = 0x03U;
    cfg->co2_addr       = 0x04U;

    cfg->temp_humi_bus = 0U;
    cfg->human_bus     = 0U;
    cfg->light_bus     = 0U;
    cfg->co2_bus       = 0U;

    cfg->sensor_poll_interval_ms = 500U;
    cfg->upload_period_ms        = 1000U;
    cfg->backlight_timeout_ms    = 30000U;

    cfg->alarm_enable     = 1U;
    cfg->buzzer_enable    = 1U;
    cfg->led_alarm_enable = 1U;
    cfg->log_enable       = 1U;

    cfg->rs485_poll_ms = 200U;
    cfg->adc_poll_ms   = 500U;

    strncpy(cfg->wifi_ssid,      "",                  sizeof(cfg->wifi_ssid) - 1U);
    strncpy(cfg->wifi_password,  "",                  sizeof(cfg->wifi_password) - 1U);
    strncpy(cfg->mqtt_host,      "192.168.1.100",     sizeof(cfg->mqtt_host) - 1U);
    cfg->mqtt_port = 1883U;
    strncpy(cfg->mqtt_client_id, "stm32_env_terminal", sizeof(cfg->mqtt_client_id) - 1U);
    strncpy(cfg->mqtt_username,  "",                  sizeof(cfg->mqtt_username) - 1U);
    strncpy(cfg->mqtt_password,  "",                  sizeof(cfg->mqtt_password) - 1U);
    strncpy(cfg->mqtt_topic_data,   "env_terminal/data",   sizeof(cfg->mqtt_topic_data) - 1U);
    strncpy(cfg->mqtt_topic_alarm,  "env_terminal/alarm",  sizeof(cfg->mqtt_topic_alarm) - 1U);
    strncpy(cfg->mqtt_topic_status, "env_terminal/status", sizeof(cfg->mqtt_topic_status) - 1U);
    cfg->mqtt_enable = 0U;
    cfg->mqtt_qos    = 0U;
    cfg->mqtt_retain = 0U;
    strncpy(cfg->mqtt_topic_cmd,  "env_terminal/cmd",  sizeof(cfg->mqtt_topic_cmd)  - 1U);
    strncpy(cfg->mqtt_topic_resp, "env_terminal/resp", sizeof(cfg->mqtt_topic_resp) - 1U);
    strncpy(cfg->mqtt_cmd_token,  "123456",            sizeof(cfg->mqtt_cmd_token)  - 1U);
    cfg->mqtt_cmd_enable = 1U;
    cfg->mqtt_qos_data   = 0U;
    cfg->mqtt_qos_alarm  = 1U;
    cfg->mqtt_qos_status = 0U;
    cfg->mqtt_qos_resp   = 1U;
    cfg->mqtt_retain_data   = 0U;
    cfg->mqtt_retain_alarm  = 0U;
    cfg->mqtt_retain_status = 0U;
    cfg->mqtt_retain_resp   = 0U;

    cfg->checksum = App_Config_CalcChecksum(cfg);
}

/* ---- validation ---- */
uint8_t App_Config_IsValid(const SysConfig_t *cfg)
{
    if (cfg->magic   != APP_CONFIG_MAGIC)   { return 0U; }
    if (cfg->version != APP_CONFIG_VERSION) {
        BSP_Log_Printf("[CONFIG] version mismatch, load default\r\n");
        return 0U;
    }
    if (cfg->size    != (uint16_t)sizeof(SysConfig_t)) { return 0U; }
    if (cfg->checksum != App_Config_CalcChecksum(cfg))  {
        BSP_Log_Printf("[CONFIG] checksum error\r\n");
        return 0U;
    }
    /* validate string termination */
    if (cfg->mqtt_host[sizeof(cfg->mqtt_host) - 1U] != '\0') { return 0U; }
    if (cfg->wifi_ssid[sizeof(cfg->wifi_ssid) - 1U] != '\0') { return 0U; }
    if (cfg->mqtt_topic_cmd[sizeof(cfg->mqtt_topic_cmd)   - 1U] != '\0') { return 0U; }
    if (cfg->mqtt_topic_resp[sizeof(cfg->mqtt_topic_resp) - 1U] != '\0') { return 0U; }
    if (cfg->mqtt_cmd_token[sizeof(cfg->mqtt_cmd_token)   - 1U] != '\0') { return 0U; }
    if (cfg->mqtt_enable > 1U) { return 0U; }
    if (cfg->mqtt_cmd_enable > 1U) { return 0U; }
    if (cfg->mqtt_qos > 1U || cfg->mqtt_retain > 1U) { return 0U; }
    if (cfg->mqtt_qos_data > 1U || cfg->mqtt_qos_alarm > 1U ||
        cfg->mqtt_qos_status > 1U || cfg->mqtt_qos_resp > 1U) {
        return 0U;
    }
    if (cfg->mqtt_retain_data > 1U || cfg->mqtt_retain_alarm > 1U ||
        cfg->mqtt_retain_status > 1U || cfg->mqtt_retain_resp > 1U) {
        return 0U;
    }
    /* validate port range */
    if (cfg->mqtt_port == 0U) { return 0U; }
    return 1U;
}

/* ---- load from Flash ---- */
uint8_t App_Config_Load(SysConfig_t *cfg)
{
    SysConfig_t tmp;
    BSP_Flash_Read(APP_CONFIG_FLASH_ADDRESS, (uint8_t *)&tmp, sizeof(SysConfig_t));
    if (!App_Config_IsValid(&tmp)) { return 0U; }
    memcpy(cfg, &tmp, sizeof(SysConfig_t));
    return 1U;
}

/* ---- save to Flash ---- */
uint8_t App_Config_Save(const SysConfig_t *cfg)
{
    /* Copy snapshot first, fill header/checksum outside the lock */
    SysConfig_t tmp;
    memcpy(&tmp, cfg, sizeof(SysConfig_t));
    tmp.magic    = APP_CONFIG_MAGIC;
    tmp.version  = APP_CONFIG_VERSION;
    tmp.size     = (uint16_t)sizeof(SysConfig_t);
    tmp.checksum = App_Config_CalcChecksum(&tmp);

    HAL_StatusTypeDef st = BSP_Flash_EraseSector(APP_CONFIG_FLASH_SECTOR);
    if (st != HAL_OK) {
        BSP_Log_Printf("[CONFIG] save failed err=%d\r\n", (int)st);
        return 0U;
    }
    st = BSP_Flash_Write(APP_CONFIG_FLASH_ADDRESS, (const uint8_t *)&tmp, sizeof(SysConfig_t));
    if (st != HAL_OK) {
        BSP_Log_Printf("[CONFIG] save failed err=%d\r\n", (int)st);
        return 0U;
    }
    BSP_Log_Printf("[CONFIG] save ok\r\n");
    App_EventLog_Add(APP_EVENT_CONFIG_SAVED, 0U, 0, 0.0f, "config saved");
    return 1U;
}

/* ---- dirty flag ---- */
//标记当前配置已经被修改，需要在合适的时机保存到 Flash / EEPROM / 外部存储中
void App_Config_MarkDirty(void)
{
    s_dirty      = 1U;
    s_dirty_tick = osKernelGetTickCount();
}

/* ---- restore default ---- */
void App_Config_RestoreDefault(void)
{
    SysConfig_t def;
    App_Config_LoadDefault(&def);

    App_StateLock();
    memcpy(&g_app_state.config, &def, sizeof(SysConfig_t));
    App_StateUnlock();

    App_Config_MarkDirty();
    BSP_Log_Printf("[CONFIG] restore default\r\n");
    App_EventLog_Add(APP_EVENT_CONFIG_RESTORE_DEFAULT, 0U, 0, 0.0f, "config restore default");
}

/* ---- startup init ---- */
void App_Config_Init(void)
{
    SysConfig_t tmp;

    if (App_Config_Load(&tmp)) {
        App_StateLock();
        memcpy(&g_app_state.config, &tmp, sizeof(SysConfig_t));
        App_StateUnlock();
        BSP_Log_Printf("[CONFIG] load ok\r\n");
    } else {
        BSP_Log_Printf("[CONFIG] invalid, load default\r\n");
        SysConfig_t def;
        App_Config_LoadDefault(&def);
        App_StateLock();
        memcpy(&g_app_state.config, &def, sizeof(SysConfig_t));
        App_StateUnlock();
        BSP_Log_Printf("[CONFIG] default loaded\r\n");
        App_Config_MarkDirty();
    }
}

/* ---- ConfigTask ---- */
//周期性检查配置是否被修改过。如果配置已经修改，并且距离最后一次修改已经超过一段延迟时间，就把当前配置保存到 Flash / EEPROM / 外部存储里。
void App_ConfigTask(void *argument)
{
    (void)argument;
    BSP_Log_Printf("[APP] ConfigTask started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_CONFIG);
        if (s_dirty) {
            uint32_t now  = osKernelGetTickCount();
            uint32_t diff = now - s_dirty_tick;
            if (diff >= CONFIG_DIRTY_DELAY_MS) {
                s_dirty = 0U;

                /* Snapshot config, then save outside lock */
                SysConfig_t snap;
                App_StateLock();
                memcpy(&snap, &g_app_state.config, sizeof(SysConfig_t));
                App_StateUnlock();

                App_Config_Save(&snap);
            }
        }
        osDelay(CONFIG_TASK_PERIOD_MS);
    }
}
