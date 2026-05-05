#include "bsp_spi_bus.h"
#include "cmsis_os.h"

static osMutexId_t s_mutex = NULL;

void BSP_SPI1Bus_Init(void)
{
    if (s_mutex == NULL) {
        static const osMutexAttr_t attr = { .name = "SPI1BusMutex" };
        s_mutex = osMutexNew(&attr);
    }
}

void BSP_SPI1Bus_Lock(void)
{
    if (s_mutex != NULL) {
        osMutexAcquire(s_mutex, osWaitForever);
    }
}

void BSP_SPI1Bus_Unlock(void)
{
    if (s_mutex != NULL) {
        osMutexRelease(s_mutex);
    }
}

uint8_t BSP_SPI1Bus_IsReady(void)
{
    return (s_mutex != NULL) ? 1U : 0U;
}
