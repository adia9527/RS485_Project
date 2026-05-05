#include "bsp_lcd.h"
#include "bsp_spi_bus.h"
#include "main.h"
#include "spi.h"

#define LCD_SPI_TIMEOUT_MS  100U

/* ILI9341 commands */
#define ILI9341_SWRESET  0x01U  //软件复位
#define ILI9341_SLPOUT   0x11U  //退出睡眠模式
#define ILI9341_DISPON   0x29U  //打开显示
#define ILI9341_CASET    0x2AU  //设置列地址，也就是 X 方向范围 
#define ILI9341_PASET    0x2BU  //设置页地址，也就是 Y 方向范围 
#define ILI9341_RAMWR    0x2CU  //写显存，开始写像素数据  
#define ILI9341_MADCTL   0x36U  //内存访问控制，设置扫描方向、屏幕旋转、RGB/BGR 顺序
#define ILI9341_COLMOD   0x3AU  //像素格式设置，比如 RGB565

/* FillRect chunk: 240 pixels × 2 bytes = 480 bytes per row  LCD 填充矩形时，临时缓冲区一次最多准备 240 个像素的数 */

#define LCD_FILL_BUF_PIXELS  240U

/* ------------------------------------------------------------------ */
/*  Bus arbitration (SPI1 shared with W25Q64)                         */
/* ------------------------------------------------------------------ */
void BSP_LCD_Select(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
}

void BSP_LCD_Unselect(void)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

/* ------------------------------------------------------------------ */
/*  Backlight                                                          */
/* ------------------------------------------------------------------ */
void BSP_LCD_BacklightOn(void)
{
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
}

void BSP_LCD_BacklightOff(void)
{
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);
}

/* ------------------------------------------------------------------ */
/*  Reset                                                              */
/* ------------------------------------------------------------------ */
void BSP_LCD_Reset(void)
{
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10U);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(120U);
}

/* ------------------------------------------------------------------ */
/*  SPI transfers (caller holds SPI1 bus lock) 向LCD发送数据                       */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef BSP_LCD_WriteCommand(uint8_t cmd)
{
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    BSP_LCD_Select();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, &cmd, 1U, LCD_SPI_TIMEOUT_MS);
    BSP_LCD_Unselect();
    return st;
}

HAL_StatusTypeDef BSP_LCD_WriteData(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0U) { return HAL_ERROR; }
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    BSP_LCD_Select();
    HAL_StatusTypeDef st = HAL_OK;
    while (len > 0U && st == HAL_OK) {
        uint16_t chunk = (len > 0xFFFFU) ? 0xFFFFU : (uint16_t)len;
        st = HAL_SPI_Transmit(&hspi1, (uint8_t *)(uintptr_t)data, chunk, LCD_SPI_TIMEOUT_MS);
        data += chunk;
        len  -= chunk;
    }
    BSP_LCD_Unselect();
    return st;
}

HAL_StatusTypeDef BSP_LCD_WriteData8(uint8_t data)
{
    return BSP_LCD_WriteData(&data, 1U);
}

/* ------------------------------------------------------------------ */
/*  ILI9341 init                                                       */
/* ------------------------------------------------------------------ */
void BSP_LCD_Init(void)
{
    BSP_LCD_Unselect();
    BSP_LCD_BacklightOff();
    BSP_LCD_Reset();

    BSP_SPI1Bus_Lock();

    /* Software reset */
    BSP_LCD_WriteCommand(ILI9341_SWRESET);
    BSP_SPI1Bus_Unlock();
    HAL_Delay(120U);
    BSP_SPI1Bus_Lock();

    /* Sleep out */
    BSP_LCD_WriteCommand(ILI9341_SLPOUT);
    BSP_SPI1Bus_Unlock();
    HAL_Delay(120U);
    BSP_SPI1Bus_Lock();

    /* Pixel format: RGB565 */
    BSP_LCD_WriteCommand(ILI9341_COLMOD);
    BSP_LCD_WriteData8(0x55U);

    /* Memory access: portrait, BGR */
    BSP_LCD_WriteCommand(ILI9341_MADCTL);
    BSP_LCD_WriteData8(0x48U);

    /* Display on */
    BSP_LCD_WriteCommand(ILI9341_DISPON);

    BSP_SPI1Bus_Unlock();
    HAL_Delay(20U);

    BSP_LCD_FillScreen(LCD_COLOR_BLACK);
    BSP_LCD_BacklightOn();
    BSP_LCD_FillRect(0,0,100,100,LCD_COLOR_RED);

}

