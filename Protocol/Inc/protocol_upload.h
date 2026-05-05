#ifndef PROTOCOL_UPLOAD_H
#define PROTOCOL_UPLOAD_H

#include "app_types.h"
#include "app_health.h"
#include <stdint.h>

typedef enum {
    UPLOAD_FORMAT_TEXT = 0,
    UPLOAD_FORMAT_JSON = 1
} UploadFormat_t;

int Protocol_Upload_FormatDataText(char *buf, uint16_t buf_size, const AppState_t *s);
int Protocol_Upload_FormatDataJson(char *buf, uint16_t buf_size, const AppState_t *s);
int Protocol_Upload_FormatCommText(char *buf, uint16_t buf_size, const AppState_t *s);
int Protocol_Upload_FormatCommJson(char *buf, uint16_t buf_size, const AppState_t *s);
int Protocol_Upload_FormatAlarmText(char *buf, uint16_t buf_size, const AppState_t *s);
int Protocol_Upload_FormatAlarmJson(char *buf, uint16_t buf_size, const AppState_t *s);
int Protocol_Upload_FormatStatusJson(char *buf, uint16_t buf_size, const AppState_t *s,
                                     const AppHealthSnapshot_t *hs);
int Protocol_Upload_FormatHeartbeatJson(char *buf, uint16_t buf_size,
                                        uint32_t uptime_s, uint8_t healthy);
int Protocol_Upload_FormatUploadTestJson(char *buf, uint16_t buf_size, const char *target);

#endif /* PROTOCOL_UPLOAD_H */
