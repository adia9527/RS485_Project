#ifndef APP_CMD_H
#define APP_CMD_H

#include "protocol_upload.h"
#include "stm32f4xx_hal.h"

typedef enum { APP_CMD_SOURCE_USART1 = 0, APP_CMD_SOURCE_MQTT } AppCmdSource_t;

typedef struct {
    AppCmdSource_t  source;
    char           *resp_buf;
    uint16_t        resp_buf_size;
} AppCmdContext_t;

void            App_Cmd_Init(void);
void            App_CmdTask(void *argument);
void            App_Cmd_UartRxCallback(UART_HandleTypeDef *huart);
UploadFormat_t  App_Cmd_GetUploadFormat(void);
void            App_Cmd_ExecuteLine(const char *line, AppCmdContext_t *ctx);

#endif /* APP_CMD_H */