/* ------------------------------------------------------------------ */
/*  SetWindow   设置 LCD 接下来要写入像素的区域，也就是设置绘图窗口。                                                    */
/* ------------------------------------------------------------------ */
void BSP_LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t d[4];

    /* Column address */
    d[0] = (uint8_t)(x0 >> 8U); d[1] = (uint8_t)x0;
    d[2] = (uint8_t)(x1 >> 8U); d[3] = (uint8_t)x1;
    BSP_LCD_WriteCommand(ILI9341_CASET);
    BSP_LCD_WriteData(d, 4U);

    /* Row address */
    d[0] = (uint8_t)(y0 >> 8U); d[1] = (uint8_t)y0;
    d[2] = (uint8_t)(y1 >> 8U); d[3] = (uint8_t)y1;
    BSP_LCD_WriteCommand(ILI9341_PASET);
    BSP_LCD_WriteData(d, 4U);

    /* Write to RAM */
    BSP_LCD_WriteCommand(ILI9341_RAMWR);
}

/* ------------------------------------------------------------------ */
/*  Fill primitives 在 LCD 屏幕指定位置画一个实心矩形，也就是填充一个矩形区域。  */
/* ------------------------------------------------------------------ */
void BSP_LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0U || h == 0U) { return; }

    /* row buffer: one row of pixels */
    static uint8_t s_row[LCD_FILL_BUF_PIXELS * 2U]; //LCD 颜色格式是 RGB565，一个像素占 2 字节
    uint16_t cols = (w > LCD_FILL_BUF_PIXELS) ? LCD_FILL_BUF_PIXELS : w; //本次缓冲区实际填多少个像素
    //把 color 这个 16 位颜色拆成两个 8 位字节，填入 s_row
    for (uint16_t i = 0U; i < cols; i++) {
        s_row[i * 2U]      = (uint8_t)(color >> 8U);
        s_row[i * 2U + 1U] = (uint8_t)(color & 0xFFU);
    }

    BSP_SPI1Bus_Lock();
    BSP_LCD_SetWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));

    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    BSP_LCD_Select();

    uint32_t total = (uint32_t)w * h;
    //循环发送像素数据
    while (total > 0U) {
        uint16_t batch = (total > cols) ? cols : (uint16_t)total;
        HAL_SPI_Transmit(&hspi1, s_row, (uint16_t)(batch * 2U), LCD_SPI_TIMEOUT_MS);
        total -= batch;
    }

    BSP_LCD_Unselect();
    BSP_SPI1Bus_Unlock();
}

void BSP_LCD_FillScreen(uint16_t color)
{
    BSP_LCD_FillRect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}

void BSP_LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    BSP_LCD_FillRect(x, y, 1U, 1U, color);
}

/* ------------------------------------------------------------------ */
/*  Test pattern: four color bands                                     */
/* ------------------------------------------------------------------ */
void BSP_LCD_TestPattern(void)
{
    uint16_t band = LCD_HEIGHT / 4U;
    BSP_LCD_FillRect(0U,          0U,        LCD_WIDTH, band, LCD_COLOR_RED);
    BSP_LCD_FillRect(0U,          band,      LCD_WIDTH, band, LCD_COLOR_GREEN);
    BSP_LCD_FillRect(0U, (uint16_t)(band*2U), LCD_WIDTH, band, LCD_COLOR_BLUE);
    BSP_LCD_FillRect(0U, (uint16_t)(band*3U), LCD_WIDTH, band, LCD_COLOR_YELLOW);
}
