#ifndef PROTOCOL_MODBUS_H
#define PROTOCOL_MODBUS_H

#include <stdint.h>

#define MODBUS_FC_READ_HOLDING_REGS 0x03

uint16_t Modbus_CRC16(uint8_t *data, uint16_t len);
uint16_t Modbus_BuildReadHoldingRegisters(uint8_t slave_addr, uint16_t start_reg, uint16_t reg_count, uint8_t *frame);
uint8_t  Modbus_CheckResponseCRC(uint8_t *resp, uint16_t len);
uint16_t Modbus_ParseReadRegistersResponse(uint8_t *resp, uint16_t len, uint16_t *regs);
uint16_t Modbus_GetExpectedResponseLength(uint16_t reg_count);

#endif /* PROTOCOL_MODBUS_H */
