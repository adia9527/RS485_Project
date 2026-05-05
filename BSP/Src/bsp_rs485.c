#include "bsp_rs485.h"
#include "bsp_log.h"
#include "usart.h"
#include "main.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"

/* RS485_1: UART7 + PA11(RS4851_DIR), RS485_2: USART3 + PD10(RS4852_DIR) */

#define RS485_DMA_BUF_SIZE  256U

typedef struct {
    BSP_RS485_Port_t      port;
    UART_HandleTypeDef   *huart;
    GPIO_TypeDef         *dir_gpio;
    uint16_t              dir_pin;
    uint8_t               dma_rx_available;
    uint8_t               rx_buf[RS485_DMA_BUF_SIZE];
    volatile uint16_t     rx_len;
    volatile uint8_t      rx_done;
    osSemaphoreId_t       rx_sem;
} RS485_PortContext_t;

static RS485_PortContext_t s_rs485_ctx[] = {
    {
        .port = BSP_RS485_PORT_1,
        .huart = &huart7,
        .dir_gpio = RS4851_DIR_GPIO_Port,
        .dir_pin = RS4851_DIR_Pin,
    },
    {
        .port = BSP_RS485_PORT_2,
        .huart = &huart3,
        .dir_gpio = RS4852_DIR_GPIO_Port,
        .dir_pin = RS4852_DIR_Pin,
    },
};

static RS485_PortContext_t *rs485_get_ctx(BSP_RS485_Port_t port)
{
    if (port == BSP_RS485_PORT_1) {
        return &s_rs485_ctx[0];
    }
    if (port == BSP_RS485_PORT_2) {
        return &s_rs485_ctx[1];
    }
    return NULL;
}

static RS485_PortContext_t *rs485_get_ctx_by_uart(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return NULL;
    }
    if (huart->Instance == UART7) {
        return &s_rs485_ctx[0];
    }
    if (huart->Instance == USART3) {
        return &s_rs485_ctx[1];
    }
    return NULL;
}

void BSP_RS485_Init(void)
{
    HAL_GPIO_WritePin(RS4851_DIR_GPIO_Port, RS4851_DIR_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RS4852_DIR_GPIO_Port, RS4852_DIR_Pin, GPIO_PIN_RESET);

    static const osSemaphoreAttr_t sem1_attr = { .name = "RS485P1RxSem" };
    static const osSemaphoreAttr_t sem2_attr = { .name = "RS485P2RxSem" };

    s_rs485_ctx[0].rx_len = 0U;
    s_rs485_ctx[0].rx_done = 0U;
    s_rs485_ctx[0].dma_rx_available = (huart7.hdmarx != NULL) ? 1U : 0U;
    if (s_rs485_ctx[0].rx_sem == NULL) {
        s_rs485_ctx[0].rx_sem = osSemaphoreNew(1U, 0U, &sem1_attr);
    }

    s_rs485_ctx[1].rx_len = 0U;
    s_rs485_ctx[1].rx_done = 0U;
    s_rs485_ctx[1].dma_rx_available = (huart3.hdmarx != NULL) ? 1U : 0U;
    if (s_rs485_ctx[1].rx_sem == NULL) {
        s_rs485_ctx[1].rx_sem = osSemaphoreNew(1U, 0U, &sem2_attr);
    }

    if (s_rs485_ctx[0].dma_rx_available != 0U) {
        BSP_Log_Printf("[RS485] port1 UART7 dma rx enabled\r\n");
    } else {
        BSP_Log_Printf("[RS485] port1 UART7 dma missing, fallback blocking\r\n");
    }
    if (s_rs485_ctx[1].dma_rx_available != 0U) {
        BSP_Log_Printf("[RS485] port2 USART3 dma rx enabled\r\n");
    } else {
        BSP_Log_Printf("[RS485] port2 USART3 dma missing, fallback blocking\r\n");
    }
}

