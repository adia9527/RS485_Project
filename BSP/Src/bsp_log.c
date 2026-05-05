#include "bsp_log.h"
#include "usart.h"
#include "stm32f4xx_hal.h"
#include <stdarg.h>
#include <stdio.h>

#define LOG_BUF_SIZE 256U

static char s_log_buf[LOG_BUF_SIZE];

void BSP_Log_Init(void)
{
    /* USART1 already initialized by CubeMX */
}

void BSP_Log_Printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(s_log_buf, LOG_BUF_SIZE, fmt, args);
    va_end(args);

    if (len > 0 && (uint32_t)len < LOG_BUF_SIZE) {
        HAL_UART_Transmit(&huart1, (uint8_t *)s_log_buf, (uint16_t)len, 100U);
    }
}
