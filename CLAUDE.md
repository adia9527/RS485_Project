# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (run once or after CMakeLists changes)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Clean rebuild
cmake --build build --clean-first
```

Output binary: `build/myproject.elf`

## Architecture

**Target**: STM32F429VET6, HAL + FreeRTOS (CMSIS-RTOS v2), C11, arm-none-eabi-gcc

**Layer structure** (dependency flows upward):

```
APP/          app_main.c, app_monitor.c — FreeRTOS tasks, global state
Protocol/     protocol_modbus.c         — Modbus RTU framing, CRC16, parsing
BSP/          bsp_led/beep/key/adc/rs485/bsp_log — hardware abstraction
Core/Src/     CubeMX-generated peripheral init (DO NOT EDIT outside USER CODE sections)
```

**Global state**: `AppState_t g_app_state` defined in `APP/Src/app_main.c`, declared extern in `APP/Inc/app_types.h`.

**FreeRTOS init flow**:
1. `main.c USER CODE BEGIN 2` → `App_MainInit()` (BSP init, boot log)
2. `freertos.c USER CODE BEGIN RTOS_THREADS` → `App_CreateTasks()` (spawns MonitorTask)
3. `osKernelStart()` takes over

## Key Hardware Mappings

| Peripheral | Handle | Notes |
|-----------|--------|-------|
| RS485_1 | `huart1` | DIR pin: PA11 (`RS4851_DIR`) |
| RS485_2 | `huart3` | DIR pin: PD10 (`RS4852_DIR`) |
| Debug log | `huart7` | Type-C UART, used by `BSP_Log_Printf` |
| ESP32 | `huart4` | Reserved, not yet implemented |
| ADC PA3-PA6 | `hadc1` | **Injected channels** CH3-CH6 (ranks 1-4); Regular DMA = CH3 only |

All CubeMX peripheral handles (`huart1/3/4/7`, `hadc1`, `hspi1/2`, `hi2c1/3`) are declared in their respective `Core/Inc/*.h` headers — use `extern` via those headers, never redeclare.

## CubeMX Constraints

- Never edit outside `/* USER CODE BEGIN */` / `/* USER CODE END */` blocks in `Core/`
- GPIO pin macros (LED1/2/3, BEEP, KEY1/2/3, RS485 DIR, etc.) are defined in `Core/Inc/main.h`
- ADC uses **injected** conversions for all 4 channels — `bsp_adc.c` uses `HAL_ADCEx_InjectedStart/PollForConversion/GetValue`. The regular DMA channel only covers CH3.
- Clock source is HSI (not HSE) — do not change `SystemClock_Config()`

## Adding New Modules

- BSP drivers → `BSP/Src/` + `BSP/Inc/`, add to `target_sources` in root `CMakeLists.txt`
- App tasks → `APP/Src/` + `APP/Inc/`, create task in `App_CreateTasks()` with `osThreadNew`
- Protocol parsers → `Protocol/Src/` + `Protocol/Inc/`
- Placeholder task attrs for SensorTask/AlarmTask/UploadTask/DisplayTask are commented in `APP/Src/app_main.c`