void BSP_RS485_Send(BSP_RS485_Port_t port, uint8_t *data, uint16_t len)
{
    RS485_PortContext_t *ctx = rs485_get_ctx(port);
    if (ctx == NULL || data == NULL || len == 0U) {
        return;
    }

    HAL_GPIO_WritePin(ctx->dir_gpio, ctx->dir_pin, GPIO_PIN_SET);
    HAL_UART_Transmit(ctx->huart, data, len, 100U);
    while (__HAL_UART_GET_FLAG(ctx->huart, UART_FLAG_TC) == RESET);
    HAL_GPIO_WritePin(ctx->dir_gpio, ctx->dir_pin, GPIO_PIN_RESET);
}

/* ---------- internal: arm DMA+IDLE reception when DMA exists ---------- */
static HAL_StatusTypeDef rs485_start_dma_receive(RS485_PortContext_t *ctx)
{
    if (ctx == NULL || ctx->huart == NULL || ctx->huart->hdmarx == NULL) {
        return HAL_ERROR;
    }

    ctx->rx_done = 0U;
    ctx->rx_len  = 0U;
    while (ctx->rx_sem != NULL && osSemaphoreAcquire(ctx->rx_sem, 0U) == osOK) {
    }

    /* Disable half-transfer interrupt; RS485 frames complete on IDLE/full events. */
    __HAL_DMA_DISABLE_IT(ctx->huart->hdmarx, DMA_IT_HT);

    if (ctx->huart->RxState != HAL_UART_STATE_READY) {
        HAL_UART_AbortReceive(ctx->huart);
    }

    return HAL_UARTEx_ReceiveToIdle_DMA(ctx->huart, ctx->rx_buf, RS485_DMA_BUF_SIZE);
}

static HAL_StatusTypeDef rs485_receive_blocking(UART_HandleTypeDef *huart,
                                                uint8_t *buf,
                                                uint16_t len,
                                                uint32_t timeout_ms)
{
    if (huart == NULL || buf == NULL || len == 0U) {
        return HAL_ERROR;
    }
    return HAL_UART_Receive(huart, buf, len, timeout_ms);
}

HAL_StatusTypeDef BSP_RS485_Receive(BSP_RS485_Port_t port, uint8_t *buf,
                                     uint16_t len, uint32_t timeout_ms)
{
    RS485_PortContext_t *ctx = rs485_get_ctx(port);

    if (ctx == NULL || buf == NULL || len == 0U) {
        return HAL_ERROR;
    }

    ctx->dma_rx_available = (ctx->huart->hdmarx != NULL) ? 1U : 0U;
    if (ctx->dma_rx_available == 0U) {
        return rs485_receive_blocking(ctx->huart, buf, len, timeout_ms);
    }

    if (ctx->rx_sem == NULL) {
        /* semaphore not yet created (called before BSP_RS485_Init) */
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = rs485_start_dma_receive(ctx);
    if (st != HAL_OK) {
        return st;
    }

    osStatus_t os_st = osSemaphoreAcquire(ctx->rx_sem, timeout_ms);
    if (os_st != osOK) {
        HAL_UART_AbortReceive(ctx->huart);
        return HAL_TIMEOUT;
    }

    uint16_t copy_len = (ctx->rx_len < len) ? ctx->rx_len : len;
    for (uint16_t i = 0U; i < copy_len; i++) {
        buf[i] = ctx->rx_buf[i];
    }

    return (ctx->rx_len > 0U) ? HAL_OK : HAL_ERROR;
}

/* Called from HAL_UARTEx_RxEventCallback in stm32f4xx_it.c */
void BSP_RS485_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    RS485_PortContext_t *ctx = rs485_get_ctx_by_uart(huart);

    if (ctx == NULL) {
        return;
    }

    ctx->rx_len  = Size;
    ctx->rx_done = 1U;
    if (ctx->rx_sem != NULL) {
        osSemaphoreRelease(ctx->rx_sem);
    }
}
