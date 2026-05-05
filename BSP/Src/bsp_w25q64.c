#include "bsp_w25q64.h"
#include "bsp_spi_bus.h"
#include "bsp_log.h"
#include "main.h"
#include "spi.h"
#include "cmsis_os.h"

static uint8_t s_detected = 0U;

static void W25Q64_Lock(void)   { BSP_SPI1Bus_Lock(); }
static void W25Q64_Unlock(void) { BSP_SPI1Bus_Unlock(); }

/* ---- CS ---- */
static void W25Q64_Select(void)
{
#if defined(LCD_CS_Pin) && defined(LCD_CS_GPIO_Port)
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
#endif
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_RESET);
}
static void W25Q64_Unselect(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET);
}

/* ---- helpers ---- */
//把一个 24 位 Flash 地址拆成 3 个字节，方便通过 SPI 发送给 W25Q64。
static void W25Q64_AddrToBytes(uint32_t addr, uint8_t out[3])
{
    out[0] = (uint8_t)((addr >> 16U) & 0xFFU);
    out[1] = (uint8_t)((addr >>  8U) & 0xFFU);
    out[2] = (uint8_t)( addr         & 0xFFU);
}

/* WaitReady without lock (called while lock is already held) */
//等待W25Q64写入完成
static HAL_StatusTypeDef W25Q64_WaitReadyUnlocked(uint32_t timeout_ms)
{
    uint32_t t0  = HAL_GetTick();
    uint8_t  cmd = W25Q64_CMD_READ_STATUS1;
    uint8_t  sr1 = 0U;
    for (;;)
    {
        W25Q64_Select();
        HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, &cmd, 1U, W25Q64_SPI_TIMEOUT_MS);
        if (st == HAL_OK) { st = HAL_SPI_Receive(&hspi1, &sr1, 1U, W25Q64_SPI_TIMEOUT_MS); }
        W25Q64_Unselect();
        if (st != HAL_OK)              { return HAL_ERROR; }
        if (!(sr1 & W25Q64_SR1_WIP_BIT)) { return HAL_OK; } //等待忙标志清除
        if ((HAL_GetTick() - t0) >= timeout_ms) { return HAL_TIMEOUT; }
        if (BSP_SPI1Bus_IsReady()) { osDelay(1U); }
    }
}

//W25Q64写使能
static HAL_StatusTypeDef W25Q64_WriteEnableUnlocked(void)
{
    uint8_t cmd = W25Q64_CMD_WRITE_ENABLE;
    W25Q64_Select();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, &cmd, 1U, W25Q64_SPI_TIMEOUT_MS);
    W25Q64_Unselect();
    return st;
}

/* ================================================================== */

void BSP_W25Q64_Init(void)
{
    W25Q64_Unselect();

    uint32_t id = BSP_W25Q64_ReadID();
    if (id != 0x000000UL && id != 0xFFFFFFUL)
    {
        s_detected = 1U;
        BSP_Log_Printf("[FLASH] W25Q64 ID=0x%06lX\r\n", (unsigned long)id);
    }
    else
    {
        s_detected = 0U;
        BSP_Log_Printf("[FLASH] W25Q64 not detected, event persist disabled\r\n");
    }
}

uint8_t BSP_W25Q64_IsDetected(void) { return s_detected; }

uint32_t BSP_W25Q64_ReadID(void)
{
    W25Q64_Lock();
    uint8_t cmd = W25Q64_CMD_JEDEC_ID;
    uint8_t rx[3] = { 0U, 0U, 0U };
    W25Q64_Select();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, &cmd, 1U, W25Q64_SPI_TIMEOUT_MS);
    if (st == HAL_OK) { st = HAL_SPI_Receive(&hspi1, rx, 3U, W25Q64_SPI_TIMEOUT_MS); }
    W25Q64_Unselect();
    W25Q64_Unlock();
    if (st != HAL_OK) { return 0U; }
    return ((uint32_t)rx[0] << 16U) | ((uint32_t)rx[1] << 8U) | (uint32_t)rx[2];
}

HAL_StatusTypeDef BSP_W25Q64_WaitReady(uint32_t timeout_ms)
{
    W25Q64_Lock();
    HAL_StatusTypeDef st = W25Q64_WaitReadyUnlocked(timeout_ms);
    W25Q64_Unlock();
    return st;
}

HAL_StatusTypeDef BSP_W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!s_detected || buf == NULL || len == 0U)  { return HAL_ERROR; }
    if ((addr + len) > W25Q64_TOTAL_SIZE)          { return HAL_ERROR; }

    W25Q64_Lock();
    HAL_StatusTypeDef st = W25Q64_WaitReadyUnlocked(W25Q64_BUSY_TIMEOUT_MS);
    if (st == HAL_OK)
    {
        uint8_t hdr[4];
        hdr[0] = W25Q64_CMD_READ_DATA;
        W25Q64_AddrToBytes(addr, &hdr[1]);
        W25Q64_Select();
        st = HAL_SPI_Transmit(&hspi1, hdr, 4U, W25Q64_SPI_TIMEOUT_MS);
        if (st == HAL_OK)
        {
            uint32_t rem = len;
            uint8_t *p   = buf;
            while (rem > 0U && st == HAL_OK)
            {
                uint16_t chunk = (rem > 0xFFFFU) ? 0xFFFFU : (uint16_t)rem;
                st  = HAL_SPI_Receive(&hspi1, p, chunk, W25Q64_SPI_TIMEOUT_MS);
                p  += chunk;
                rem -= chunk;
            }
        }
        W25Q64_Unselect();
    }
    W25Q64_Unlock();
    return st;
}

