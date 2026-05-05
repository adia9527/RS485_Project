# STM32 LVGL Dashboard Simulator

This is a small Ubuntu desktop preview app for the STM32F429 LVGL dashboard UI.
It does not simulate the firmware, FreeRTOS, HAL, SPI, I2C, RS485, ESP32, flash,
ADC hardware, GPIO, or UART. It only renders a dashboard-like LVGL screen with
fake `AppState_t` data.

## Dependencies

```sh
sudo apt install build-essential cmake ninja-build libsdl2-dev
```

## Build

```sh
cd simulator
cmake -S . -B build -G Ninja
cmake --build build
```

Makefiles also work if Ninja is not installed:

```sh
cd simulator
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/lvgl_sim
```

## Keyboard Controls

- `N`: next page
- `P`: previous page
- `A`: toggle alarm state
- `H`: toggle human presence
- `M`: toggle MQTT connected state
- `Q` or `Esc`: quit

## Current Limitations

- The simulator mirrors the embedded dashboard layout, but it does not reuse
  `APP/Src/app_display.c` because that file depends on BSP, CMSIS-RTOS2,
  global firmware state, and STM32 display ports.
- The data is generated locally and is not connected to RS485, ADC hardware,
  MQTT, flash logging, or FreeRTOS tasks.
- The window is fixed at the target LCD size: 240x320.
