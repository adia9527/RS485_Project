/**
 * @file    app_log_storage.c
 * @brief   Event log persistence to W25Q64 external SPI Flash.
 *
 * Storage region: 0x7C0000 – 0x7FFFFF  (256 KB, last 64 sectors of 4 KB each)
 *
 * Strategy: simple append-only log.
 *   - At init, scan from EVENT_LOG_FLASH_BASE to find the first empty slot
 *     (magic == 0xFFFFFFFF).
 *   - New records are appended at next_write_addr.
 *   - When the area is full, erase the entire 256 KB region and restart.
 *   - Each record is 128 bytes (fixed size); boundary alignment is guaranteed.
 *
 * Persistence filter: only "important" event types are written to Flash to
 * reduce write amplification.  All events still go to the RAM ring buffer.
 *
 * Thread safety: The W25Q64 BSP driver already holds an internal SPI mutex.
 *   App_EventLog_Persist() is intended to be called from a single
 *   LogStorageTask in a future phase, or directly from the event-log add path
 *   (which holds the event-log mutex — Flash write is fast enough for the
 *   current phase).  Do NOT call from an ISR.
 */

#include "app_log_storage.h"
#include "app_health.h"
#include "bsp_w25q64.h"
#include "bsp_log.h"
#include "cmsis_os.h"
#include <string.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Persistent event types whitelist                                   */
/* 判断某一种事件类型是否需要“持久化保存”到日志存储里                          */
/* ------------------------------------------------------------------ */
static uint8_t AppLogStorage_ShouldPersist(AppEventType_t type)
{
    switch (type)
    {
        case APP_EVENT_SYSTEM_BOOT:
        case APP_EVENT_SYSTEM_HEALTH_FAULT:
        case APP_EVENT_SYSTEM_HEALTH_RECOVERED:
        case APP_EVENT_ALARM_ACTIVE:
        case APP_EVENT_ALARM_CLEARED:
        case APP_EVENT_COMM_OFFLINE:
        case APP_EVENT_COMM_RECOVERED:
        case APP_EVENT_CONFIG_SAVED:
        case APP_EVENT_CONFIG_RESTORE_DEFAULT:
            //以上事件需要持久化
            return 1U;
        default:
            return 0U;
    }
}

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */
static uint32_t s_next_write_addr = EVENT_LOG_FLASH_BASE; //下一条日志写入的地址
static uint32_t s_record_count    = 0U; //flash中保存的日志数量

/* ------------------------------------------------------------------ */
/*  Async queue state                                                  */
/* ------------------------------------------------------------------ */
static osMessageQueueId_t s_queue;
static uint32_t s_persist_ok_count;//持久化成功次数计数器，表示保存了多少条日志
static uint32_t s_persist_fail_count;//持久化失败次数计数器：写 Flash 失败、存储器异常、校验失败等
static uint32_t s_drop_count;//日志丢弃计数器：因为队列满而被丢弃的日志
static uint8_t  s_enabled;//日志持久化功能是否启用的标志位
static volatile uint8_t s_busy;//忙碌标志位，表示日志存储模块当前是否正在执行写入操作

/* ------------------------------------------------------------------ */
/*  Checksum: simple 32-bit XOR over all bytes except checksum field  */
//校验和，用来判断记录是否损坏：整条日志记录按 字节 遍历一遍，然后对每个字节做异或运算 ^，最后得到一个校验值（会跳过结构体中的 checksum 字段本身）。
/* ------------------------------------------------------------------ */
static uint32_t CalcChecksum(const AppEventFlashRecord_t *r)
{
    const uint8_t *p   = (const uint8_t *)r;
    uint32_t       sum = 0U;
    uint32_t       cs_offset = (uint32_t)offsetof(AppEventFlashRecord_t, checksum);
    for (uint32_t i = 0U; i < EVENT_LOG_FLASH_RECORD_SIZE; i++)
    {
        //checksum 字段从结构体第 96 个字节开始，整体为0～127
        if (i >= cs_offset && i < (cs_offset + 4U)) { continue; } //跳过checksum
        sum ^= (uint32_t)p[i];
    }
    return sum;
}

