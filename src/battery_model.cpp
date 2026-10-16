#include "battery_model.h"

// ------------------- Характеристики химии --------------------------------
// OCV(SOC) с шагом 10 %: 0, 10, … 100 %
#if CFG_CHEMISTRY == CHEM_NMC
static const float OCV_TAB[11] PROGMEM = {
    3.000f, 3.450f, 3.550f, 3.620f, 3.670f, 3.730f, 3.810f, 3.900f, 3.980f, 4.070f, 4.180f };
static const uint16_t CV_MV        = 4150;  // напряжение CV зарядника на ячейку
static const uint16_t OV_MV        = 4250;  // защита от перезаряда ячейки
static const uint16_t OV_REC_MV    = 4100;
static const uint16_t UV_MV        = 3000;  // защита от переразряда ячейки
static const uint16_t UV_REC_MV    = 3200;
static const uint16_t BAL_CHG_MV   = 3950;  // балансировка при заряде выше этого
static const uint16_t BAL_START_MV = 4050;  // балансировка «в покое» выше этого
#elif CFG_CHEMISTRY == CHEM_LFP
static const float OCV_TAB[11] PROGMEM = {
    2.500f, 3.100f, 3.200f, 3.240f, 3.260f, 3.280f, 3.290f, 3.300f, 3.320f, 3.340f, 3.450f };
static const uint16_t CV_MV        = 3550;
static const uint16_t OV_MV        = 3650;
static const uint16_t OV_REC_MV    = 3400;
static const uint16_t UV_MV        = 2600;
static const uint16_t UV_REC_MV    = 2900;
static const uint16_t BAL_CHG_MV   = 3350;
static const uint16_t BAL_START_MV = 3400;
#else
#error "Unknown CFG_CHEMISTRY"
#endif

static const float FULL_MAS = CFG_CAPACITY_AH * 3600000.0f;   // ёмкость, мА·с
static const float PROT_DELAY_S = 2.0f;                         // задержка срабатывания OV/UV

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float BatteryModel::rnd()
{
    _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5;
    return (float)(_rng & 0xFFFF) / 32767.5f - 1.0f;
}

float BatteryModel::ocv(float soc) const
{
    float tab[11];
    memcpy_P(tab, OCV_TAB, sizeof(tab));
    if (soc <= 0.0f) return tab[0] + soc * (tab[1] - tab[0]) * 10.0f;          // круто вниз
    if (soc >= 1.0f) return tab[10] + (soc - 1.0f) * (tab[10] - tab[9]) * 30.0f; // круто вверх
    float x = soc * 10.0f;
    uint8_t k = (uint8_t)x;
    return tab[k] + (tab[k + 1] - tab[k]) * (x - k);
}

uint32_t BatteryModel::capOf(uint8_t i) const
{
    return (i + 1 == _weak) ? (uint32_t)(_cap[i] * 0.85f) : _cap[i];
}

float BatteryModel::rOf(uint8_t i) const
{
    float kT = clampf(1.0f + 0.015f * (25.0f - _tc), 0.7f, 2.5f);   // холодно — сопротивление выше
    return _r[i] * ((i + 1 == _weak) ? 1.6f : 1.0f) * kT;
}

void BatteryModel::begin()
{
    _rng = CFG_RANDOM_SEED | 1UL;
    for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i) {
        _cap[i] = (uint32_t)(FULL_MAS * (1.0f + CFG_CELL_CAP_SPREAD * rnd()));
        _r[i]   = CFG_CELL_R_MOHM * 0.001f * (1.0f + CFG_CELL_R_SPREAD * rnd());
    }
    cap_full_uah = (uint32_t)(CFG_CAPACITY_AH * 1000000.0f);
    cycle_mah    = (uint32_t)(CFG_INITIAL_CYCLE_AH * 1000.0f);
    _tm = _tb = _tc = _ambient;
    reset(0.8f, 0.005f, 0);
}

void BatteryModel::reset(float soc, float spread, uint8_t weak_cell)
{
    _weak = weak_cell;
    for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i) {
        float s = clampf(soc + spread * rnd(), -0.02f, 1.02f);
        _q[i] = (int32_t)(capOf(i) * s);
    }
    _bms_q = clampf(soc, 0.0f, 1.0f) * FULL_MAS;
    _ip = 0; current_a = 0; current_meas_a = 0;
    _q_frac = _bal_frac = 0;
    _oc_chg_t = _oc_dsg_t = _ov_t = _uv_t = 0;
    charger_done = false; cv_active = false;
    chg_status = dsg_status = FET_ON;
    bal_status = BAL_OFF; bal_bits = 0;
    measure();
}

