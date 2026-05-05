#include "sim_app_state.h"
#include "app_sensor_config.h"
#include <string.h>

static float Sim_Wave(uint32_t tick_ms, uint32_t period_ms, float min_value, float max_value)
{
    uint32_t phase;
    uint32_t half;
    float ratio;

    if (period_ms < 2U) {
        return min_value;
    }

    phase = tick_ms % period_ms;
    half = period_ms / 2U;
    if (phase < half) {
        ratio = (float)phase / (float)half;
    } else {
        ratio = (float)(period_ms - phase) / (float)half;
    }

    return min_value + ((max_value - min_value) * ratio);
}

static uint16_t Sim_VoltageToRaw(float voltage)
{
    float raw = (voltage * 4095.0f) / 3.3f;
    if (raw < 0.0f) {
        raw = 0.0f;
    }
    if (raw > 4095.0f) {
        raw = 4095.0f;
    }
    return (uint16_t)raw;
}

static void Sim_SetSensorOnline(SensorData_t *sensor, bool online)
{
    sensor->online = online ? 1U : 0U;
    sensor->status = online ? SENSOR_STATUS_ONLINE : SENSOR_STATUS_TIMEOUT;
    sensor->quality = online ? SENSOR_DATA_VALID : SENSOR_DATA_STALE;
    sensor->fail_count = online ? 0U : 3U;
}

void Sim_AppState_Init(SimAppState_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));

    state->app.initialized = 1U;
    state->app.config.device_id = 1U;
    state->app.config.temp_humi_bus = 1U;
    state->app.config.human_bus = 1U;
    state->app.config.light_bus = 1U;
    state->app.config.co2_bus = 2U;
    state->app.config.temp_humi_addr = SENSOR_ADDR_TH;
    state->app.config.human_addr = SENSOR_ADDR_HUMAN;
    state->app.config.light_addr = SENSOR_ADDR_LIGHT;
    state->app.config.co2_addr = SENSOR_ADDR_CO2;

    state->app.sensors[SENSOR_IDX_TH].slave_addr = SENSOR_ADDR_TH;
    state->app.sensors[SENSOR_IDX_HUMAN].slave_addr = SENSOR_ADDR_HUMAN;
    state->app.sensors[SENSOR_IDX_LIGHT].slave_addr = SENSOR_ADDR_LIGHT;
    state->app.sensors[SENSOR_IDX_CO2].slave_addr = SENSOR_ADDR_CO2;

    state->free_heap = 24576U;
    state->healthy = true;
    state->mqtt_connected = true;
    state->upload_target = SIM_UPLOAD_TARGET_MQTT;
}

void Sim_AppState_Update(SimAppState_t *state, uint32_t tick_ms)
{
    AppState_t *app;
    bool light_online;
    bool human_present;
    uint32_t seconds;

    if (state == NULL) {
        return;
    }

    app = &state->app;
    seconds = tick_ms / 1000U;
    state->uptime_s = seconds;
    state->free_heap = 24576U + (seconds % 1024U);
    state->healthy = true;

    app->sensors[SENSOR_IDX_TH].values[0] = Sim_Wave(tick_ms, 14000U, 26.0f, 28.0f);
    app->sensors[SENSOR_IDX_TH].values[1] = Sim_Wave(tick_ms + 3000U, 17000U, 60.0f, 65.0f);
    app->sensors[SENSOR_IDX_CO2].values[0] = Sim_Wave(tick_ms + 5000U, 22000U, 650.0f, 900.0f);
    app->sensors[SENSOR_IDX_LIGHT].values[0] = Sim_Wave(tick_ms + 2000U, 19000U, 300.0f, 700.0f);

    human_present = ((tick_ms / 9000U) % 2U) != 0U;
    if (app->sensors[SENSOR_IDX_HUMAN].values[0] > 0.5f) {
        human_present = true;
    }
    app->sensors[SENSOR_IDX_HUMAN].values[0] = human_present ? 1.0f : 0.0f;

    light_online = ((tick_ms / 12000U) % 4U) != 3U;
    Sim_SetSensorOnline(&app->sensors[SENSOR_IDX_TH], true);
    Sim_SetSensorOnline(&app->sensors[SENSOR_IDX_HUMAN], true);
    Sim_SetSensorOnline(&app->sensors[SENSOR_IDX_LIGHT], light_online);
    Sim_SetSensorOnline(&app->sensors[SENSOR_IDX_CO2], true);

    app->sensors[SENSOR_IDX_LIGHT].crc_err_count = (seconds / 17U) % 4U;
    app->sensors[SENSOR_IDX_LIGHT].proto_err_count = (seconds / 29U) % 2U;
    app->sensors[SENSOR_IDX_LIGHT].invalid_count = light_online ? 0U : 1U;
    app->sensors[SENSOR_IDX_CO2].crc_err_count = (seconds / 31U) % 2U;
    app->sensors[SENSOR_IDX_TH].proto_err_count = (seconds / 37U) % 2U;

    app->comm[0].tx_count = seconds * 4U;
    app->comm[0].rx_count = seconds * 4U;
    app->comm[0].err_count = app->sensors[SENSOR_IDX_LIGHT].crc_err_count;
    app->comm[0].timeout_count = light_online ? 0U : 1U;
    app->comm[1].tx_count = seconds;
    app->comm[1].rx_count = seconds;
    app->comm[1].err_count = app->sensors[SENSOR_IDX_CO2].crc_err_count;

    app->adc.voltage[0] = 12.1f + Sim_Wave(tick_ms, 11000U, -0.10f, 0.10f);
    app->adc.voltage[1] = Sim_Wave(tick_ms + 1000U, 15000U, 1.10f, 1.50f);
    app->adc.voltage[2] = Sim_Wave(tick_ms + 2000U, 16000U, 2.60f, 2.95f);
    app->adc.voltage[3] = Sim_Wave(tick_ms + 3000U, 12000U, 0.80f, 1.20f);
    for (uint8_t i = 0U; i < ADC_CHANNEL_COUNT; i++) {
        app->adc.raw[i] = Sim_VoltageToRaw(app->adc.voltage[i] > 3.3f ? 3.3f : app->adc.voltage[i]);
    }

    app->alarm.current_value = app->sensors[SENSOR_IDX_CO2].values[0];
    app->alarm.threshold = 1000.0f;
    app->alarm.source = app->alarm.active ? ALARM_SRC_CO2_HIGH : ALARM_SRC_NONE;
    app->alarm.level = app->alarm.active ? ALARM_WARN : ALARM_NONE;

    state->ram_log_count = 12U + ((seconds / 8U) % 20U);
    state->flash_persist_ok = 10U + ((seconds / 10U) % 40U);
    state->flash_persist_fail = (seconds / 90U) % 2U;
    state->log_drop_count = (seconds / 120U) % 2U;
    state->log_queue_count = (seconds / 15U) % 5U;
}

void Sim_AppState_ToggleAlarm(SimAppState_t *state)
{
    if (state != NULL) {
        state->app.alarm.active = state->app.alarm.active ? 0U : 1U;
    }
}

void Sim_AppState_ToggleHuman(SimAppState_t *state)
{
    if (state != NULL) {
        state->app.sensors[SENSOR_IDX_HUMAN].values[0] =
            state->app.sensors[SENSOR_IDX_HUMAN].values[0] > 0.5f ? 0.0f : 1.0f;
    }
}

void Sim_AppState_ToggleMqtt(SimAppState_t *state)
{
    if (state != NULL) {
        state->mqtt_connected = !state->mqtt_connected;
    }
}
