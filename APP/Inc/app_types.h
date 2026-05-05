#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define RS485_PORT_COUNT    2
#define ADC_CHANNEL_COUNT   4
#define SENSOR_REG_MAX      16
#define SENSOR_COUNT        4

typedef enum {
    ALARM_NONE = 0,
    ALARM_WARN,
    ALARM_CRITICAL
} AlarmLevel_t;

typedef enum {
    SENSOR_STATUS_UNKNOWN = 0,
    SENSOR_STATUS_ONLINE,
    SENSOR_STATUS_OFFLINE,
    SENSOR_STATUS_TIMEOUT,
    SENSOR_STATUS_CRC_ERROR,
    SENSOR_STATUS_DATA_ERROR
} SensorStatus_t;

typedef enum {
    SENSOR_DATA_INVALID = 0,
    SENSOR_DATA_VALID,
    SENSOR_DATA_STALE
} SensorDataQuality_t;

typedef enum {
    ALARM_SRC_NONE = 0,
    ALARM_SRC_TEMP_HIGH,
    ALARM_SRC_TEMP_LOW,
    ALARM_SRC_HUMI_HIGH,
    ALARM_SRC_HUMI_LOW,
    ALARM_SRC_CO2_HIGH,
    ALARM_SRC_LIGHT_HIGH,
    ALARM_SRC_LIGHT_LOW,
    ALARM_SRC_HUMAN_DETECT,
    ALARM_SRC_COMM_FAIL,
    ALARM_SRC_POWER_LOW,
    ALARM_SRC_AIN1_HIGH,
    ALARM_SRC_AIN2_HIGH
} AlarmSource_t;

//传感器数据状态结构体
typedef struct {
    uint8_t              slave_addr; //Modbus 从机地址
    uint16_t             regs[SENSOR_REG_MAX]; //原始寄存器数据
    float                values[SENSOR_REG_MAX]; //换算后的浮点数据
    uint8_t              reg_count; //当前有效寄存器数量
    uint8_t              online; // 是否在线
    SensorStatus_t       status; //传感器状态 
    uint8_t              fail_count; //连续失败次数
    uint32_t             last_update_tick; //最近一次更新数据的系统 tick
    uint32_t             crc_err_count; //CRC 校验错误累计次数
    uint32_t             proto_err_count; //协议错误累计次数
    SensorDataQuality_t  quality; //当前数据质量
    uint32_t             last_valid_tick; //最近一次获得有效数据的系统 tick
    uint32_t             invalid_count; //无效数据累计次数
} SensorData_t;

typedef struct {
    uint16_t raw[ADC_CHANNEL_COUNT];
    float    voltage[ADC_CHANNEL_COUNT];
} AdcData_t;

//告警状态结构体
typedef struct {
    uint8_t         active; //当前告警是否正在触发
    AlarmSource_t   source; //告警来源
    AlarmLevel_t    level; //告警等级
    float           current_value; //当前检测到的数值
    float           threshold; //触发告警的阈值
    uint8_t         muted; //蜂鸣器是否被用户静音
    uint32_t        trigger_tick; //
} AlarmState_t;

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t err_count;
    uint32_t timeout_count;
} CommStats_t;

