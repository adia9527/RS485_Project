#include "app_alarm.h"
#include "app_alarm_config.h"
#include "app_types.h"
#include "app_sensor_config.h"
#include "app_config.h"
#include "app_health.h"
#include "app_event_log.h"
#include "bsp_led.h"
#include "bsp_beep.h"
#include "bsp_log.h"
#include "cmsis_os.h"

#define ALARM_TASK_PERIOD_MS  200U
#define BEEP_ON_MS            200U
#define BEEP_OFF_MS           800U

/* ADC channel indices */
#define ADC_CH_VIN   0U
#define ADC_CH_AIN1  1U
#define ADC_CH_AIN2  2U

static uint8_t s_muted = 0U; //警报控制

void App_Alarm_Mute(void)
{
    s_muted = 1U;
    BSP_Log_Printf("[ALARM] muted\r\n");
    App_EventLog_Add(APP_EVENT_ALARM_MUTED, 0U, 0, 0.0f, "alarm muted");
}

void App_Alarm_Unmute(void)
{
    s_muted = 0U;
}

/* -------- alarm source name lookup -------- */
static const char *Alarm_SrcName(AlarmSource_t src)
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

/* -------- evaluate highest-priority alarm -------- */
//告警评估结构体
typedef struct {
    AlarmSource_t source;
    AlarmLevel_t  level;
    float         value;
    float         threshold;
} AlarmCandidate_t;