float BatteryModel::socTrue() const
{
    float s = 0;
    for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i) s += (float)_q[i] / capOf(i);
    return s / CFG_CELL_COUNT;
}

void BatteryModel::step(float dt, float dts, float i_req)
{
    if (dt <= 0.0f) return;

    // 1. Ключи BMS ограничивают ток
    if (i_req > 0 && dsg_status != FET_ON) i_req = 0;
    if (i_req < 0 && chg_status != FET_ON) i_req = 0;

    // 2. Зарядник CC/CV: держит напряжение сборки N*CV, заканчивает по току
    cv_active = false;
    if (i_req < 0) {
        if (charger_done) {
            i_req = 0;
        } else {
            float ocv_sum = 0, r_sum = 0;
            for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i) {
                ocv_sum += ocv((float)_q[i] / capOf(i));
                r_sum   += rOf(i);
            }
            float v_target = CFG_CELL_COUNT * (CV_MV * 0.001f);   // (на AVR int 16-бит!)
            float i_max = (v_target - ocv_sum + 0.6f * _ip * r_sum) / r_sum;
            if (i_max < 0) i_max = 0;
            if (-i_req >= i_max) { i_req = -i_max; cv_active = true; }
            if (cv_active && i_max < CFG_CHARGE_TERM_C * CFG_CAPACITY_AH) {
                charger_done = true;
                i_req = 0;
                _bms_q = FULL_MAS;          // BMS калибрует счётчик на 100 %
            }
        }
    }

    // 3. Ток не меняется мгновенно (инерция нагрузки/зарядника), реальное время
    current_a += (i_req - current_a) * (dt / (0.25f + dt));

    // 4. Заряд ячеек (модельное время) + балансировочный «слив»
    float dq = current_a * 1000.0f * dts + _q_frac;
    int32_t dqi = (int32_t)dq;
    _q_frac = dq - dqi;
    int32_t dbi = 0;
    if (bal_bits) {
        float db = CFG_BAL_CURRENT_A * 1000.0f * dts + _bal_frac;
        dbi = (int32_t)db;
        _bal_frac = db - dbi;
    }
    for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i) {
        int32_t q = _q[i] - dqi;
        if (bal_bits & (1UL << i)) q -= dbi;
        int32_t lo = -(int32_t)(capOf(i) / 20), hi = (int32_t)(capOf(i) + capOf(i) / 20);
        _q[i] = q < lo ? lo : (q > hi ? hi : q);
    }

    // 5. Поляризация (релаксация напряжения после снятия нагрузки), τ = 40 с
    _ip += (current_a - _ip) * (dts / (40.0f + dts));

    // 6. Тепло: ячейки медленно, ключи быстро, плата балансира греется при балансировке
    float i2 = current_a * current_a;
    _tc += (_ambient + i2 * 0.0008f - _tc) * (dts / (900.0f + dts));
    _tm += (_ambient + 3.0f + i2 * 0.003f - _tm) * (dts / (120.0f + dts));
    _tb += (_ambient + 2.0f + (bal_bits ? 10.0f : 0.0f) - _tb) * (dts / (200.0f + dts));

    // 7. Перегрузка по току — блокировка на CFG_OC_LATCH_S
    if (_oc_chg_t > 0) _oc_chg_t -= dt;
    if (_oc_dsg_t > 0) _oc_dsg_t -= dt;
    if ( current_a > CFG_DSG_OC_A) _oc_dsg_t = CFG_OC_LATCH_S;
    if (-current_a > CFG_CHG_OC_A) _oc_chg_t = CFG_OC_LATCH_S;

    // 8. Измерение тока BMS: шум датчика, «мёртвая зона» около нуля
    current_meas_a = current_a + 0.05f * rnd();
    if (fabsf(current_meas_a) < CFG_ZERO_DEADBAND_A) current_meas_a = 0;

    // 9. Кулоновский счётчик BMS и счётчик циклической ёмкости (по разряду)
    _bms_q = clampf(_bms_q - current_meas_a * 1000.0f * dts, 0.0f, FULL_MAS);
    if (current_meas_a > 0) {
        _cyc_frac += current_meas_a * 1000.0f * dts;
        while (_cyc_frac >= 3600.0f) { _cyc_frac -= 3600.0f; ++cycle_mah; }  // 3600 мА·с = 1 мА·ч
    }

    measure();

    // задержка срабатывания защит по напряжению — как у настоящей BMS
    _ov_t = (max_mv >= OV_MV) ? _ov_t + dt : 0;
    _uv_t = (min_mv <= UV_MV) ? _uv_t + dt : 0;
    protections();
}

