/**
 * LVGL v9 simulator configuration.
 *
 * This file is used only by simulator/CMakeLists.txt through LV_CONF_PATH.
 * It is intentionally separate from LVGL/lv_conf.h so the STM32 firmware
 * configuration remains unchanged.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE   <stdint.h>
#define LV_STDDEF_INCLUDE   <stddef.h>
#define LV_STDBOOL_INCLUDE  <stdbool.h>
#define LV_INTTYPES_INCLUDE <inttypes.h>
#define LV_LIMITS_INCLUDE   <limits.h>
#define LV_STDARG_INCLUDE   <stdarg.h>

#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 130

#define LV_USE_OS LV_OS_NONE

#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4

#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_SW_COMPLEX       0
#define LV_DRAW_SW_SUPPORT_RGB565  1
#define LV_DRAW_SW_SUPPORT_RGB888  1
#define LV_DRAW_SW_SUPPORT_XRGB8888 1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_A8       1

#define LV_USE_DRAW_SDL 0

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT 0

#define LV_USE_ANIMIMG     0
#define LV_USE_ARC         0
#define LV_USE_ARCLABEL    0
#define LV_USE_BAR         0
#define LV_USE_BUTTON      0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR    0
#define LV_USE_CANVAS      0
#define LV_USE_CHART       0
#define LV_USE_CHECKBOX    0
#define LV_USE_DROPDOWN    0
#define LV_USE_GIF         0
#define LV_USE_IMAGE       0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD    0
#define LV_USE_LED         0
#define LV_USE_LINE        0
#define LV_USE_LIST        0
#define LV_USE_LOTTIE      0
#define LV_USE_MENU        0
#define LV_USE_MSGBOX      0
#define LV_USE_ROLLER      0
#define LV_USE_SCALE       0
#define LV_USE_SLIDER      0
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     0
#define LV_USE_SWITCH      0
#define LV_USE_TABLE       0
#define LV_USE_TABVIEW     0
#define LV_USE_TEXTAREA    0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

#define LV_USE_THEME_DEFAULT 0

#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_SDL_BUF_COUNT   1
#define LV_SDL_ACCELERATED 0
#define LV_SDL_FULLSCREEN  0
#define LV_SDL_DIRECT_EXIT 0
#define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS    0

#endif /* LV_CONF_H */
