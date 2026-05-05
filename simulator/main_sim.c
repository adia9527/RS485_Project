#define SDL_MAIN_HANDLED

#include "sim_app_state.h"
#include "sim_dashboard.h"
#include "lvgl.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SIM_WINDOW_WIDTH  240
#define SIM_WINDOW_HEIGHT 320
#define SIM_REFRESH_MS    250U
#define SIM_LOOP_DELAY_MS 5U

static bool Sim_KeyPressed(const uint8_t *keys, const uint8_t *prev_keys, SDL_Scancode scancode)
{
    return (keys[scancode] != 0U) && (prev_keys[scancode] == 0U);
}

int main(void)
{
    SimAppState_t state;
    SimPage_t page = SIM_PAGE_MAIN;
    uint32_t last_tick;
    uint32_t last_refresh = 0U;
    uint8_t prev_keys[SDL_NUM_SCANCODES];
    bool running = true;

    memset(prev_keys, 0, sizeof(prev_keys));
    SDL_SetMainReady();

    lv_init();

    lv_display_t *display = lv_sdl_window_create(SIM_WINDOW_WIDTH, SIM_WINDOW_HEIGHT);
    if (display == NULL) {
        fprintf(stderr, "Failed to create LVGL SDL window\n");
        return 1;
    }

    lv_sdl_window_set_title(display, "STM32 LVGL Dashboard Simulator");
    lv_sdl_window_set_resizeable(display, false);
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();

    /* The SDL backend installs SDL_GetTicks as a tick callback. Use explicit
       lv_tick_inc here so the simulator loop mirrors the embedded DisplayTask. */
    lv_tick_set_cb(NULL);
    last_tick = SDL_GetTicks();

    Sim_AppState_Init(&state);
    Sim_AppState_Update(&state, last_tick);
    Sim_Dashboard_Init();
    Sim_Dashboard_Update(page, &state);
    lv_refr_now(NULL);

    printf("LVGL dashboard simulator\n");
    printf("Controls: N next, P previous, A alarm, H human, M MQTT, Q/ESC quit\n");

    while (running) {
        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - last_tick;
        bool page_changed = false;

        if (elapsed > 0U) {
            lv_tick_inc(elapsed);
            last_tick = now;
        }

        lv_timer_handler();
        if (lv_display_get_next(NULL) == NULL) {
            break;
        }

        const uint8_t *keys = SDL_GetKeyboardState(NULL);
        if (Sim_KeyPressed(keys, prev_keys, SDL_SCANCODE_N)) {
            page = Sim_Dashboard_NextPage(page);
            page_changed = true;
        }
        if (Sim_KeyPressed(keys, prev_keys, SDL_SCANCODE_P)) {
            page = Sim_Dashboard_PrevPage(page);
            page_changed = true;
        }
        if (Sim_KeyPressed(keys, prev_keys, SDL_SCANCODE_A)) {
            Sim_AppState_ToggleAlarm(&state);
            page_changed = true;
        }
        if (Sim_KeyPressed(keys, prev_keys, SDL_SCANCODE_H)) {
            Sim_AppState_ToggleHuman(&state);
            page_changed = true;
        }
        if (Sim_KeyPressed(keys, prev_keys, SDL_SCANCODE_M)) {
            Sim_AppState_ToggleMqtt(&state);
            page_changed = true;
        }
        if (Sim_KeyPressed(keys, prev_keys, SDL_SCANCODE_Q) ||
            Sim_KeyPressed(keys, prev_keys, SDL_SCANCODE_ESCAPE)) {
            running = false;
        }
        memcpy(prev_keys, keys, sizeof(prev_keys));

        Sim_AppState_Update(&state, now);
        if (page_changed || ((now - last_refresh) >= SIM_REFRESH_MS)) {
            last_refresh = now;
            Sim_Dashboard_Update(page, &state);
        }

        SDL_Delay(SIM_LOOP_DELAY_MS);
    }

    if (lv_display_get_next(NULL) != NULL) {
        lv_deinit();
    }

    return 0;
}
