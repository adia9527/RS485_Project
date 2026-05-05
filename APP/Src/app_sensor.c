#include "app_sensor.h"
#include "app_sensor_config.h"
#include "app_types.h"
#include "app_config.h"
#include "app_health.h"
#include "app_event_log.h"
#include "bsp_rs485.h"
#include "bsp_log.h"
#include "protocol_modbus.h"
#include "cmsis_os.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Comm result type                                                    */
/* ------------------------------------------------------------------ */
typedef enum {
    SENSOR_RESULT_OK = 0,
    SENSOR_RESULT_TIMEOUT,
    SENSOR_RESULT_CRC_ERROR,
    SENSOR_RESULT_ADDR_ERROR,
    SENSOR_RESULT_FUNC_ERROR,
    SENSOR_RESULT_LEN_ERROR
} SensorResult_t;

/* ------------------------------------------------------------------ */
/*  Per-sensor runtime state (offline backoff tracking)                */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t last_poll_tick;    /* tick of last poll attempt  上一次尝试读取该传感器的系统 tick 时间        */
    uint8_t  prev_online;       /* online state at previous poll  上一次轮询时，该传感器是否在线    */
} SensorRuntime_t;

static SensorRuntime_t s_rt[SENSOR_COUNT];

/* ------------------------------------------------------------------ */
/*  Sensor configuration table                                         */
/*  addr is refreshed from config each iteration                       */
/* ------------------------------------------------------------------ */
static SensorConfig_t s_sensor_cfg[SENSOR_COUNT] = {
    {
        .type         = SENSOR_TYPE_TEMP_HUMI,
        .name         = "TH",
        .idx          = SENSOR_IDX_TH,
        .addr         = SENSOR_ADDR_TH,
        .function_code= MODBUS_FC_READ_HOLDING_REGS,
        .start_reg    = SENSOR_REG_TH,
        .reg_count    = SENSOR_REGCNT_TH,
        .rs485_port   = 0U,
        .scale_0      = SENSOR_SCALE_TEMP,
        .scale_1      = SENSOR_SCALE_HUMI,
        /* temp raw: -400 ~ 1000 (→ -40.0 ~ 100.0 ℃), humi raw: 0 ~ 1000 */
        .raw_min_0    = -400, .raw_max_0 = 1000,
        .raw_min_1    =    0, .raw_max_1 = 1000,
        .normal_poll_ms  = 500U,
        .offline_poll_ms = SENSOR_OFFLINE_POLL_MS,
    },
    {
        .type         = SENSOR_TYPE_HUMAN,
        .name         = "HUMAN",
        .idx          = SENSOR_IDX_HUMAN,
        .addr         = SENSOR_ADDR_HUMAN,
        .function_code= MODBUS_FC_READ_HOLDING_REGS,
        .start_reg    = SENSOR_REG_HUMAN,
        .reg_count    = SENSOR_REGCNT_HUMAN,
        .rs485_port   = 0U,
        .scale_0      = 1.0f, .scale_1 = 1.0f,
        /* human: 0 ~ 10 */
        .raw_min_0    = 0,  .raw_max_0 = 10,
        .raw_min_1    = 0,  .raw_max_1 = 0,
        .normal_poll_ms  = 500U,
        .offline_poll_ms = SENSOR_OFFLINE_POLL_MS,
    },
    {
        .type         = SENSOR_TYPE_LIGHT,
        .name         = "LIGHT",
        .idx          = SENSOR_IDX_LIGHT,
        .addr         = SENSOR_ADDR_LIGHT,
        .function_code= MODBUS_FC_READ_HOLDING_REGS,
        .start_reg    = SENSOR_REG_LIGHT,
        .reg_count    = SENSOR_REGCNT_LIGHT,
        .rs485_port   = 0U,
        .scale_0      = 1.0f, .scale_1 = 1.0f,
        /* light: 0 ~ 65535 lux */
        .raw_min_0    = 0, .raw_max_0 = 65535,
        .raw_min_1    = 0, .raw_max_1 = 0,
        .normal_poll_ms  = 500U,
        .offline_poll_ms = SENSOR_OFFLINE_POLL_MS,
    },
    {
        .type         = SENSOR_TYPE_CO2,
        .name         = "CO2",
        .idx          = SENSOR_IDX_CO2,
        .addr         = SENSOR_ADDR_CO2,
        .function_code= MODBUS_FC_READ_HOLDING_REGS,
        .start_reg    = SENSOR_REG_CO2,
        .reg_count    = SENSOR_REGCNT_CO2,
        .rs485_port   = 0U,
        .scale_0      = 1.0f, .scale_1 = 1.0f,
        /* co2: 300 ~ 10000 ppm */
        .raw_min_0    = 300,  .raw_max_0 = 10000,
        .raw_min_1    = 0,    .raw_max_1 = 0,
        .normal_poll_ms  = 500U,
        .offline_poll_ms = SENSOR_OFFLINE_POLL_MS,
    },
};

