#ifndef BSP_SPI_BUS_H
#define BSP_SPI_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void    BSP_SPI1Bus_Init(void);
void    BSP_SPI1Bus_Lock(void);
void    BSP_SPI1Bus_Unlock(void);
uint8_t BSP_SPI1Bus_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_BUS_H */
