#ifndef BSP_KEY_H
#define BSP_KEY_H

#include <stdint.h>

#define BSP_KEY_COUNT 3 //按键数量

typedef enum {
    BSP_KEY_1 = 0,
    BSP_KEY_2,
    BSP_KEY_3
} BSP_Key_t;

typedef enum {
    BSP_KEY_EVENT_NONE = 0,
    BSP_KEY_EVENT_PRESS,
    BSP_KEY_EVENT_LONG_PRESS,
    BSP_KEY_EVENT_RELEASE
} BSP_KeyEvent_t;

typedef struct {
    BSP_Key_t      key;
    BSP_KeyEvent_t event;
} BSP_KeyInfo_t;

void BSP_Key_Init(void);
BSP_KeyInfo_t BSP_Key_Scan(void);

#endif /* BSP_KEY_H */
