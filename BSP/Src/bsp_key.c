#include "bsp_key.h"
#include "main.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"

#define KEY_DEBOUNCE_MS    20U
#define KEY_LONGPRESS_MS  800U

static const struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} s_key_map[BSP_KEY_COUNT] = {
    { KEY1_GPIO_Port, KEY1_Pin },
    { KEY2_GPIO_Port, KEY2_Pin },
    { KEY3_GPIO_Port, KEY3_Pin }
};

typedef struct {
    uint8_t  last_raw; //上一次读取到的原始电平
    uint8_t  debounced;//消抖后的稳定状态
    uint32_t press_tick;//按下时刻
    uint8_t  long_reported;//长按事件是否已经上报过
} KeyState_t;

static KeyState_t s_key_state[BSP_KEY_COUNT];

void BSP_Key_Init(void)
{
    for (int i = 0; i < BSP_KEY_COUNT; i++) {
        s_key_state[i].last_raw     = 1U;
        s_key_state[i].debounced    = 1U;
        s_key_state[i].press_tick   = 0U;
        s_key_state[i].long_reported = 0U;
    }
}

BSP_KeyInfo_t BSP_Key_Scan(void)
{
    BSP_KeyInfo_t result = { BSP_KEY_1, BSP_KEY_EVENT_NONE };
    uint32_t now = osKernelGetTickCount();

    for (int i = 0; i < BSP_KEY_COUNT; i++) {
        uint8_t raw = (HAL_GPIO_ReadPin(s_key_map[i].port, s_key_map[i].pin) == GPIO_PIN_RESET) ? 0U : 1U;

        if (raw != s_key_state[i].last_raw) {
            s_key_state[i].last_raw = raw;
        }

        if (raw == 0U && s_key_state[i].debounced == 1U) {
            if ((now - s_key_state[i].press_tick) >= KEY_DEBOUNCE_MS) {
                s_key_state[i].debounced    = 0U;
                s_key_state[i].press_tick   = now;
                s_key_state[i].long_reported = 0U;
                result.key   = (BSP_Key_t)i;
                result.event = BSP_KEY_EVENT_PRESS;
                return result;
            }
        } else if (raw == 0U && s_key_state[i].debounced == 0U) {
            if (!s_key_state[i].long_reported &&
                (now - s_key_state[i].press_tick) >= KEY_LONGPRESS_MS) {
                s_key_state[i].long_reported = 1U;
                result.key   = (BSP_Key_t)i;
                result.event = BSP_KEY_EVENT_LONG_PRESS;
                return result;
            }
        } else if (raw == 1U && s_key_state[i].debounced == 0U) {
            s_key_state[i].debounced = 1U;
            s_key_state[i].press_tick = now;
        }
    }

    return result;
}
