cmake_minimum_required(VERSION 3.22)

project(lvgl_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

find_package(SDL2 REQUIRED)

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_ROOT}/LVGL/src/*.c"
)

add_executable(lvgl_sim
    main_sim.c
    sim_app_state.c
    sim_dashboard.c
    ${LVGL_SOURCES}
)

target_include_directories(lvgl_sim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_ROOT}/APP/Inc"
    "${PROJECT_ROOT}/LVGL"
    "${PROJECT_ROOT}/LVGL/src"
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(lvgl_sim PRIVATE
    LV_SIMULATOR=1
    LV_CONF_PATH=\"lv_conf_sim.h\"
)

if(TARGET SDL2::SDL2)
    target_link_libraries(lvgl_sim PRIVATE SDL2::SDL2 m)
else()
    target_link_libraries(lvgl_sim PRIVATE ${SDL2_LIBRARIES} m)
endif()