//做告警评估，也就是根据当前系统状态检查传感器在线状态、电源电压、CO₂、温湿度、光照、ADC 模拟输入等数据，然后把结果填到 out 这个结构体里
static void Alarm_Evaluate(AlarmCandidate_t *out)
{
    out->source    = ALARM_SRC_NONE;
    out->level     = ALARM_NONE;
    out->value     = 0.0f;
    out->threshold = 0.0f;

    //加锁，从 g_app_state 全局状态中复制一份当前传感器数据、ADC 数据、系统配置，然后立刻解锁。
    App_StateLock();
    SensorData_t snap[SENSOR_COUNT];
    AdcData_t    adc_snap;
    SysConfig_t  cfg;
    //传感器数据快照
    for (uint8_t i = 0U; i < SENSOR_COUNT; i++) snap[i] = g_app_state.sensors[i];
    //ADC数据快照
    adc_snap = g_app_state.adc;
    //系统配置快照
    cfg      = g_app_state.config;
    App_StateUnlock();

    if (!cfg.alarm_enable) { return; }
    
    //告警优先级设置，按优先级找出第一个最重要的告警，然后立刻 return。
    /*
    | 优先级 | 告警类型               |
    | --: | ------------------ |
    |   1 | 通信失败               |
    |   2 | 电源电压过低             |
    |   3 | CO₂ 过高             |
    |   4 | 温度过高 / 过低          |
    |   5 | 湿度过高 / 过低          |
    |   6 | 光照过高 / 过低          |
    |   7 | AIN1 / AIN2 模拟输入过高 |
    */    

    /* Priority 1 – communication failure */
    for (uint8_t i = 0U; i < SENSOR_COUNT; i++) {
        if (snap[i].status == SENSOR_STATUS_OFFLINE) {
            out->source    = ALARM_SRC_COMM_FAIL;
            out->level     = ALARM_CRITICAL;
            out->value     = (float)i;
            out->threshold = 0.0f;
            return;
        }
    }

    /* Priority 2 – power */
    if (adc_snap.voltage[ADC_CH_VIN] < cfg.vin_low_threshold) {
        out->source    = ALARM_SRC_POWER_LOW;
        out->level     = ALARM_CRITICAL;
        out->value     = adc_snap.voltage[ADC_CH_VIN];
        out->threshold = cfg.vin_low_threshold;
        return;
    }

    /* Priority 3 – CO2 */
    if (snap[SENSOR_IDX_CO2].status == SENSOR_STATUS_ONLINE) {
        float v = snap[SENSOR_IDX_CO2].values[0];
        if (v > (float)cfg.co2_high_threshold) {
            out->source    = ALARM_SRC_CO2_HIGH;
            out->level     = ALARM_WARN;
            out->value     = v;
            out->threshold = (float)cfg.co2_high_threshold;
            return;
        }
    }

    /* Priority 4 – temperature / Priority 5 – humidity */
    if (snap[SENSOR_IDX_TH].status == SENSOR_STATUS_ONLINE) {
        float temp = snap[SENSOR_IDX_TH].values[0];
        float humi = snap[SENSOR_IDX_TH].values[1];
        if (temp > cfg.temp_high_threshold) {
            out->source = ALARM_SRC_TEMP_HIGH; out->level = ALARM_WARN;
            out->value = temp; out->threshold = cfg.temp_high_threshold; return;
        }
        if (temp < cfg.temp_low_threshold) {
            out->source = ALARM_SRC_TEMP_LOW; out->level = ALARM_WARN;
            out->value = temp; out->threshold = cfg.temp_low_threshold; return;
        }
        if (humi > cfg.humi_high_threshold) {
            out->source = ALARM_SRC_HUMI_HIGH; out->level = ALARM_WARN;
            out->value = humi; out->threshold = cfg.humi_high_threshold; return;
        }
        if (humi < cfg.humi_low_threshold) {
            out->source = ALARM_SRC_HUMI_LOW; out->level = ALARM_WARN;
            out->value = humi; out->threshold = cfg.humi_low_threshold; return;
        }
    }

    /* Priority 6 – light */
    if (snap[SENSOR_IDX_LIGHT].status == SENSOR_STATUS_ONLINE) {
        float lux = snap[SENSOR_IDX_LIGHT].values[0];
        if (lux > (float)cfg.light_high_threshold) {
            out->source = ALARM_SRC_LIGHT_HIGH; out->level = ALARM_WARN;
            out->value = lux; out->threshold = (float)cfg.light_high_threshold; return;
        }
        if (lux < (float)cfg.light_low_threshold) {
            out->source = ALARM_SRC_LIGHT_LOW; out->level = ALARM_WARN;
            out->value = lux; out->threshold = (float)cfg.light_low_threshold; return;
        }
    }

    /* Priority 7 – AIN1 / AIN2 */
    if (adc_snap.voltage[ADC_CH_AIN1] > cfg.ain1_high_threshold) {
        out->source = ALARM_SRC_AIN1_HIGH; out->level = ALARM_WARN;
        out->value = adc_snap.voltage[ADC_CH_AIN1]; out->threshold = cfg.ain1_high_threshold; return;
    }
    if (adc_snap.voltage[ADC_CH_AIN2] > cfg.ain2_high_threshold) {
        out->source = ALARM_SRC_AIN2_HIGH; out->level = ALARM_WARN;
        out->value = adc_snap.voltage[ADC_CH_AIN2]; out->threshold = cfg.ain2_high_threshold; return;
    }
}

