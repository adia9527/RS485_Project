cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME myproject)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)
# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -std=gnu11 -ffunction-sections -fdata-sections)
# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--gc-sections)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # BSP layer
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_led.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_beep.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_key.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_adc.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_rs485.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_log.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_flash.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_watchdog.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_w25q64.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_lcd.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_spi_bus.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_esp32.c
    ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_touch.c
    # Protocol layer
    ${CMAKE_SOURCE_DIR}/Protocol/Src/protocol_modbus.c
    ${CMAKE_SOURCE_DIR}/Protocol/Src/protocol_upload.c
    # APP layer
    ${CMAKE_SOURCE_DIR}/APP/Src/app_event_log.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_log_storage.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_health.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_main.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_monitor.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_sensor.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_alarm.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_upload.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_config.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_display.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_ui_text.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_backlight.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_cmd.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_esp32.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_mqtt_cmd.c
    ${CMAKE_SOURCE_DIR}/APP/Src/app_touch.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/BSP/Inc
    ${CMAKE_SOURCE_DIR}/Protocol/Inc
    ${CMAKE_SOURCE_DIR}/APP/Inc
)

# ================= LVGL =================
option(APP_USE_LVGL "Enable LVGL display application" ON)

if(APP_USE_LVGL)
    file(GLOB_RECURSE LVGL_SOURCES
        ${CMAKE_SOURCE_DIR}/LVGL/src/*.c
        ${CMAKE_SOURCE_DIR}/LVGL/porting/*.c
    )

    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${LVGL_SOURCES}
    )

    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/LVGL
        ${CMAKE_SOURCE_DIR}/LVGL/src
        ${CMAKE_SOURCE_DIR}/LVGL/porting
    )
endif()

# ==================================

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    APP_USE_LVGL=$<IF:$<BOOL:${APP_USE_LVGL}>,1,0>
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_PROJECT_NAME}.elf ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating .bin file"
)

add_custom_target(flash
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMAND st-flash --reset write ${CMAKE_PROJECT_NAME}.bin 0x08000000
    COMMENT "Flashing via ST-Link"
)
