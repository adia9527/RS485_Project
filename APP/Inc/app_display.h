#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>
#include "app_types.h"

/* Set to 1 when LVGL sources and lv_conf.h are integrated into the build. */
#ifndef APP_USE_LVGL
#define APP_USE_LVGL  1
#endif

#if APP_USE_LVGL
#include "lvgl.h"
#endif

//LVGL页面
typedef enum {
    APP_PAGE_MAIN = 0,
    APP_PAGE_SENSOR,
    APP_PAGE_COMM,
    APP_PAGE_ALARM,
    APP_PAGE_ADC,
    APP_PAGE_SYSTEM,
    APP_PAGE_LOG,
    APP_PAGE_COUNT
} AppPage_t;

/* Build a multi-line text snapshot for the given page.
   Does not read g_app_state directly; caller must pass a locked snapshot. */
void App_Display_BuildPageText(AppPage_t page,
                               const AppState_t *snapshot,
                               char *buf,
                               uint16_t buf_size);

void App_Display_Init(void);
void App_DisplayTask(void *argument);
void App_Display_RequestPageNext(void);
void App_Display_RequestPagePrev(void);
void App_Display_SetPage(uint8_t page);
void App_Display_RequestRefresh(void);

/* Touch stubs — wired to backlight wakeup for now */
void App_Display_OnTouchWakeup(void);
void App_Display_OnTouchPoint(uint16_t x, uint16_t y);

#endif /* APP_DISPLAY_H */
