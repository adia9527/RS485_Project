#ifndef SIM_APP_STATE_H
#define SIM_APP_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "app_types.h"

typedef enum {
    SIM_UPLOAD_TARGET_NONE = 0,
    SIM_UPLOAD_TARGET_UART,
    SIM_UPLOAD_TARGET_MQTT,
    SIM_UPLOAD_TARGET_UART_AND_MQTT
} SimUploadTarget_t;

typedef struct
{
    AppState_t        app;
    uint32_t          uptime_s;
    uint32_t          free_heap;
    bool              healthy;
    bool              mqtt_connected;
    SimUploadTarget_t upload_target;
    uint32_t          ram_log_count;
    uint32_t          flash_persist_ok;
    uint32_t          flash_persist_fail;
    uint32_t          log_drop_count;
    uint32_t          log_queue_count;
} SimAppState_t;

void Sim_AppState_Init(SimAppState_t *state);
void Sim_AppState_Update(SimAppState_t *state, uint32_t tick_ms);
void Sim_AppState_ToggleAlarm(SimAppState_t *state);
void Sim_AppState_ToggleHuman(SimAppState_t *state);
void Sim_AppState_ToggleMqtt(SimAppState_t *state);

#endif /* SIM_APP_STATE_H */
