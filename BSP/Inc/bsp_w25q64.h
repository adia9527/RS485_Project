/**
 * @file    bsp_w25q64.h
 * @brief   W25Q64 SPI Flash BSP driver (8 MB, SPI1, CS=PD2)
 *
 * Hardware:
 *   SPI1  SCK=PB3  MISO=PB4  MOSI=PB5
 *   W25Q64_CS = PD2   (defined in main.h)
 *   LCD_CS    = PD3   (defined in main.h, deasserted before W25Q64 access)
 *
 * Notes:
 *   - W25Q64 shares SPI1 with the TFT LCD.
 *   - LCD_CS is driven HIGH before asserting W25Q64_CS so the TFT does
 *     not respond while W25Q64 is addressed.
 *   - Blocking HAL SPI calls are used (no DMA).
 *   - A FreeRTOS mutex guards every SPI transaction.  When called before
 *     the RTOS scheduler starts the mutex handle is NULL and locking is
 *     skipped, so BSP_W25Q64_Init() / ReadID() can be called from
 *     App_MainInit().
 */
#ifndef BSP_W25Q64_H
#define BSP_W25Q64_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  W25Q64 command set                                                 */
/* ------------------------------------------------------------------ */
#define W25Q64_CMD_WRITE_ENABLE      0x06U  //写使能
#define W25Q64_CMD_WRITE_DISABLE     0x04U  //写禁止
#define W25Q64_CMD_READ_STATUS1      0x05U  //读取状态寄存器
#define W25Q64_CMD_PAGE_PROGRAM      0x02U  //页编程，写入数据
#define W25Q64_CMD_READ_DATA         0x03U  //普通读取数据
#define W25Q64_CMD_FAST_READ         0x0BU  //快速读取数据
#define W25Q64_CMD_SECTOR_ERASE_4K   0x20U  //擦除 4KB 扇区
#define W25Q64_CMD_BLOCK_ERASE_32K   0x52U  //擦除 32KB 块
#define W25Q64_CMD_CHIP_ERASE        0xC7U  //整片擦除
#define W25Q64_CMD_JEDEC_ID          0x9FU  //读取芯片 ID

/* Status register 1 */
#define W25Q64_SR1_WIP_BIT           0x01U  /* Write In Progress 状态寄存器 1 的 bit0，表示 Flash 是否正在忙 */

/* ------------------------------------------------------------------ */
/*  Device geometry                                                    */
/* ------------------------------------------------------------------ */
#define W25Q64_PAGE_SIZE             256U   //一页大小，256 字节
#define W25Q64_SECTOR_SIZE           4096U  //一个扇区大小，4KB
#define W25Q64_BLOCK32K_SIZE         (32U * 1024U)  //一个 32KB 块大小
#define W25Q64_TOTAL_SIZE            (8U * 1024U * 1024U)   //整片 Flash 总大小，8MB

/* Expected JEDEC ID for W25Q64 (Winbond) */
#define W25Q64_JEDEC_ID_EXPECTED     0xEF4017UL //期望的芯片ID

/* ------------------------------------------------------------------ */
/*  Timing                                                             */
/* ------------------------------------------------------------------ */
/** SPI byte-transfer timeout in ms */
#define W25Q64_SPI_TIMEOUT_MS        100U
/** Maximum time to wait for WIP=0 after a write/erase, in ms */
#define W25Q64_BUSY_TIMEOUT_MS       5000U
/** Maximum time to wait for chip erase, in ms */
#define W25Q64_CHIP_ERASE_TIMEOUT_MS 60000U

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief  Initialise the W25Q64 driver (reads JEDEC ID, creates mutex).
 *         Must be called after MX_SPI1_Init().
 *         Safe to call before RTOS scheduler starts.
 */
void BSP_W25Q64_Init(void);

/**
 * @brief  Return true (1) if W25Q64 was successfully detected at init.
 */
uint8_t BSP_W25Q64_IsDetected(void);

/**
 * @brief  Read the 24-bit JEDEC ID (Manufacturer + Memory Type + Capacity).
 * @return JEDEC ID, e.g. 0xEF4017 for W25Q64; 0 on SPI error.
 */
uint32_t BSP_W25Q64_ReadID(void);

/**
 * @brief  Poll the WIP bit; returns HAL_OK once the device is idle.
 * @param  timeout_ms  Maximum wait in milliseconds.
 */
HAL_StatusTypeDef BSP_W25Q64_WaitReady(uint32_t timeout_ms);

/**
 * @brief  Read @p len bytes from @p addr into @p buf.
 */
HAL_StatusTypeDef BSP_W25Q64_Read(uint32_t addr,
                                  uint8_t *buf,
                                  uint32_t len);

/**
 * @brief  Write up to 256 bytes into one page (page boundary must not
 *         be crossed — caller's responsibility or use BSP_W25Q64_Write).
 */
HAL_StatusTypeDef BSP_W25Q64_PageProgram(uint32_t addr,
                                         const uint8_t *buf,
                                         uint16_t len);

/**
 * @brief  Write @p len bytes starting at @p addr, automatically
 *         splitting across page boundaries.
 *         The target range must already be erased (all 0xFF).
 */
HAL_StatusTypeDef BSP_W25Q64_Write(uint32_t addr,
                                   const uint8_t *buf,
                                   uint32_t len);

/**
 * @brief  Erase the 4-KB sector containing @p addr.
 */
HAL_StatusTypeDef BSP_W25Q64_EraseSector(uint32_t addr);

/**
 * @brief  Erase the 32-KB block containing @p addr.
 */
HAL_StatusTypeDef BSP_W25Q64_EraseBlock32K(uint32_t addr);

/**
 * @brief  Erase the entire chip (~20-40 s typical).
 */
HAL_StatusTypeDef BSP_W25Q64_EraseChip(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_W25Q64_H */