#define SENSOR_TABLE_SIZE (sizeof(s_sensor_cfg) / sizeof(s_sensor_cfg[0])) //有几个传感器

/* ------------------------------------------------------------------ */
/*  Helper: cast uint16 register as signed int16                       */
//把 uint16_t 寄存器值转换成 int16_t
/* ------------------------------------------------------------------ */
static inline int16_t Sensor_RegToI16(uint16_t r)
{
    return (int16_t)r;
}

/* ------------------------------------------------------------------ */
/*  Modbus read + comm stats update                                    */
//读取 Modbus 传感器
/* ------------------------------------------------------------------ */
static SensorResult_t Sensor_Modbus_ReadRegisters(const SensorConfig_t *cfg,
                                                   uint16_t *regs,
                                                   uint8_t  *reg_count_out)
{
    uint8_t  tx_buf[8];
    uint8_t  rx_buf[64];
    uint16_t tx_len  = Modbus_BuildReadHoldingRegisters(cfg->addr, cfg->start_reg,
                                                        cfg->reg_count, tx_buf);
    uint16_t exp_len = Modbus_GetExpectedResponseLength(cfg->reg_count);

    g_app_state.comm[cfg->rs485_port].tx_count++;
    BSP_RS485_Send((BSP_RS485_Port_t)cfg->rs485_port, tx_buf, tx_len);

    HAL_StatusTypeDef ret = BSP_RS485_Receive((BSP_RS485_Port_t)cfg->rs485_port,
                                               rx_buf, exp_len, 200U);
    if (ret != HAL_OK) {
        g_app_state.comm[cfg->rs485_port].timeout_count++;
        g_app_state.comm[cfg->rs485_port].err_count++;
        return SENSOR_RESULT_TIMEOUT;
    }

    g_app_state.comm[cfg->rs485_port].rx_count++;

    if (!Modbus_CheckResponseCRC(rx_buf, exp_len)) {
        g_app_state.comm[cfg->rs485_port].err_count++;
        return SENSOR_RESULT_CRC_ERROR;
    }
    if (rx_buf[0] != cfg->addr) {
        g_app_state.comm[cfg->rs485_port].err_count++;
        return SENSOR_RESULT_ADDR_ERROR;
    }
    if (rx_buf[1] != MODBUS_FC_READ_HOLDING_REGS) {
        g_app_state.comm[cfg->rs485_port].err_count++;
        return SENSOR_RESULT_FUNC_ERROR;
    }

    uint8_t got = (uint8_t)Modbus_ParseReadRegistersResponse(rx_buf, exp_len, regs);
    *reg_count_out = got;
    return (got == cfg->reg_count) ? SENSOR_RESULT_OK : SENSOR_RESULT_LEN_ERROR;
}

/* ------------------------------------------------------------------ */
/*  Per-type parse functions                                           */
/*  Return 1 = data valid, 0 = data out of range                      */
//把温湿度传感器返回的原始寄存器值解析成真实温度、湿度，并检查数据是否合理
/* ------------------------------------------------------------------ */
static uint8_t Sensor_ParseTempHumi(const SensorConfig_t *cfg,
                                     const uint16_t *regs, uint8_t reg_count,
                                     float *v0_out, float *v1_out)
{
    if (reg_count < 2U) { return 0U; } //温湿度传感器至少需要 2 个寄存器

    //解析原始温度和湿度
    int16_t raw_temp = Sensor_RegToI16(regs[0]);
    int16_t raw_humi = (int16_t)regs[1]; /* humidity is unsigned, but cast safe */
    uint16_t u_humi  = regs[1];

    //检查温度原始值范围是否合理
    if (raw_temp < (int16_t)cfg->raw_min_0 || raw_temp > (int16_t)cfg->raw_max_0) {
        BSP_Log_Printf("[DATA] TH invalid temp_raw=%d humi_raw=%u\r\n",
                       (int)raw_temp, (unsigned)u_humi);
        return 0U;
    }
    //检查湿度原始值范围是否合理
    if ((int32_t)u_humi < cfg->raw_min_1 || (int32_t)u_humi > cfg->raw_max_1) {
        BSP_Log_Printf("[DATA] TH invalid temp_raw=%d humi_raw=%u\r\n",
                       (int)raw_temp, (unsigned)u_humi);
        return 0U;
    }
    (void)raw_humi;
    //计算真实温湿度值
    *v0_out = (float)raw_temp / cfg->scale_0;
    *v1_out = (float)u_humi  / cfg->scale_1;
    return 1U;
}

