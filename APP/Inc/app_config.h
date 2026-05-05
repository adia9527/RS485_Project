#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "app_types.h"

#define APP_CONFIG_MAGIC    0xA5A55A5AUL    //判断 Flash 里的数据是不是有效配置
#define APP_CONFIG_VERSION  0x0005U //判断配置数据结构的版本

#define APP_CONFIG_FLASH_SECTOR   FLASH_SECTOR_7    //指定配置数据存在哪个 Flash 扇区
#define APP_CONFIG_FLASH_ADDRESS  0x08060000UL  //指定配置数据在 Flash 中的起始地址

void     App_Config_Init(void);
void     App_Config_LoadDefault(SysConfig_t *cfg);
uint8_t  App_Config_Load(SysConfig_t *cfg);
uint8_t  App_Config_Save(const SysConfig_t *cfg);
uint8_t  App_Config_IsValid(const SysConfig_t *cfg);
void     App_Config_MarkDirty(void);
void     App_Config_RestoreDefault(void);
void     App_ConfigTask(void *argument);

#endif /* APP_CONFIG_H */
