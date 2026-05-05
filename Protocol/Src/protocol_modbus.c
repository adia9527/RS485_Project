#include "protocol_modbus.h"

//计算CRC16校验码
uint16_t Modbus_CRC16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0U; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0U; j < 8U; j++) {
            if (crc & 0x0001U) {
                crc = (crc >> 1) ^ 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

//组装一帧 Modbus RTU 的“读保持寄存器”请求帧。
// | 参数           | 含义                     |
// | ------------ | ---------------------- |
// | `slave_addr` | 从机地址，也就是你要问哪个 RS485 设备 |
// | `start_reg`  | 起始寄存器地址，从哪个寄存器开始读      |
// | `reg_count`  | 要连续读取几个寄存器             |
// | `frame`      | 输出缓冲区，用来存放组装好的请求帧      |
uint16_t Modbus_BuildReadHoldingRegisters(uint8_t slave_addr, uint16_t start_reg, uint16_t reg_count, uint8_t *frame)
{
    frame[0] = slave_addr;
    frame[1] = MODBUS_FC_READ_HOLDING_REGS;
    frame[2] = (uint8_t)(start_reg >> 8);
    frame[3] = (uint8_t)(start_reg & 0xFFU);
    frame[4] = (uint8_t)(reg_count >> 8);
    frame[5] = (uint8_t)(reg_count & 0xFFU);

    uint16_t crc = Modbus_CRC16(frame, 6U);
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)(crc >> 8);

    return 8U;
}

//检查一帧 Modbus 响应数据的 CRC 是否正确
uint8_t Modbus_CheckResponseCRC(uint8_t *resp, uint16_t len)
{
    if (len < 3U) return 0U;
    uint16_t crc_calc = Modbus_CRC16(resp, len - 2U);
    uint16_t crc_recv = (uint16_t)(resp[len - 2U] | (resp[len - 1U] << 8));//CRC低字节在前，CRC高字节在后
    return (crc_calc == crc_recv) ? 1U : 0U;
}

//解析 Modbus “读保持寄存器”响应帧，把响应里的寄存器数据提取到 regs[] 数组里，并返回解析出的寄存器数量
//addr + function_code + byte_count + data + crc
uint16_t Modbus_ParseReadRegistersResponse(uint8_t *resp, uint16_t len, uint16_t *regs)
{
    if (len < 5U || resp[1] != MODBUS_FC_READ_HOLDING_REGS) return 0U;//正常响应最短也要 5 字节
    uint8_t byte_count = resp[2];
    if (len < (uint16_t)(3U + byte_count + 2U)) return 0U;

    uint16_t reg_count = byte_count / 2U; //一个寄存器占 2 字节,寄存器数量 = 数据字节数 / 2
    for (uint16_t i = 0U; i < reg_count; i++) {
        regs[i] = (uint16_t)((resp[3U + i * 2U] << 8) | resp[4U + i * 2U]);//Modbus 寄存器数据是高字节在前，低字节在后
    }
    return reg_count;
}

/* addr(1) + fc(1) + byte_count(1) + data(reg_count*2) + crc(2) */
//根据要读取的寄存器数量，计算 Modbus RTU 正常响应帧应该有多少字节
uint16_t Modbus_GetExpectedResponseLength(uint16_t reg_count)
{
    return (uint16_t)(5U + reg_count * 2U);
}