//解析人体存在/人体感应传感器的寄存器数据，并判断数据是否合法
static uint8_t Sensor_ParseHuman(const SensorConfig_t *cfg,
                                  const uint16_t *regs, uint8_t reg_count,
                                  float *v0_out)
{
    if (reg_count < 1U) { return 0U; }//人体传感器至少需要 1 个寄存器
    uint16_t raw = regs[0];
    if ((int32_t)raw < cfg->raw_min_0 || (int32_t)raw > cfg->raw_max_0) {
        BSP_Log_Printf("[DATA] HUMAN invalid raw=%u\r\n", (unsigned)raw);
        return 0U;
    }
    *v0_out = (float)raw;//把原始值转换成 float 输出，传感器的数据本身就是状态值，不需要缩放
    return 1U;
}

static uint8_t Sensor_ParseLight(const SensorConfig_t *cfg,
                                  const uint16_t *regs, uint8_t reg_count,
                                  float *v0_out)
{
    if (reg_count < 1U) { return 0U; }
    uint16_t raw = regs[0];
    /* light allows full uint16 range; raw_max_0 = 65535 */
    if ((int32_t)raw < cfg->raw_min_0) {
        BSP_Log_Printf("[DATA] LIGHT invalid raw=%u\r\n", (unsigned)raw);
        return 0U;
    }
    *v0_out = (float)raw;
    return 1U;
}

static uint8_t Sensor_ParseCO2(const SensorConfig_t *cfg,
                                 const uint16_t *regs, uint8_t reg_count,
                                 float *v0_out)
{
    if (reg_count < 1U) { return 0U; }
    uint16_t raw = regs[0];
    if ((int32_t)raw < cfg->raw_min_0 || (int32_t)raw > cfg->raw_max_0) {
        BSP_Log_Printf("[DATA] CO2 invalid raw=%u\r\n", (unsigned)raw);
        return 0U;
    }
    *v0_out = (float)raw;
    return 1U;
}