//保存到Flash中的配置参数
typedef struct {
    uint32_t magic; //魔术值，用来判断这份配置是不是有效配置
    uint16_t version; //配置结构体版本号
    uint16_t size; //配置结构体大小

    uint8_t  device_id; //本设备 ID

    //温湿度告警阈值
    float    temp_high_threshold;
    float    temp_low_threshold;
    float    humi_high_threshold;
    float    humi_low_threshold;

    //CO₂ 和光照告警阈值
    uint16_t co2_high_threshold;
    uint16_t light_high_threshold;
    uint16_t light_low_threshold;

    //电源和模拟输入阈值
    float    vin_low_threshold;
    float    ain1_high_threshold;
    float    ain2_high_threshold;

    //RS485 传感器地址配置
    uint8_t  temp_humi_addr;
    uint8_t  human_addr;
    uint8_t  light_addr;
    uint8_t  co2_addr;

    //RS485 传感器总线配置：485_1 485_2 
    uint8_t  temp_humi_bus;
    uint8_t  human_bus;
    uint8_t  light_bus;
    uint8_t  co2_bus;

    uint32_t sensor_poll_interval_ms;   //传感器轮询间隔
    uint32_t upload_period_ms;  //数据上传周期
    uint32_t backlight_timeout_ms;  //背光自动关闭时间

    //告警开关配置
    uint8_t  alarm_enable;  //是否启用告警总功能
    uint8_t  buzzer_enable; //是否允许蜂鸣器报警
    uint8_t  led_alarm_enable;  //是否允许 LED 报警

    //日志开关
    uint8_t  log_enable;

    uint32_t rs485_poll_ms; //RS485 任务轮询周期
    uint32_t adc_poll_ms;   //ADC 任务采样周期

    /* WiFi / MQTT */
    char     wifi_ssid[32]; //WiFi 名称
    char     wifi_password[64];//WiFi 密码
    char     mqtt_host[64];//MQTT 服务器地址
    uint16_t mqtt_port;//MQTT 服务器端口
    char     mqtt_client_id[32];//MQTT 客户端 ID
    char     mqtt_username[32];//MQTT 用户名
    char     mqtt_password[32];//MQTT 密码
    char     mqtt_topic_data[64];//传感器数据上传 topic
    char     mqtt_topic_alarm[64];//告警消息 topic
    char     mqtt_topic_status[64];//设备状态 topic
    uint8_t  mqtt_enable;//是否启用 MQTT 功能
    /* Legacy global MQTT policy, kept for simple commands/backward compatibility. */
    uint8_t  mqtt_qos;//QoS 等级
    uint8_t  mqtt_retain;//旧版全局 MQTT 策略，为了兼容旧命令或简单配置保留下来的

    //MQTT 远程命令 topic，通过 MQTT 下发命令
    char     mqtt_topic_cmd[64];//设备订阅的命令 topic
    char     mqtt_topic_resp[64];//设备回复命令结果的 topic
    char     mqtt_cmd_token[24];//命令校验 token：做一个简单权限校验，避免任何订阅者随便控制设备。它不等于完整安全方案，但比裸奔强一点
    uint8_t  mqtt_cmd_enable;//是否启用 MQTT 命令功能

    /*
     * Per-message MQTT publish policy:
     * DATA is periodic, ALARM/RESP are more important, STATUS is periodic.
     * QoS is limited to 0/1; retain defaults off to avoid stale values.
     */
    //按消息类型分别配置 QoS
    uint8_t  mqtt_qos_data;//周期传感器数据，通常 `0`
    uint8_t  mqtt_qos_alarm;//告警消息，通常 `1`
    uint8_t  mqtt_qos_status;//设备状态，通常 `0` 或 `1`
    uint8_t  mqtt_qos_resp;//命令响应，通常 `1`
    //按消息类型分别配置 retain：retain 会让 broker 保存最后一条消息，每当有新客户端订阅时，都会收到最后一条消息
    uint8_t  mqtt_retain_data;//周期传感器数据，通常 `0`
    uint8_t  mqtt_retain_alarm;//告警消息，看需求，通常 `0`
    uint8_t  mqtt_retain_status;//设备状态，可考虑 `1`，但要处理离线状态
    uint8_t  mqtt_retain_resp;//命令响应，通常 `0`

    //预留字段
    uint8_t  reserved[8];
    //校验值:检查整份配置有没有损坏
    uint32_t checksum;
} SysConfig_t;

//应用程序的全局状态集合
typedef struct {
    //传感器数据数组
    SensorData_t sensors[SENSOR_COUNT];
    //ADC 采集数据，比如电压、电流、模拟量传感器数据
    AdcData_t    adc;
    //报警状态
    AlarmState_t alarm;
    //通信统计信息数组，每个 RS485 端口一份统计数据
    CommStats_t  comm[RS485_PORT_COUNT];
    //系统配置参数
    SysConfig_t  config;
    //系统启动时的 tick 时间
    uint32_t     boot_tick;
    //初始化标志，表示这个状态结构体是否已经初始化完成
    uint8_t      initialized;
} AppState_t;

extern AppState_t g_app_state;

void App_StateLock(void);
void App_StateUnlock(void);

typedef struct {
    uint32_t rx_count;//接收到的 MQTT 命令总数
    uint32_t ok_count;//成功处理的命令数量 
    uint32_t fail_count;//处理失败的命令数量
    uint32_t auth_fail_count;//鉴权失败的命令数量
    uint32_t unsupported_count;//不支持的命令数量  
    uint32_t last_rx_tick;//最近一次收到命令的系统 tick
    uint32_t last_ok_tick;//最近一次成功处理命令的系统 tick
    uint32_t last_fail_tick;//最近一次处理失败的系统 tick
    uint8_t  subscribed;//是否已经订阅 MQTT 命令主题
} MqttCmdStats_t;

#endif /* APP_TYPES_H */
