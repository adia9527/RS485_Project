#include "bsp_flash.h"

HAL_StatusTypeDef BSP_Flash_EraseSector(uint32_t sector)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0U;

    HAL_FLASH_Unlock();

    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_init.Sector       = sector;
    erase_init.NbSectors    = 1U;

    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

    HAL_FLASH_Lock();
    return status;
}

HAL_StatusTypeDef BSP_Flash_Write(uint32_t address, const uint8_t *data, uint32_t length)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t i;

    HAL_FLASH_Unlock();

    for (i = 0U; i < length; i += 4U) {
        uint32_t word;
        if (i + 4U <= length) {
            word = *(uint32_t *)(data + i);
        } else {
            //处理最后不足 4 字节的数据，把 data 里的 1~3 个字节，按字节塞进一个 32 位的 word 里面。
            word = 0xFFFFFFFFU;
            for (uint32_t j = 0U; j < (length - i); j++) {
                //
                word = (word & ~(0xFFU << (j * 8U))) | ((uint32_t)data[i + j] << (j * 8U));
            }
        }
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word);
        if (status != HAL_OK) break;
    }

    HAL_FLASH_Lock();
    return status;
}

void BSP_Flash_Read(uint32_t address, uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0U; i < length; i++) {
        //把这个整数地址强制转换成一个指针后取值
        data[i] = *(volatile uint8_t *)(address + i);
    }
}