/* ------------------------------------------------------------------ */
/*  Update g_app_state after one poll attempt                          */
//更新传感器状态
/* ------------------------------------------------------------------ */
static void Sensor_UpdateState(uint8_t idx, SensorResult_t result,
                                const uint16_t *regs, uint8_t reg_count)
{
    const SensorConfig_t *cfg = &s_sensor_cfg[idx];
    float v0 = 0.0f, v1 = 0.0f;
    uint8_t data_ok  = 0U;
    uint8_t was_online;
    uint8_t now_online;
    uint8_t fail_cnt;
    uint32_t invalid_cnt = 0U;

    App_StateLock();
    SensorData_t *s = &g_app_state.sensors[idx];
    was_online = s->online;
    //更新全局状态中的传感器信息：在线状态、失败次数、最近更新的时间、寄存器原始值、数据有效值、数据质量、最近有效数据时间、无效数据数量等等
    if (result == SENSOR_RESULT_OK) {
        /* --- parse by type --- */
        uint8_t parse_ok = 0U;
        if (cfg->type == SENSOR_TYPE_TEMP_HUMI) {
            parse_ok = Sensor_ParseTempHumi(cfg, regs, reg_count, &v0, &v1);
        } else if (cfg->type == SENSOR_TYPE_HUMAN) {
            parse_ok = Sensor_ParseHuman(cfg, regs, reg_count, &v0);
        } else if (cfg->type == SENSOR_TYPE_LIGHT) {
            parse_ok = Sensor_ParseLight(cfg, regs, reg_count, &v0);
        } else if (cfg->type == SENSOR_TYPE_CO2) {
            parse_ok = Sensor_ParseCO2(cfg, regs, reg_count, &v0);
        }

        s->status         = SENSOR_STATUS_ONLINE;
        s->online         = 1U;
        s->fail_count     = 0U;
        s->last_update_tick = osKernelGetTickCount();
        memcpy(s->regs, regs, reg_count * sizeof(uint16_t));//更新原始寄存器数据
        s->reg_count = reg_count;

        if (parse_ok) {
            s->values[0]      = v0;
            s->values[1]      = v1;
            s->quality        = SENSOR_DATA_VALID;
            s->last_valid_tick = osKernelGetTickCount();
            data_ok = 1U;
        } else {
            s->invalid_count++;
            invalid_cnt = s->invalid_count;
            /* keep previous values[]; set STALE if we had valid data before */
            s->quality = (s->last_valid_tick != 0U) ? SENSOR_DATA_STALE : SENSOR_DATA_INVALID;
        }
    } else {
        /* --- comm failure --- */
        s->fail_count++;
        if (result == SENSOR_RESULT_CRC_ERROR) {
            s->crc_err_count++;
            s->status = SENSOR_STATUS_CRC_ERROR;
        } else if (result == SENSOR_RESULT_ADDR_ERROR ||
                   result == SENSOR_RESULT_FUNC_ERROR ||
                   result == SENSOR_RESULT_LEN_ERROR) {
            s->proto_err_count++;
            s->status = SENSOR_STATUS_DATA_ERROR;
        } else {
            s->status = SENSOR_STATUS_TIMEOUT;
        }
        if (s->fail_count >= SENSOR_FAIL_THRESHOLD) {
            s->status = SENSOR_STATUS_OFFLINE;
            s->online = 0U;
            /* keep last valid values; mark STALE if we had them */
            if (s->quality == SENSOR_DATA_VALID) {
                s->quality = SENSOR_DATA_STALE;
            }
        }
    }

    now_online = s->online;
    fail_cnt   = s->fail_count;
    App_StateUnlock();

    /* ---------------------------------------------------------------- */
    /* Logging — only outside the lock, only on state change or success */
    /* ---------------------------------------------------------------- */
    //传感器数据正常时，打印数据
    if (result == SENSOR_RESULT_OK && data_ok) {
        if (cfg->type == SENSOR_TYPE_TEMP_HUMI) {
            BSP_Log_Printf("[MB] TH ok temp=%.1f humi=%.1f\r\n",
                           (double)v0, (double)v1);
        } else if (cfg->type == SENSOR_TYPE_HUMAN) {
            BSP_Log_Printf("[MB] HUMAN ok exist=%u\r\n", (unsigned)v0);
        } else if (cfg->type == SENSOR_TYPE_LIGHT) {
            BSP_Log_Printf("[MB] LIGHT ok lux=%u\r\n", (unsigned)v0);
        } else if (cfg->type == SENSOR_TYPE_CO2) {
            BSP_Log_Printf("[MB] CO2 ok ppm=%u\r\n", (unsigned)v0);
        }
    }

    /* State-change logs */
    //传感器状态发生变化时或者数据错误时，打印状态变更或数据错误，并添加事件记录
    if (!was_online && now_online) {
        BSP_Log_Printf("[COMM] %s recovered\r\n", cfg->name);//
        App_EventLog_Add(APP_EVENT_COMM_RECOVERED, idx, 0, 0.0f, "%s recovered", cfg->name);
    } else if (was_online && !now_online) {
        BSP_Log_Printf("[COMM] %s offline\r\n", cfg->name);
        App_EventLog_Add(APP_EVENT_COMM_OFFLINE, idx, 0, 0.0f, "%s offline", cfg->name);
    } else if (!now_online && result == SENSOR_RESULT_CRC_ERROR && fail_cnt == 1U) {
        BSP_Log_Printf("[COMM] %s crc_error\r\n", cfg->name);
        App_EventLog_Add(APP_EVENT_COMM_CRC_ERROR, idx, 0, 0.0f, "%s crc_error", cfg->name);
    } else if (result != SENSOR_RESULT_OK && result != SENSOR_RESULT_CRC_ERROR
               && fail_cnt < SENSOR_FAIL_THRESHOLD && (fail_cnt % SENSOR_FAIL_THRESHOLD) == 1U) {
        BSP_Log_Printf("[MB] %s fail=%u\r\n", cfg->name, (unsigned)fail_cnt);
    }

    /* Log data-invalid only on the first occurrence (invalid_cnt == 1) */
    //记录第一次无效数据事件
    if (result == SENSOR_RESULT_OK && invalid_cnt == 1U) {
        App_EventLog_Add(APP_EVENT_SENSOR_DATA_INVALID, idx,
            (int32_t)invalid_cnt, 0.0f, "%s data invalid", cfg->name);
    }
}