//循环检测当前系统是否有告警,并根据告警状态：
/*
    1. 更新全局告警状态
    2. 打印告警日志
    3. 控制蜂鸣器
    4. 控制告警 LED
    5. 支持静音 muted 状态
*/
void App_AlarmTask(void *arg)
{
    (void)arg;

    AlarmCandidate_t candidate; //当前这一轮评估出来的告警候选结果 
    AlarmSource_t    prev_source = ALARM_SRC_NONE; //上一轮的告警来源，用来判断告警是否发生变化
    uint32_t         beep_tick   = 0U; //蜂鸣器闪烁/鸣叫周期的起点时间
    uint8_t          beep_state  = 0U; //当前蜂鸣器是否正在响，`1` 表示响，`0` 表示不响

    BSP_Log_Printf("[APP] AlarmTask started\r\n");

    for (;;) {
        App_Health_Beat(APP_TASK_ID_ALARM);
        /* --- evaluate current alarm --- */
        Alarm_Evaluate(&candidate);

        /* --- detect state change and log --- */
        //检测告警状态是否变化:当前告警来源和上一轮是否不同？
        if (candidate.source != prev_source) {
            //更新全局告警状态
            App_StateLock();
            g_app_state.alarm.active        = (candidate.source != ALARM_SRC_NONE) ? 1U : 0U; 
            g_app_state.alarm.source        = candidate.source;
            g_app_state.alarm.level         = candidate.level;
            g_app_state.alarm.current_value = candidate.value;
            g_app_state.alarm.threshold     = candidate.threshold;
            g_app_state.alarm.trigger_tick  = osKernelGetTickCount();
            App_StateUnlock();

            if (candidate.source != ALARM_SRC_NONE) {
                if (candidate.source == ALARM_SRC_COMM_FAIL) {
                    BSP_Log_Printf("[ALARM] active src=COMM_FAIL sensor=%u\r\n",
                        (unsigned)(uint32_t)candidate.value);
                    App_EventLog_Add(APP_EVENT_ALARM_ACTIVE, (uint8_t)candidate.source,
                        (int32_t)candidate.value, 0.0f,
                        "COMM_FAIL sensor=%u", (unsigned)(uint32_t)candidate.value);
                } else {
                    BSP_Log_Printf("[ALARM] active src=%s value=%.1f threshold=%.1f\r\n",
                        Alarm_SrcName(candidate.source),
                        (double)candidate.value,
                        (double)candidate.threshold);
                    App_EventLog_Add(APP_EVENT_ALARM_ACTIVE, (uint8_t)candidate.source,
                        0, candidate.value,
                        "%s value=%.1f thr=%.1f",
                        Alarm_SrcName(candidate.source),
                        (double)candidate.value, (double)candidate.threshold);
                }
            } else {
                BSP_Log_Printf("[ALARM] cleared\r\n");
                s_muted = 0U;
                App_EventLog_Add(APP_EVENT_ALARM_CLEARED, 0U, 0, 0.0f, "alarm cleared");
            }
            prev_source = candidate.source;
        }

        /* --- update muted flag in global state --- */
        App_StateLock();
        g_app_state.alarm.muted = s_muted;
        App_StateUnlock();

        /* --- read buzzer/LED enable flags --- */
        App_StateLock();
        uint8_t buzzer_en = g_app_state.config.buzzer_enable; //是否允许蜂鸣器响
        uint8_t led_en    = g_app_state.config.led_alarm_enable; //是否允许 LED 告警闪烁
        App_StateUnlock();

        /* --- beeper and LED3 control --- */
        if (candidate.source == ALARM_SRC_NONE) {
            BSP_Beep_Off();
            BSP_LED_Off(BSP_LED_3);
            beep_state = 0U;
            beep_tick  = osKernelGetTickCount();
        } else {
            //周期性蜂鸣/闪灯效果
            uint32_t now    = osKernelGetTickCount();
            uint32_t period = BEEP_ON_MS + BEEP_OFF_MS;//1 秒为一个周期：前 200ms：蜂鸣器响，LED亮 ;后 800ms：蜂鸣器停，LED灭
            uint32_t phase  = (now - beep_tick) % period;//表示当前处于这个周期里的哪个位置。

            if (phase < BEEP_ON_MS) {
                if (led_en) { BSP_LED_On(BSP_LED_3); }
                /*
                蜂鸣器要响，必须同时满足：
                | 条件            | 含义        |
                | ------------- | --------- |
                | `buzzer_en`   | 配置允许蜂鸣器   |
                | `!s_muted`    | 当前没有被静音   |
                | `!beep_state` | 蜂鸣器当前还没打开 |
                */
                if (buzzer_en && !s_muted && !beep_state) {
                    BSP_Beep_On();
                    beep_state = 1U;
                }
            } else {
                BSP_LED_Off(BSP_LED_3);
                if (beep_state) {
                    BSP_Beep_Off();
                    beep_state = 0U;
                }
            }
        }

        osDelay(ALARM_TASK_PERIOD_MS);
    }
}
