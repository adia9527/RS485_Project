#ifndef APP_BACKLIGHT_H
#define APP_BACKLIGHT_H

#include <stdint.h>

void App_Backlight_Init(void);
void App_Backlight_On(void);
void App_Backlight_Off(void);
void App_Backlight_Wakeup(void);
void App_Backlight_SetTimeout(uint32_t timeout_ms);
void App_Backlight_OnUserActivity(void);
void App_BacklightTask(void *argument);

#endif /* APP_BACKLIGHT_H */
