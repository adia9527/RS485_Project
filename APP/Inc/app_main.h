#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "cmsis_os.h"

extern osMutexId_t g_app_state_mutex;

void App_MainInit(void);
void App_CreateTasks(void);

#endif /* APP_MAIN_H */
