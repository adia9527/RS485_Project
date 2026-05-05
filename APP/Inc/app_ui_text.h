#ifndef APP_UI_TEXT_H
#define APP_UI_TEXT_H

#include <stdint.h>
#include "app_types.h"

typedef enum {
    APP_UI_PAGE_MAIN = 0,
    APP_UI_PAGE_SENSOR,
    APP_UI_PAGE_COMM,
    APP_UI_PAGE_ALARM,
    APP_UI_PAGE_ADC,
    APP_UI_PAGE_SYSTEM,
    APP_UI_PAGE_LOG,
    APP_UI_PAGE_COUNT
} AppUiPage_t;

void App_UI_BuildPageText(AppUiPage_t page,
                          const AppState_t *snapshot,
                          char *buf,
                          uint16_t buf_size);

#endif /* APP_UI_TEXT_H */
