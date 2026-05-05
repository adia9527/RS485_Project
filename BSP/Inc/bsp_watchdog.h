#ifndef BSP_WATCHDOG_H
#define BSP_WATCHDOG_H

/*
 * Set to 1 to enable IWDG.
 * Requires IWDG to be enabled in CubeMX (generates hiwdg handle).
 * Keep at 0 during debug to avoid unwanted resets.
 */
#define BSP_WATCHDOG_ENABLE  1

void BSP_Watchdog_Init(void);
void BSP_Watchdog_Feed(void);

#endif /* BSP_WATCHDOG_H */