HAL_StatusTypeDef BSP_W25Q64_PageProgram(uint32_t addr,
                                         const uint8_t *buf,
                                         uint16_t len)
{
    if (!s_detected || buf == NULL || len == 0U) { return HAL_ERROR; }
    if (len > (uint16_t)W25Q64_PAGE_SIZE)         { return HAL_ERROR; }
    if ((addr + len) > W25Q64_TOTAL_SIZE)          { return HAL_ERROR; }

    W25Q64_Lock();
    HAL_StatusTypeDef st = W25Q64_WriteEnableUnlocked();
    if (st == HAL_OK)
    {
        uint8_t hdr[4];
        hdr[0] = W25Q64_CMD_PAGE_PROGRAM;
        W25Q64_AddrToBytes(addr, &hdr[1]);
        W25Q64_Select();
        st = HAL_SPI_Transmit(&hspi1, hdr, 4U, W25Q64_SPI_TIMEOUT_MS);
        if (st == HAL_OK)
        {
            st = HAL_SPI_Transmit(&hspi1, (uint8_t *)(uintptr_t)buf,
                                  len, W25Q64_SPI_TIMEOUT_MS);
        }
        W25Q64_Unselect();
        if (st == HAL_OK) { st = W25Q64_WaitReadyUnlocked(W25Q64_BUSY_TIMEOUT_MS); }
    }
    W25Q64_Unlock();
    return st;
}

HAL_StatusTypeDef BSP_W25Q64_Write(uint32_t addr,
                                   const uint8_t *buf,
                                   uint32_t len)
{
    if (!s_detected || buf == NULL || len == 0U)  { return HAL_ERROR; }
    if ((addr + len) > W25Q64_TOTAL_SIZE)          { return HAL_ERROR; }

    uint32_t       remaining = len;
    const uint8_t *p         = buf;
    uint32_t       cur_addr  = addr;

    while (remaining > 0U)
    {
        /* bytes left in current page */
        uint32_t page_offset = cur_addr % W25Q64_PAGE_SIZE;
        uint32_t page_space  = W25Q64_PAGE_SIZE - page_offset;
        uint16_t chunk = (remaining < page_space) ? (uint16_t)remaining
                                                   : (uint16_t)page_space;

        HAL_StatusTypeDef st = BSP_W25Q64_PageProgram(cur_addr, p, chunk);
        if (st != HAL_OK) { return st; }

        cur_addr  += chunk;
        p         += chunk;
        remaining -= chunk;
    }
    return HAL_OK;
}

HAL_StatusTypeDef BSP_W25Q64_EraseSector(uint32_t addr)
{
    if (!s_detected) { return HAL_ERROR; }

    W25Q64_Lock();
    HAL_StatusTypeDef st = W25Q64_WriteEnableUnlocked();
    if (st == HAL_OK)
    {
        uint8_t hdr[4];
        //命令格式： 命令字节 + 24位地址 + 数据
        hdr[0] = W25Q64_CMD_SECTOR_ERASE_4K;
        W25Q64_AddrToBytes(addr, &hdr[1]);
        W25Q64_Select();
        st = HAL_SPI_Transmit(&hspi1, hdr, 4U, W25Q64_SPI_TIMEOUT_MS);
        W25Q64_Unselect();
        if (st == HAL_OK) { st = W25Q64_WaitReadyUnlocked(W25Q64_BUSY_TIMEOUT_MS); }
    }
    W25Q64_Unlock();
    return st;
}

HAL_StatusTypeDef BSP_W25Q64_EraseBlock32K(uint32_t addr)
{
    if (!s_detected) { return HAL_ERROR; }

    W25Q64_Lock();
    HAL_StatusTypeDef st = W25Q64_WriteEnableUnlocked();
    if (st == HAL_OK)
    {
        uint8_t hdr[4];
        hdr[0] = W25Q64_CMD_BLOCK_ERASE_32K;
        W25Q64_AddrToBytes(addr, &hdr[1]);
        W25Q64_Select();
        st = HAL_SPI_Transmit(&hspi1, hdr, 4U, W25Q64_SPI_TIMEOUT_MS);
        W25Q64_Unselect();
        if (st == HAL_OK) { st = W25Q64_WaitReadyUnlocked(W25Q64_BUSY_TIMEOUT_MS); }
    }
    W25Q64_Unlock();
    return st;
}

HAL_StatusTypeDef BSP_W25Q64_EraseChip(void)
{
    if (!s_detected) { return HAL_ERROR; }

    W25Q64_Lock();
    HAL_StatusTypeDef st = W25Q64_WriteEnableUnlocked();
    if (st == HAL_OK)
    {
        uint8_t cmd = W25Q64_CMD_CHIP_ERASE;
        W25Q64_Select();
        st = HAL_SPI_Transmit(&hspi1, &cmd, 1U, W25Q64_SPI_TIMEOUT_MS);
        W25Q64_Unselect();
        if (st == HAL_OK) { st = W25Q64_WaitReadyUnlocked(W25Q64_CHIP_ERASE_TIMEOUT_MS); }
    }
    W25Q64_Unlock();
    return st;
}
