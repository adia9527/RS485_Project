#ifndef APP_MQTT_CMD_H
#define APP_MQTT_CMD_H

#include <stdint.h>
#include "app_types.h"

void     App_MqttCmd_Init(void);
void     App_MqttCmd_HandleRaw(const char *subrecv_line);
void     App_MqttCmd_HandleDirect(const char *payload);
void     App_MqttCmd_GetStats(MqttCmdStats_t *out);

#endif /* APP_MQTT_CMD_H */
