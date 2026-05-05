#include "bsp_watchdog.h"

#if BSP_WATCHDOG_ENABLE
#include "iwdg.h"  /* CubeMX-generated: declares hiwdg */
#endif

void BSP_Watchdog_Init(void)
{
#if BSP_WATCHDOG_ENABLE
    /* hiwdg must be initialized by CubeMX-generated MX_IWDG_Init() before this */
    (void)hiwdg;
#endif
}

void BSP_Watchdog_Feed(void)
{
#if BSP_WATCHDOG_ENABLE
    HAL_IWDG_Refresh(&hiwdg);
#endif
}