/* ------------------------------------------------------------------ */
/*  Erase entire log region   擦除整个日志区域                                        */
/* ------------------------------------------------------------------ */
static HAL_StatusTypeDef EraseLogRegion(void)
{
    BSP_Log_Printf("[EVENT] erasing flash log region...\r\n");
    for (uint32_t s = 0U; s < EVENT_LOG_FLASH_SECTORS; s++)
    {
        uint32_t addr = EVENT_LOG_FLASH_BASE + s * W25Q64_SECTOR_SIZE;
        HAL_StatusTypeDef st = BSP_W25Q64_EraseSector(addr);
        if (st != HAL_OK)
        {
            BSP_Log_Printf("[EVENT] erase sector 0x%06lX failed\r\n",
                           (unsigned long)addr);
            return st;
        }
    }
    BSP_Log_Printf("[EVENT] flash log region erased\r\n");
    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/*  Init: scan to find next_write_addr                                 */
//初始化事件日志在 W25Q64 Flash 中的存储状态，扫描已有日志记录，找到下一次应该写入的位置，并统计有效日志数量。
/* ------------------------------------------------------------------ */
void App_EventLog_StorageInit(void)
{
    if (!BSP_W25Q64_IsDetected())
    {
        BSP_Log_Printf("[EVENT] flash log storage disabled (W25Q64 not detected)\r\n");
        return;
    }

    s_next_write_addr = EVENT_LOG_FLASH_BASE;
    s_record_count    = 0U;

    /* Read records one at a time, looking for the first empty slot */
    AppEventFlashRecord_t rec;
    uint32_t max_records = EVENT_LOG_FLASH_SIZE / EVENT_LOG_FLASH_RECORD_SIZE;

    for (uint32_t i = 0U; i < max_records; i++)
    {
        uint32_t addr = EVENT_LOG_FLASH_BASE + i * EVENT_LOG_FLASH_RECORD_SIZE;
        HAL_StatusTypeDef st = BSP_W25Q64_Read(addr,
                                               (uint8_t *)&rec,
                                               EVENT_LOG_FLASH_RECORD_SIZE);
        if (st != HAL_OK) { break; }
        
        //判断是否为空记录
        if (rec.magic == 0xFFFFFFFFUL)
        {
            /* Found empty slot */
            s_next_write_addr = addr;
            break;
        }

        /* Validate magic + checksum 校验已有日志是否有效 */
        if (rec.magic == EVENT_LOG_FLASH_MAGIC)
        {
            uint32_t expected_cs = CalcChecksum(&rec);
            if (rec.checksum == expected_cs) { s_record_count++; }
        }

        /* If this was the last slot, wrap around */
        //如果日志区域已经写满，擦除整个日志区域并重置
        if (i == (max_records - 1U))
        {
            BSP_Log_Printf("[EVENT] flash log full, erasing...\r\n");
            if (EraseLogRegion() == HAL_OK)
            {
                s_next_write_addr = EVENT_LOG_FLASH_BASE;
                s_record_count    = 0U;
            }
        }
    }

    BSP_Log_Printf("[EVENT] flash log init ok records=%lu next=0x%06lX\r\n",
                   (unsigned long)s_record_count,
                   (unsigned long)s_next_write_addr);
}

/* ------------------------------------------------------------------ */
/*  Persist one event to Flash                                         */
/* ------------------------------------------------------------------ */
uint8_t App_EventLog_StoragePersist(const AppEventLogItem_t *item)
{
    if (!BSP_W25Q64_IsDetected()) { return 0U; }
    if (item == NULL)              { return 0U; }
    if (!AppLogStorage_ShouldPersist(item->type)) { return 1U; }

    uint32_t end_addr = EVENT_LOG_FLASH_BASE + EVENT_LOG_FLASH_SIZE;
    if ((s_next_write_addr + EVENT_LOG_FLASH_RECORD_SIZE) > end_addr)
    {
        if (EraseLogRegion() != HAL_OK) { return 0U; }
        s_next_write_addr = EVENT_LOG_FLASH_BASE;
        s_record_count    = 0U;
        BSP_Log_Printf("[EVENT] flash log wrapped\r\n");
    }

    if ((s_next_write_addr % W25Q64_SECTOR_SIZE) == 0U)
    {
        if (BSP_W25Q64_EraseSector(s_next_write_addr) != HAL_OK)
        {
            BSP_Log_Printf("[EVENT] erase sector 0x%06lX failed\r\n",
                           (unsigned long)s_next_write_addr);
            return 0U;
        }
    }

    AppEventFlashRecord_t rec;
    memset(&rec, 0U, sizeof(rec));
    rec.magic   = EVENT_LOG_FLASH_MAGIC;
    rec.version = EVENT_LOG_FLASH_VERSION;
    rec.size    = (uint16_t)sizeof(rec);
    rec.id      = item->id;
    rec.tick    = item->tick;
    rec.type    = (uint32_t)item->type;
    rec.source  = item->source;
    rec.value_i = item->value_i;
    rec.value_f = item->value_f;
    memcpy(rec.message, item->message, sizeof(rec.message) - 1U);
    rec.checksum = CalcChecksum(&rec);

    HAL_StatusTypeDef st = BSP_W25Q64_Write(s_next_write_addr,
                                            (const uint8_t *)&rec,
                                            EVENT_LOG_FLASH_RECORD_SIZE);
    if (st == HAL_OK)
    {
        s_next_write_addr += EVENT_LOG_FLASH_RECORD_SIZE;
        s_record_count++;
        return 1U;
    }
    else
    {
        BSP_Log_Printf("[EVENT] flash write failed at 0x%06lX\r\n",
                       (unsigned long)s_next_write_addr);
        return 0U;
    }
}

/* ------------------------------------------------------------------ */
/*  Statistics                                                         */
/* ------------------------------------------------------------------ */
uint32_t App_EventLog_StorageGetCount(void)    { return s_record_count; }
uint32_t App_EventLog_StorageGetNextAddr(void) { return s_next_write_addr; }

/* ------------------------------------------------------------------ */
/*  Read latest N records (chronologically from oldest to newest)     */
/* ------------------------------------------------------------------ */
uint16_t App_EventLog_StorageReadLatest(AppEventFlashRecord_t *out,
                                        uint16_t max_count)
{
    if (!BSP_W25Q64_IsDetected() || out == NULL || max_count == 0U) { return 0U; }

    uint32_t max_records  = EVENT_LOG_FLASH_SIZE / EVENT_LOG_FLASH_RECORD_SIZE;
    uint32_t valid_count  = (s_record_count < max_records) ? s_record_count : max_records;
    if (valid_count == 0U) { return 0U; }

    /* Determine start index (read latest max_count records) */
    uint32_t start_idx = (valid_count > (uint32_t)max_count)
                         ? (valid_count - (uint32_t)max_count)
                         : 0U;
    uint32_t read_count = valid_count - start_idx;

    uint16_t found = 0U;
    for (uint32_t i = start_idx; i < valid_count && found < max_count; i++)
    {
        uint32_t addr = EVENT_LOG_FLASH_BASE + i * EVENT_LOG_FLASH_RECORD_SIZE;
        HAL_StatusTypeDef st = BSP_W25Q64_Read(addr,
                                               (uint8_t *)&out[found],
                                               EVENT_LOG_FLASH_RECORD_SIZE);
        if (st != HAL_OK) { break; }
        if (out[found].magic != EVENT_LOG_FLASH_MAGIC) { continue; }
        uint32_t cs = CalcChecksum(&out[found]);
        if (out[found].checksum != cs) { continue; }
        found++;
    }
    (void)read_count; /* suppress unused warning */
    return found;
}

/* ------------------------------------------------------------------ */
/*  Clear flash log                                                    */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef App_EventLog_StorageClear(void)
{
    if (!BSP_W25Q64_IsDetected()) { return HAL_OK; }
    if (s_queue != NULL) {
        osMessageQueueReset(s_queue);
    }
    HAL_StatusTypeDef st = EraseLogRegion();
    if (st == HAL_OK)
    {
        s_next_write_addr = EVENT_LOG_FLASH_BASE;
        s_record_count    = 0U;
    }
    return st;
}

/* ================================================================== */
/*  Async queue layer                                                 */
/* ================================================================== */

void App_LogStorage_Init(void)
{
    s_queue = osMessageQueueNew(LOG_STORAGE_QUEUE_DEPTH, sizeof(AppEventLogItem_t), NULL);
    s_persist_ok_count   = 0U;
    s_persist_fail_count = 0U;
    s_drop_count         = 0U;
    s_enabled = BSP_W25Q64_IsDetected();
    if (s_queue != NULL) {
        BSP_Log_Printf("[LOGST] queue ready depth=%u\r\n", (unsigned)LOG_STORAGE_QUEUE_DEPTH);
    }
}

//把一条需要持久化保存的事件日志放入日志存储队列中，后面另一个任务从队列取出，再写入 Flash 或其他存储器
uint8_t App_LogStorage_Enqueue(const AppEventLogItem_t *item)
{
    if (s_queue == NULL || item == NULL) { return 0U; }
    if (!AppLogStorage_ShouldPersist(item->type)) { return 1U; }

    osStatus_t st = osMessageQueuePut(s_queue, item, 0U, 0U);
    if (st != osOK) {
        s_drop_count++;
        if ((s_drop_count % 10U) == 1U) {
            BSP_Log_Printf("[LOGST] queue full drop=%lu\r\n", (unsigned long)s_drop_count);
        }
        return 0U;
    }
    return 1U;
}

uint8_t App_LogStorage_IsEnabled(void)  { return s_enabled; }
uint32_t App_LogStorage_GetPersistOkCount(void)   { return s_persist_ok_count; }
uint32_t App_LogStorage_GetPersistFailCount(void) { return s_persist_fail_count; }
uint32_t App_LogStorage_GetDropCount(void)        { return s_drop_count; }

uint32_t App_LogStorage_GetQueuedCount(void)
{
    return (s_queue != NULL) ? osMessageQueueGetCount(s_queue) : 0U;
}

//等待日志存储模块把队列里的日志全部写完。如果在指定超时时间内写完，返回 1U；如果超时还没写完，返回 0U
uint8_t App_LogStorage_Flush(uint32_t timeout_ms)
{
    if (s_queue == NULL) { return 1U; }
    uint32_t t0 = osKernelGetTickCount();
    while (osMessageQueueGetCount(s_queue) > 0U || s_busy) {
        if ((osKernelGetTickCount() - t0) >= timeout_ms) { return 0U; }
        osDelay(10U);
    }
    return 1U;
}

void App_LogStorageTask(void *argument)
{
    (void)argument;
    BSP_Log_Printf("[APP] LogStorageTask started\r\n");

    AppEventLogItem_t item;
    for (;;)
    {
        App_Health_Beat(APP_TASK_ID_LOG_STORAGE);

        if (osMessageQueueGet(s_queue, &item, NULL, 9000U) == osOK)
        {
            s_busy = 1U;
            if (!s_enabled) {
                s_persist_fail_count++;
            } else if (App_EventLog_StoragePersist(&item)) {
                s_persist_ok_count++;
            } else {
                s_persist_fail_count++;
            }
            s_busy = 0U;
        }
    }
}
