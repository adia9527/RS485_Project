#ifndef APP_LOG_STORAGE_H
#define APP_LOG_STORAGE_H

#include "app_event_log.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Flash layout                                                       */
/* ------------------------------------------------------------------ */
#define EVENT_LOG_FLASH_BASE     (0x7C0000UL) //事件日志区域在 W25Q64 中的起始地址
#define EVENT_LOG_FLASH_SIZE     (256U * 1024U) //事件日志区域总大小，256KB
#define EVENT_LOG_FLASH_SECTORS  (EVENT_LOG_FLASH_SIZE / 4096U) //日志区域占用多少个 4KB 扇区

/* ------------------------------------------------------------------ */
/*  Record format                                                      */
/* ------------------------------------------------------------------ */
#define EVENT_LOG_FLASH_MAGIC        0x45564C47UL //日志记录魔数，用于识别有效日志
#define EVENT_LOG_FLASH_VERSION      0x0001U //日志记录格式版本号
#define EVENT_LOG_FLASH_RECORD_SIZE  128U //每条日志记录固定占 128 字节

//事件日志记录
typedef struct __attribute__((packed))
{
    uint32_t magic; //魔数，用于判断这是不是一条有效日志
    uint16_t version;   //日志格式版本号
    uint16_t size;  //当前结构体/记录大小
    uint32_t id;    //日志编号
    uint32_t tick;  //事件发生时的系统 tick
    uint32_t type;  //事件类型
    uint8_t  source;    //事件来源
    uint8_t  reserved0[3];  //保留/对齐用 
    int32_t  value_i;   //整型事件值
    float    value_f;   //浮点型事件值
    char     message[64];   //事件描述字符串 
    uint32_t checksum;  //校验和，用于判断记录是否损坏
    uint8_t  reserved[28];  //保留字段，方便以后扩展 
} AppEventFlashRecord_t;

_Static_assert(sizeof(AppEventFlashRecord_t) == EVENT_LOG_FLASH_RECORD_SIZE,
               "AppEventFlashRecord_t must be exactly 128 bytes");

/* ------------------------------------------------------------------ */
/*  Queue depth                                                        */
/* ------------------------------------------------------------------ */
#define LOG_STORAGE_QUEUE_DEPTH  16U

/* ------------------------------------------------------------------ */
/*  Public API — Flash layer                                           */
/* ------------------------------------------------------------------ */
void              App_EventLog_StorageInit(void);
uint8_t           App_EventLog_StoragePersist(const AppEventLogItem_t *item);
uint32_t          App_EventLog_StorageGetCount(void);
uint32_t          App_EventLog_StorageGetNextAddr(void);
uint16_t          App_EventLog_StorageReadLatest(AppEventFlashRecord_t *out, uint16_t max_count);
HAL_StatusTypeDef App_EventLog_StorageClear(void);

/* ------------------------------------------------------------------ */
/*  Public API — async queue layer                                     */
/* ------------------------------------------------------------------ */
void     App_LogStorage_Init(void);
void     App_LogStorageTask(void *argument);

uint8_t  App_LogStorage_Enqueue(const AppEventLogItem_t *item);
uint8_t  App_LogStorage_IsEnabled(void);
uint8_t  App_LogStorage_Flush(uint32_t timeout_ms);

uint32_t App_LogStorage_GetPersistOkCount(void);
uint32_t App_LogStorage_GetPersistFailCount(void);
uint32_t App_LogStorage_GetDropCount(void);
uint32_t App_LogStorage_GetQueuedCount(void);

#endif /* APP_LOG_STORAGE_H */
