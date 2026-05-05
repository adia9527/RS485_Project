#ifndef SIM_DASHBOARD_H
#define SIM_DASHBOARD_H

#include "sim_app_state.h"

typedef enum {
    SIM_PAGE_MAIN = 0,
    SIM_PAGE_SENSOR,
    SIM_PAGE_COMM,
    SIM_PAGE_ALARM,
    SIM_PAGE_ADC,
    SIM_PAGE_SYSTEM,
    SIM_PAGE_LOG,
    SIM_PAGE_COUNT
} SimPage_t;

void Sim_Dashboard_Init(void);
void Sim_Dashboard_Update(SimPage_t page, const SimAppState_t *state);
SimPage_t Sim_Dashboard_NextPage(SimPage_t page);
SimPage_t Sim_Dashboard_PrevPage(SimPage_t page);

#endif /* SIM_DASHBOARD_H */