void BatteryModel::measure()
{
    uint32_t sum = 0;
    max_mv = 0; min_mv = 0xFFFF; max_idx = min_idx = 1;
    for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i) {
        float soc = (float)_q[i] / capOf(i);
        float v = ocv(soc) - (current_a + 0.6f * _ip) * rOf(i);
        int32_t mv = (int32_t)lroundf(v * 1000.0f) + (int32_t)(rnd() * 1.5f);  // шум АЦП ±1 мВ
        if (mv < 0) mv = 0;
        cell_mv[i] = (uint16_t)mv;
        sum += (uint32_t)mv;
        if (cell_mv[i] > max_mv) { max_mv = cell_mv[i]; max_idx = i + 1; }
        if (cell_mv[i] < min_mv) { min_mv = cell_mv[i]; min_idx = i + 1; }
    }
    pack_mv = sum;
    avg_mv  = (uint16_t)(sum / CFG_CELL_COUNT);

    static const float TOFS[4] = { -0.4f, 0.3f, 0.9f, -0.1f };   // датчики стоят в разных местах
    t_mos = (int16_t)lroundf(_tm);
    t_bal = (int16_t)lroundf(_tb);
    for (uint8_t k = 0; k < 4; ++k) t_cell[k] = (int16_t)lroundf(_tc + TOFS[k]);

    cap_rem_uah = (uint32_t)(_bms_q / 3.6f);
    soc_pct = (uint8_t)lroundf(clampf(_bms_q * 100.0f / FULL_MAS, 0.0f, 100.0f));
}

void BatteryModel::protections()
{
    int16_t tc = t_cell[0];
    for (uint8_t k = 1; k < 4; ++k) if (t_cell[k] > tc) tc = t_cell[k];

    // --- ключ заряда ---
    uint8_t c = FET_ON;
    if (_oc_chg_t > 0)                                                            c = FET_OVER_CURRENT;
    else if (t_mos >= CFG_TEMP_MOS_PROT || (chg_status == FET_MOS_TEMP_HIGH && t_mos > CFG_TEMP_MOS_REC)) c = FET_MOS_TEMP_HIGH;
    else if (tc >= CFG_TEMP_CHG_PROT || (chg_status == FET_TEMP_HIGH && tc > CFG_TEMP_CHG_REC))           c = FET_TEMP_HIGH;
    else if (_ov_t >= PROT_DELAY_S || (chg_status == FET_CELL_VOLT && max_mv > OV_REC_MV))                c = FET_CELL_VOLT;
    chg_status = c;

    // --- ключ разряда ---
    uint8_t d = FET_ON;
    if (_oc_dsg_t > 0)                                                            d = FET_OVER_CURRENT;
    else if (t_mos >= CFG_TEMP_MOS_PROT || (dsg_status == FET_MOS_TEMP_HIGH && t_mos > CFG_TEMP_MOS_REC)) d = FET_MOS_TEMP_HIGH;
    else if (tc >= CFG_TEMP_DSG_PROT || (dsg_status == FET_TEMP_HIGH && tc > CFG_TEMP_DSG_REC))           d = FET_TEMP_HIGH;
    else if (_uv_t >= PROT_DELAY_S || (dsg_status == FET_CELL_VOLT && min_mv < UV_REC_MV))                d = FET_CELL_VOLT;
    dsg_status = d;

    // --- балансир: «сливает» ячейки выше минимума, с гистерезисом ---
    bool charging = current_a < -0.5f;
    uint16_t diff = max_mv - min_mv;
    uint16_t need = (bal_status != BAL_OFF) ? CFG_BAL_DIFF_MV / 2 : CFG_BAL_DIFF_MV;
    bal_bits = 0;
    bal_status = BAL_OFF;
    if (diff > need && (charging ? max_mv >= BAL_CHG_MV : max_mv >= BAL_START_MV) && t_bal < 70) {
        uint16_t thr = min_mv + CFG_BAL_DIFF_MV / 2;
        for (uint8_t i = 0; i < CFG_CELL_COUNT; ++i)
            if (cell_mv[i] > thr) bal_bits |= (1UL << i);
        if (bal_bits) bal_status = charging ? BAL_DIFF : BAL_AUTO;
    }
}
