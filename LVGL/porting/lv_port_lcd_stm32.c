#include "lv_port_lcd_stm32.h"
#include "./src/drivers/display/ili9341/lv_ili9341.h"
#include "bsp_spi_bus.h"
#include "main.h"
#include "spi.h"

#define MY_DISP_HOR_RES  240
#define MY_DISP_VER_RES  320
#define BUS_SPI1_POLL_TIMEOUT  0x1000U

/* 1/10 screen, RGB565 = 2 bytes/pixel */
#define DISP_BUF_PIXELS  (MY_DISP_HOR_RES * MY_DISP_VER_RES / 10)

static lv_display_t *lcd_disp;
static volatile int  lcd_bus_busy = 0;

static uint8_t s_buf1[DISP_BUF_PIXELS * 2];
static uint8_t s_buf2[DISP_BUF_PIXELS * 2];

static void lcd_color_transfer_ready_cb(SPI_HandleTypeDef *hspi)
{
    (void)hspi;
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    BSP_SPI1Bus_Unlock();
    lcd_bus_busy = 0;
    lv_display_flush_ready(lcd_disp);
}

static int32_t lcd_io_init(void)
{
    HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_COMPLETE_CB_ID,
                             lcd_color_transfer_ready_cb);

    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(100U);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100U);

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,  LCD_CS_Pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port,  LCD_DC_Pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port,  LCD_BL_Pin,  GPIO_PIN_SET);

    return 0;
}

static void lcd_send_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                         const uint8_t *param, size_t param_size)
{
    LV_UNUSED(disp);
    while (lcd_bus_busy) {}
    BSP_SPI1Bus_Lock();
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    HAL_SPI_Init(&hspi1);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(&hspi1, (uint8_t *)(uintptr_t)cmd,
                         (uint16_t)cmd_size, BUS_SPI1_POLL_TIMEOUT) == HAL_OK) {
        HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
        HAL_SPI_Transmit(&hspi1, (uint8_t *)(uintptr_t)param,
                         (uint16_t)param_size, BUS_SPI1_POLL_TIMEOUT);
    }
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    BSP_SPI1Bus_Unlock();
}

static void lcd_send_color(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                           uint8_t *param, size_t param_size)
{
    LV_UNUSED(disp);
    while (lcd_bus_busy) {}
    BSP_SPI1Bus_Lock();
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    HAL_SPI_Init(&hspi1);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(&hspi1, (uint8_t *)(uintptr_t)cmd,
                         (uint16_t)cmd_size, BUS_SPI1_POLL_TIMEOUT) == HAL_OK) {
        HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
        hspi1.Init.DataSize = SPI_DATASIZE_16BIT;
        HAL_SPI_Init(&hspi1);
        lcd_bus_busy = 1;
        HAL_SPI_Transmit_DMA(&hspi1, param, (uint16_t)(param_size / 2U));
        /* BSP_SPI1Bus_Unlock() called in DMA complete callback */
    } else {
        HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
        BSP_SPI1Bus_Unlock();
    }
}

void lv_port_disp_init(void)
{
    if (lcd_io_init() != 0) { return; }

    lcd_disp = lv_ili9341_create(MY_DISP_HOR_RES, MY_DISP_VER_RES,
                                 LV_LCD_FLAG_NONE, lcd_send_cmd, lcd_send_color);
    lv_display_set_buffers(lcd_disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}
