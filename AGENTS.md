# AGENTS.md

## Project Overview

This is an STM32F429-based embedded project for a multi-sensor environmental monitoring terminal.

The system includes:

- STM32F429VET6
- FreeRTOS / CMSIS-RTOS2
- LVGL v9.5.0
- ILI9341 SPI TFT display
- FT6336 I2C capacitive touch
- RS485 Modbus RTU sensors
- ADC inputs
- W25Q64 SPI Flash
- ESP32 AT WiFi/MQTT module
- USART1 local command console
- Event log and health monitor system

The project is not a quick demo. It is intended to be an engineering-style embedded system.

## Important Development Philosophy

Do not over-polish one feature while leaving the system incomplete.

Prefer:

1. Minimal complete feature chain
2. Compileable code
3. Clear module boundaries
4. Small, reviewable changes
5. Hardware-testable behavior

Avoid:

- Large rewrites unless necessary
- Adding unnecessary frameworks
- Creating complicated UI before the data path is stable
- Breaking existing USART1 console commands
- Breaking FreeRTOS tasks
- Breaking the current CMake build

## Coding Rules

- Use C, not C++.
- Use HAL and CMSIS-RTOS2 style APIs already used in the project.
- Do not modify CubeMX generated code outside USER CODE sections.
- Keep BSP, APP, and Protocol layers separate.
- Do not put business logic into BSP files.
- Do not put hardware-specific code into UI text generation.
- Do not use malloc unless already used by the project.
- Use snprintf instead of sprintf.
- Avoid large stack buffers.
- Keep all public APIs declared in matching header files.
- Do not remove existing USART1 console commands unless explicitly requested.
- Do not remove fallback behavior.

## Build Rules

Before finishing any task:

1. Run the existing build command if available.
2. Report error count and warning count.
3. If the only failure is the known LVGL Flash overflow, clearly say so.
4. Do not claim success if there are new compile errors.
5. Do not hide warnings.

Known current issue:

- Enabling full LVGL may cause Flash overflow on the 512KB STM32F429VET6 build.
- For LVGL work, prefer minimal LVGL configuration and do not enable demos or unused widgets.

## LVGL Rules

Current display hardware:

- ILI9341 / ILI9341V compatible SPI TFT
- Resolution: 240x320
- LVGL version: v9.5.0
- LCD uses SPI1
- W25Q64 also uses SPI1
- SPI1 must be protected by BSP_SPI1Bus_Lock / BSP_SPI1Bus_Unlock

LVGL requirements:

- Use LVGL v9 API.
- Keep the first UI simple.
- Use reusable page data generated from project state.
- Do not add image assets or Chinese fonts in early UI stages.
- Do not enable LVGL demos.
- Do not enable unnecessary widgets.
- Keep APP_USE_LVGL switch usable.
- UART fallback display must remain available.

## Existing Display Concept

The project already has:

- BSP_LCD for ILI9341
- LVGL display port / flush callback
- Touch input skeleton
- App_Display_BuildPageText or equivalent text page generation
- DisplayTask
- APP_USE_LVGL macro

The goal is to complete the practical LVGL application layer without destroying the fallback display path.

## Git / Diff Expectations

When making changes:

- List modified files.
- Explain why each file changed.
- Keep changes minimal.
- Prefer improving existing modules over creating parallel duplicate systems.
