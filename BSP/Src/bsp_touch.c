#include "bsp_touch.h"
#include "main.h"
#include "i2c.h"
#include "cmsis_os.h"

/* 7-bit addresses left-shifted to 8-bit HAL format */
#define TOUCH_ADDR_GT911_1  (0x5DU << 1U)
#define TOUCH_ADDR_GT911_2  (0x14U << 1U)
#define TOUCH_ADDR_FT6336   (0x38U << 1U)
#define TOUCH_ADDR_CST816   (0x15U << 1U)

#define TOUCH_I2C_TIMEOUT_MS  50U

/* FT6336 registers */
#define FT6336_REG_TD_STATUS  0x02U//触摸点状态寄存器地址
#define FT6336_REG_P1_XH      0x03U/*第 1 个触摸点 X 坐标高字节寄存器地址（X和Y各两个字节记录），FT6336 的坐标通常是多个寄存器连续保存，
                                     所以程序一般会从 0x03 开始连续读取 4 个字节 */

static BSP_TouchIc_t s_ic = BSP_TOUCH_IC_UNKNOWN;
static uint8_t       s_addr = TOUCH_ADDR_FT6336;

static void Touch_Delay(uint32_t ms)
{
    if (osKernelGetState() == osKernelRunning) {
        osDelay(ms);
    } else {
        HAL_Delay(ms);
    }
}

void BSP_Touch_Reset(void)
{
    HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_RESET);
    Touch_Delay(10U);
    HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_SET);
    Touch_Delay(100U);
}

uint8_t BSP_Touch_IsIntActive(void)
{
    return (HAL_GPIO_ReadPin(CTP_INT_GPIO_Port, CTP_INT_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

HAL_StatusTypeDef BSP_Touch_ReadReg(uint16_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t reg8 = (uint8_t)reg;
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c1, s_addr, &reg8, 1U,
                                                    TOUCH_I2C_TIMEOUT_MS);
    if (st != HAL_OK) { return st; }
    return HAL_I2C_Master_Receive(&hi2c1, s_addr, buf, len, TOUCH_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef BSP_Touch_WriteReg(uint16_t reg, const uint8_t *buf, uint16_t len)
{
    uint8_t tx[32];
    if (len > (uint16_t)(sizeof(tx) - 1U)) { return HAL_ERROR; }
    tx[0] = (uint8_t)reg;
    for (uint16_t i = 0U; i < len; i++) { tx[i + 1U] = buf[i]; }
    return HAL_I2C_Master_Transmit(&hi2c1, s_addr, tx, (uint16_t)(len + 1U),
                                   TOUCH_I2C_TIMEOUT_MS);
}

//读取LCD屏幕触摸芯片ID
BSP_TouchIc_t BSP_Touch_DetectIc(void)
{
    uint8_t val = 0U;

    /* Try FT6336: read chip ID register 0xA3 */
    s_addr = TOUCH_ADDR_FT6336;
    uint8_t reg = 0xA3U;
    if (HAL_I2C_Master_Transmit(&hi2c1, s_addr, &reg, 1U, TOUCH_I2C_TIMEOUT_MS) == HAL_OK &&
        HAL_I2C_Master_Receive(&hi2c1, s_addr, &val, 1U, TOUCH_I2C_TIMEOUT_MS) == HAL_OK) {
        /* FT6336 chip ID is 0x36 or 0x64 */
        if (val == 0x36U || val == 0x64U || val != 0x00U) {
            s_ic = BSP_TOUCH_IC_FT6336;
            return s_ic;
        }
    }

    s_ic = BSP_TOUCH_IC_UNKNOWN;
    return s_ic;
}

void BSP_Touch_Init(void)
{
    BSP_Touch_Reset();
    s_ic = BSP_Touch_DetectIc();
}

uint8_t BSP_Touch_ReadPoint(BSP_TouchPoint_t *point)
{
    if (point == NULL) { return 0U; }
    point->pressed = 0U;
    point->x = 0U;
    point->y = 0U;
    point->points = 0U;

    if (s_ic == BSP_TOUCH_IC_UNKNOWN) { return 0U; }

    if (s_ic == BSP_TOUCH_IC_FT6336) {
        uint8_t buf[6];
        uint8_t reg = FT6336_REG_TD_STATUS;
        if (HAL_I2C_Master_Transmit(&hi2c1, s_addr, &reg, 1U, TOUCH_I2C_TIMEOUT_MS) != HAL_OK) {
            return 0U;
        }
        if (HAL_I2C_Master_Receive(&hi2c1, s_addr, buf, sizeof(buf), TOUCH_I2C_TIMEOUT_MS) != HAL_OK) {
            return 0U;
        }
        /*
        | `buf` 下标 | 对应寄存器  | 含义              |
        | -------: | ------ | --------------- |
        | `buf[0]` | `0x02` | TD_STATUS，触摸点数量 |
        | `buf[1]` | `0x03` | P1_XH           |
        | `buf[2]` | `0x04` | P1_XL           |
        | `buf[3]` | `0x05` | P1_YH           |
        | `buf[4]` | `0x06` | P1_YL           |
        | `buf[5]` | `0x07` | 后续信息，这里没有用到     |
        */
        uint8_t td = buf[0] & 0x0FU;//低 4 位通常表示触摸点数量
        if (td == 0U || td > 2U) { return 0U; } //如果没有触摸，则返回0
        point->points  = td;
        point->x       = (uint16_t)(((uint16_t)(buf[1] & 0x0FU) << 8U) | buf[2]);
        point->y       = (uint16_t)(((uint16_t)(buf[3] & 0x0FU) << 8U) | buf[4]);
        point->pressed = 1U;
        return 1U;
    }

    return 0U;
}

const char *BSP_Touch_IcToString(BSP_TouchIc_t ic)
{
    switch (ic) {
        case BSP_TOUCH_IC_GT911:  return "GT911";
        case BSP_TOUCH_IC_FT6336: return "FT6336";
        case BSP_TOUCH_IC_CST816: return "CST816";
        default:                  return "UNKNOWN";
    }
}
