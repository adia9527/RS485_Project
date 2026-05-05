#include "bsp_esp32.h"
#include "main.h"
#include "usart.h"
#include "cmsis_os.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Ring buffer (ISR writer, task reader)                              */
/* ------------------------------------------------------------------ */
static uint8_t           s_rx_ring[BSP_ESP32_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t           s_rx_it_byte;

static void ESP32_Delay(uint32_t ms)
{
    if (osKernelGetState() == osKernelRunning) {
        osDelay(ms);
    } else {
        HAL_Delay(ms);
    }
}

void BSP_ESP32_SetBootNormal(void)
{
    HAL_GPIO_WritePin(ESP32_IO0_GPIO_Port, ESP32_IO0_Pin, GPIO_PIN_SET);
}

void BSP_ESP32_SetBootDownload(void)
{
    HAL_GPIO_WritePin(ESP32_IO0_GPIO_Port, ESP32_IO0_Pin, GPIO_PIN_RESET);
}

void BSP_ESP32_Enable(void)
{
    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_SET);
}

void BSP_ESP32_Disable(void)
{
    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_RESET);
}

void BSP_ESP32_Reset(void)
{
    BSP_ESP32_SetBootNormal();
    BSP_ESP32_Disable();
    ESP32_Delay(100U);
    BSP_ESP32_Enable();
    ESP32_Delay(1000U);
}

void BSP_ESP32_Init(void)
{
    BSP_ESP32_Reset();
    s_rx_head = 0U;
    s_rx_tail = 0U;
    HAL_UART_Receive_IT(&huart4, &s_rx_it_byte, 1U);
}

/* ------------------------------------------------------------------ */
/*  ISR callback — feeds ring buffer, re-arms IT                      */
/* ------------------------------------------------------------------ */
void BSP_ESP32_UartRxCallback(void)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % BSP_ESP32_RX_BUF_SIZE);
    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = s_rx_it_byte;
        s_rx_head = next;
    }
    HAL_UART_Receive_IT(&huart4, &s_rx_it_byte, 1U);
}

/* ------------------------------------------------------------------ */
/*  Ring buffer accessors                                              */
/* ------------------------------------------------------------------ */
uint16_t BSP_ESP32_RxAvailable(void)
{
    return (uint16_t)((s_rx_head - s_rx_tail + BSP_ESP32_RX_BUF_SIZE) % BSP_ESP32_RX_BUF_SIZE);
}

uint8_t BSP_ESP32_ReadByte(uint8_t *b)
{
    if (s_rx_tail == s_rx_head) { return 0U; }
    *b = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % BSP_ESP32_RX_BUF_SIZE);
    return 1U;
}

/* ------------------------------------------------------------------ */
/*  Send                                                               */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef BSP_ESP32_SendRaw(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0U) { return HAL_ERROR; }
    return HAL_UART_Transmit(&huart4, (uint8_t *)(uintptr_t)data, len, timeout_ms);
}

HAL_StatusTypeDef BSP_ESP32_SendString(const char *str, uint32_t timeout_ms)
{
    if (str == NULL) { return HAL_ERROR; }
    uint16_t len = (uint16_t)strlen(str);
    if (len == 0U) { return HAL_OK; }
    return BSP_ESP32_SendRaw((const uint8_t *)str, len, timeout_ms);
}

/* ------------------------------------------------------------------ */
/*  Receive — drains ring buffer until timeout                        */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef BSP_ESP32_Receive(uint8_t *buf, uint16_t buf_size,
                                    uint16_t *rx_len, uint32_t timeout_ms)
{
    if (buf == NULL || buf_size == 0U || rx_len == NULL) { return HAL_ERROR; }

    *rx_len = 0U;
    uint32_t t0 = HAL_GetTick();

    while ((HAL_GetTick() - t0) < timeout_ms && *rx_len < (buf_size - 1U)) {
        uint8_t b;
        if (BSP_ESP32_ReadByte(&b)) {
            buf[(*rx_len)++] = b;
        }
    }
    buf[*rx_len] = '\0';
    return (*rx_len > 0U) ? HAL_OK : HAL_TIMEOUT;
}