/* ------------------------------------------------------------------ */
/*  SensorTask                                                         */
/* ------------------------------------------------------------------ */
void App_SensorTask(void *arg)
{
    (void)arg;

    /* Init global sensor metadata */
    //把配置表里的 Modbus 地址写入全局状态
    for (uint8_t i = 0U; i < SENSOR_TABLE_SIZE; i++) {
        g_app_state.sensors[i].slave_addr = s_sensor_cfg[i].addr;
        g_app_state.sensors[i].status     = SENSOR_STATUS_UNKNOWN;//表示刚启动时还不知道传感器状态。
        g_app_state.sensors[i].quality    = SENSOR_DATA_INVALID;//表示当前数据无效
        s_rt[i].prev_online                = 1U; /* assume online so first failure is detected 先假设传感器在线，这样第一次读取失败时，就能检测到“从在线变成离线”的状态变化。*/
        s_rt[i].last_poll_tick             = 0U;
    }

    BSP_Log_Printf("[APP] SensorTask started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_SENSOR);
        uint32_t now = osKernelGetTickCount();

        /* Refresh addresses and poll interval from config */
        //从全局配置刷新传感器参数，每轮循环都从系统配置里读取最新的传感器地址、总线选择、轮询周期。
        App_StateLock();
        uint32_t poll_ms = g_app_state.config.sensor_poll_interval_ms;////传感器正常在线时的轮询周期
        s_sensor_cfg[SENSOR_IDX_TH].addr    = g_app_state.config.temp_humi_addr;
        s_sensor_cfg[SENSOR_IDX_HUMAN].addr = g_app_state.config.human_addr;
        s_sensor_cfg[SENSOR_IDX_LIGHT].addr = g_app_state.config.light_addr;
        s_sensor_cfg[SENSOR_IDX_CO2].addr   = g_app_state.config.co2_addr;
        s_sensor_cfg[SENSOR_IDX_TH].rs485_port    = g_app_state.config.temp_humi_bus;
        s_sensor_cfg[SENSOR_IDX_HUMAN].rs485_port = g_app_state.config.human_bus;
        s_sensor_cfg[SENSOR_IDX_LIGHT].rs485_port = g_app_state.config.light_bus;
        s_sensor_cfg[SENSOR_IDX_CO2].rs485_port   = g_app_state.config.co2_bus;
        App_StateUnlock();

        /* Clamp poll interval */
        //限制轮询周期范围
        if (poll_ms < 100U || poll_ms > 10000U) { poll_ms = 500U; }

        /* Update normal_poll_ms from config for every sensor */
        //更新每个传感器的正常轮询周期(所有传感器使用同一个正常轮询周期。)
        for (uint8_t i = 0U; i < SENSOR_TABLE_SIZE; i++) {
            s_sensor_cfg[i].normal_poll_ms = poll_ms;
        }

        /* Poll each sensor that is due */
        //逐个判断传感器是否该轮询
        for (uint8_t i = 0U; i < SENSOR_TABLE_SIZE; i++) {           
            SensorConfig_t *cfg = &s_sensor_cfg[i];
            uint8_t is_offline;

            App_StateLock();
            is_offline = (g_app_state.sensors[i].online == 0U &&
                          g_app_state.sensors[i].status != SENSOR_STATUS_UNKNOWN); //读取当前传感器是否离线
            App_StateUnlock();
            
            //传感器在线时可以频繁读取；传感器离线时就降低重试频率
            uint32_t interval = is_offline ? cfg->offline_poll_ms : cfg->normal_poll_ms;
            
            //如果距离上次轮询还没超过指定周期，就跳过这个传感器
            if ((now - s_rt[i].last_poll_tick) < interval) {
                continue; /* not due yet */
            }
            //记录本次轮询时间
            s_rt[i].last_poll_tick = now;

            uint16_t regs[SENSOR_REG_MAX]; //准备接收寄存器数据
            uint8_t  reg_count = 0U;

            SensorResult_t result = Sensor_Modbus_ReadRegisters(cfg, regs, &reg_count);
            Sensor_UpdateState(i, result, regs, reg_count);
        }

        osDelay(50U); /* short sleep; per-sensor timing controlled by last_poll_tick */
    }
}
