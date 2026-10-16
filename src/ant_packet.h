#pragma once
#include "platform.h"
#include "battery_model.h"

// Старый UART-протокол ANT BMS: запрос 5A 5A 00 00 00 00 -> ответ 140 байт, big-endian.
#define ANT_PACKET_SIZE 140

// Смещения полей (по klotztech/VBMS wiki и syssi/esphome-ant-bms)
enum AntOffset : uint8_t {
    ANT_HEADER        = 0,    // AA 55 AA FF
    ANT_TOTAL_VOLTAGE = 4,    // u16, 0.1 V
    ANT_CELL_VOLTAGE  = 6,    // u16 x32, 0.001 V
    ANT_CURRENT       = 70,   // i32, 0.1 A
    ANT_SOC           = 74,   // u8, %
    ANT_CAP_PHY       = 75,   // u32, 0.000001 Ah
    ANT_CAP_REM       = 79,   // u32, 0.000001 Ah
    ANT_CAP_CYCLE     = 83,   // u32, 0.001 Ah
    ANT_UPTIME        = 87,   // u32, s
    ANT_TEMPS         = 91,   // i16 x6, °C
    ANT_CHG_FET       = 103,
    ANT_DSG_FET       = 104,
    ANT_BALANCER      = 105,
    ANT_TIRE_LENGTH   = 106,  // u16
    ANT_PULSES        = 108,  // u16
    ANT_RELAY         = 110,  // u8
    ANT_POWER         = 111,  // i32, W
    ANT_VMAX_NUM      = 115,
    ANT_VMAX          = 116,
    ANT_VMIN_NUM      = 118,
    ANT_VMIN          = 119,
    ANT_VAVG          = 121,
    ANT_CELL_COUNT    = 123,
    ANT_DSG_FET_VDS   = 124,  // u16, 0.1 V
    ANT_DSG_FET_DRIVE = 126,  // u16, 0.1 V
    ANT_CHG_FET_DRIVE = 128,  // u16, 0.1 V
    ANT_ZERO_CURRENT  = 130,  // u16, начальное значение компаратора тока
    ANT_BALANCE_BITS  = 132,  // u32, бит0 = ячейка 1
    ANT_SYSTEM_LOG    = 136,  // u16
    ANT_CHECKSUM      = 138   // u16, сумма байт 4..137
};

void     antBuildPacket(const BatteryModel &b, uint32_t uptime_s, uint8_t *p);
uint16_t antChecksum(const uint8_t *p);
