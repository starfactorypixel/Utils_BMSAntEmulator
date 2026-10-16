#pragma once
#include "platform.h"
#include "config.h"

// Статусы ключей/балансира — значения как в протоколе ANT
enum : uint8_t {
    FET_OFF = 0, FET_ON = 1, FET_CELL_VOLT = 2, FET_OVER_CURRENT = 3,
    FET_TEMP_HIGH = 6, FET_MOS_TEMP_HIGH = 7
};
enum : uint8_t { BAL_OFF = 0, BAL_DIFF = 2, BAL_AUTO = 4 };

// Простая, но «живая» модель последовательной сборки:
//  - у каждой ячейки своя ёмкость и сопротивление (разброс), опц. «слабая» ячейка;
//  - напряжение = OCV(SOC) − I·R(T) − поляризация (RC-цепочка, даёт релаксацию после нагрузки);
//  - зарядник CC/CV по напряжению сборки с окончанием по току;
//  - тепловая модель ключей, платы балансира и ячеек;
//  - кулоновский счётчик BMS отдельно от «истинного» заряда ячеек (калибруется на 100 %);
//  - защиты с гистерезисом и пассивная балансировка.
class BatteryModel {
public:
    void begin();
    // soc 0..1; spread — разброс SOC ячеек (±доля); weak_cell 1..N или 0
    void reset(float soc, float spread, uint8_t weak_cell);
    void setAmbient(float c) { _ambient = c; }
    void resetCharger() { charger_done = false; }
    // dt_real — реальное время шага, dt_sim — модельное (с ускорением), i_req — запрос нагрузки/зарядника, А (+разряд)
    void step(float dt_real, float dt_sim, float i_req);

    float socTrue() const;   // средний истинный SOC ячеек 0..1

    // ---------- то, что «видит» BMS ----------
    uint16_t cell_mv[CFG_CELL_COUNT];
    uint16_t max_mv, min_mv, avg_mv;
    uint8_t  max_idx, min_idx;          // 1..N
    uint32_t pack_mv;
    float    current_a;                 // фактический ток, + разряд
    float    current_meas_a;            // измеренный BMS
    int16_t  t_mos, t_bal, t_cell[4];
    uint8_t  chg_status, dsg_status, bal_status;
    uint32_t bal_bits;
    uint32_t cap_full_uah, cap_rem_uah, cycle_mah;
    uint8_t  soc_pct;
    bool     charger_done, cv_active;

private:
    float    ocv(float soc) const;
    uint32_t capOf(uint8_t i) const;
    float    rOf(uint8_t i) const;      // Ом, с учётом температуры
    float    rnd();                     // −1..1
    void     measure();
    void     protections();

    int32_t  _q[CFG_CELL_COUNT];        // заряд ячейки, мА·с
    uint32_t _cap[CFG_CELL_COUNT];      // ёмкость ячейки, мА·с
    float    _r[CFG_CELL_COUNT];        // сопротивление, Ом
    uint8_t  _weak = 0;
    float    _q_frac = 0, _bal_frac = 0, _cyc_frac = 0;
    float    _bms_q = 0;                // счётчик BMS, мА·с
    float    _ip = 0;                   // ток поляризации, А
    float    _ambient = 22, _tm = 22, _tb = 22, _tc = 22;
    float    _oc_chg_t = 0, _oc_dsg_t = 0, _ov_t = 0, _uv_t = 0;
    uint32_t _rng = 1;
};
