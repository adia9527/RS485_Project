#ifndef APP_SENSOR_CONFIG_H
#define APP_SENSOR_CONFIG_H

#include <stdint.h>

/* Sensor indices 传感器下标 */
#define SENSOR_IDX_TH       0U
#define SENSOR_IDX_HUMAN    1U
#define SENSOR_IDX_LIGHT    2U
#define SENSOR_IDX_CO2      3U

/* Modbus slave addresses (defaults; overridden at runtime from g_app_state.config) */
//传感器从机地址
#define SENSOR_ADDR_TH      0x01U
#define SENSOR_ADDR_HUMAN   0x02U
#define SENSOR_ADDR_LIGHT   0x03U
#define SENSOR_ADDR_CO2     0x04U

/* Modbus register start addresses (placeholder — adjust per sensor datasheet) */
//各个传感器Modbus寄存器的起始地址
#define SENSOR_REG_TH       0x0000U
#define SENSOR_REG_HUMAN    0x0000U
#define SENSOR_REG_LIGHT    0x0000U
#define SENSOR_REG_CO2      0x0000U

/* Register counts */
//各个传感器的读数据寄存器数量
#define SENSOR_REGCNT_TH    2U
#define SENSOR_REGCNT_HUMAN 1U
#define SENSOR_REGCNT_LIGHT 1U
#define SENSOR_REGCNT_CO2   1U

/* Scale factors */
//温湿度数据缩放值
#define SENSOR_SCALE_TEMP   10.0f
#define SENSOR_SCALE_HUMI   10.0f

/* Offline back-off interval (ms) */
//传感器离线时轮询周期
#define SENSOR_OFFLINE_POLL_MS  5000U

/* Consecutive failures before declaring OFFLINE 连续失败达到 3 次后，才把传感器判定为离线 */
#define SENSOR_FAIL_THRESHOLD   3U

/* Sensor type */
typedef enum {
    SENSOR_TYPE_TEMP_HUMI = 0,
    SENSOR_TYPE_HUMAN,
    SENSOR_TYPE_LIGHT,
    SENSOR_TYPE_CO2,
    SENSOR_TYPE_UNKNOWN
} SensorType_t;

/* Full sensor configuration — one entry per sensor slot */
//传感器配置结构体
typedef struct {
    SensorType_t  type; //传感器类型
    const char   *name;

    uint8_t   idx;           /* index into g_app_state.sensors[] 对应 g_app_state.sensors[] 数组里的下标  */
    uint8_t   addr;          /* current Modbus slave address Modbus 从机地址       */
    uint8_t   function_code; /* Modbus function code Modbus 功能码               */
    uint16_t  start_reg;     /* first register address 起始寄存器地址             */
    uint8_t   reg_count;     /* number of registers to read 要读取的寄存器数量        */
    uint8_t   rs485_port;    /* BSP_RS485_Port_t 使用哪一路 RS485                   */

    float    scale_0;        /* divisor for values[0] 第 1 个数据的缩放系数，很多 Modbus 传感器返回的是整数原始值，不是直接返回浮点数。*/
    float    scale_1;        /* divisor for values[1] 第 2 个数据的缩放系数              */

    /* Valid range for raw register values (before scaling) */
    int32_t  raw_min_0; //第 1 个原始值最小合法值
    int32_t  raw_max_0; //第 1 个原始值最大合法值
    int32_t  raw_min_1; //第 2 个原始值最小合法值
    int32_t  raw_max_1; //第 2 个原始值最大合法值

    uint32_t normal_poll_ms;  /* poll interval when online  在线时轮询周期        */
    uint32_t offline_poll_ms; /* poll interval when offline 离线时轮询周期        */
} SensorConfig_t;

/* Legacy descriptor kept for compatibility */
typedef struct {
    uint8_t  idx;
    uint8_t  addr;
    uint16_t start_reg;
    uint8_t  reg_count;
    uint8_t  rs485_port;
    const char *name;
} SensorDesc_t;

#endif /* APP_SENSOR_CONFIG_H */
