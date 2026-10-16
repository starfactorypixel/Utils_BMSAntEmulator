#include "ant_packet.h"

static inline void be16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static inline void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

uint16_t antChecksum(const uint8_t *p)
{
    uint16_t s = 0;
    for (uint8_t i = 4; i < ANT_CHECKSUM; ++i) s += p[i];
    return s;
}

void antBuildPacket(const BatteryModel &b, uint32_t uptime_s, uint8_t *p)
{
    memset(p, 0, ANT_PACKET_SIZE);
    p[0] = 0xAA; p[1] = 0x55; p[2] = 0xAA; p[3] = 0xFF;

    be16(p + ANT_TOTAL_VOLTAGE, (uint16_t)((b.pack_mv + 50) / 100));
    for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i)              // остальные до 32 — нули
        be16(p + ANT_CELL_VOLTAGE + 2 * i, b.cell_mv[i]);

#if CFG_DISCHARGE_NEGATIVE
    const float sign = -1.0f;
#else
    const float sign = 1.0f;
#endif
    float i_a = b.current_meas_a * sign;
    be32(p + ANT_CURRENT, (uint32_t)(int32_t)lroundf(i_a * 10.0f));

    p[ANT_SOC] = b.soc_pct;
    be32(p + ANT_CAP_PHY,   b.cap_full_uah);
    be32(p + ANT_CAP_REM,   b.cap_rem_uah);
    be32(p + ANT_CAP_CYCLE, b.cycle_mah);
    be32(p + ANT_UPTIME,    uptime_s);

    // Порядок датчиков в документации не расписан; здесь — как в BMSAnt_Data.h:
    // MOSFET, балансир, T1..T4
    be16(p + ANT_TEMPS + 0, (uint16_t)b.t_mos);
    be16(p + ANT_TEMPS + 2, (uint16_t)b.t_bal);
    for (uint8_t k = 0; k < 4; ++k) be16(p + ANT_TEMPS + 4 + 2 * k, (uint16_t)b.t_cell[k]);

    p[ANT_CHG_FET]  = b.chg_status;
    p[ANT_DSG_FET]  = b.dsg_status;
    p[ANT_BALANCER] = b.bal_status;
    be16(p + ANT_TIRE_LENGTH, CFG_TIRE_LENGTH_MM);
    be16(p + ANT_PULSES,      CFG_PULSES_PER_REV);
    p[ANT_RELAY] = CFG_RELAY_STATE;

    float watts = (b.pack_mv * 0.001f) * i_a;
    be32(p + ANT_POWER, (uint32_t)(int32_t)lroundf(watts));

    p[ANT_VMAX_NUM] = b.max_idx;  be16(p + ANT_VMAX, b.max_mv);
    p[ANT_VMIN_NUM] = b.min_idx;  be16(p + ANT_VMIN, b.min_mv);
    be16(p + ANT_VAVG, b.avg_mv);
    p[ANT_CELL_COUNT] = CFG_CELL_COUNT;

    // Служебные поля — правдоподобные значения
    bool dsg_on = b.dsg_status == FET_ON, chg_on = b.chg_status == FET_ON;
    be16(p + ANT_DSG_FET_VDS,   (uint16_t)lroundf(fabsf(b.current_a) * 0.0008f * 10.0f)); // ~0.8 мОм ключей
    be16(p + ANT_DSG_FET_DRIVE, dsg_on ? 115 + (b.pack_mv & 3) : 0);                    // ≈11.5 В
    be16(p + ANT_CHG_FET_DRIVE, chg_on ? 111 + (b.pack_mv & 1) : 0);
    be16(p + ANT_ZERO_CURRENT,  679);
    be32(p + ANT_BALANCE_BITS,  b.bal_bits);
    be16(p + ANT_SYSTEM_LOG,    0);

    be16(p + ANT_CHECKSUM, antChecksum(p));
}
