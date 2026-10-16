// =====================================================================
//  Режимы эмуляции. Чтобы добавить режим:
//   1) (если нужно) написать функцию-профиль тока ниже;
//   2) добавить имя N_xxx и строку в таблицу MODES[].
//  Порядок в таблице = порядок переключения кнопкой.
// =====================================================================
#include "modes.h"

static float hash01(uint32_t x)      // детерминированное «случайное» 0..1
{
    x ^= x >> 16; x *= 0x7FEB352DUL; x ^= x >> 15; x *= 0x846CA68BUL; x ^= x >> 16;
    return (float)(x & 0xFFFF) / 65535.0f;
}

// ---------------------------- Профили тока ---------------------------

// Постоянный ток p1 (собственное потребление, стоянка)
static float prof_rest(const ModeDesc &m, float)
{
    return m.p1;
}

// p1 ± p2 синусом с периодом p3 секунд
static float prof_steady(const ModeDesc &m, float t)
{
    return m.p1 + m.p2 * sinf(2.0f * PI * t / m.p3);
}

// Городской цикл 90 с: разгон до ~p1, крейсер, рекуперация до −p2, стоянка с током p3.
// Каждый цикл немного отличается.
static float prof_drive(const ModeDesc &m, float t)
{
    const float period = 90.0f;
    uint32_t c = (uint32_t)(t / period);
    float x = fmodf(t, period);
    float peak   = m.p1 * (0.6f + 0.4f * hash01(c));
    float cruise = m.p1 * (0.15f + 0.15f * hash01(c * 7 + 3));
    if (x < 6.0f)  return peak * x / 6.0f;                                 // разгон
    if (x < 10.0f) return peak;
    if (x < 50.0f) return cruise + m.p1 * 0.05f * sinf(x * 0.7f);         // крейсер
    if (x < 58.0f) return -m.p2 * (1.0f - (x - 50.0f) / 8.0f);             // рекуперация
    return m.p3;                                                           // стоянка
}

// Зарядник с током CC = p1 (CV и окончание заряда — в модели)
static float prof_charge(const ModeDesc &m, float)
{
    return -m.p1;
}

// ---------------------------- Таблица режимов ------------------------
static const char N_FULL[]    PROGMEM = "Заряжена, покой";
static const char N_SMOOTH[]  PROGMEM = "Плавный разряд";
static const char N_DRIVE[]   PROGMEM = "Езда (город, рекуперация)";
static const char N_MID[]     PROGMEM = "Средне разряжена, покой";
static const char N_LOW[]     PROGMEM = "Сильно разряжена, под нагрузкой";
static const char N_CHARGE[]  PROGMEM = "Зарядка CC/CV";
static const char N_TOPOFF[]  PROGMEM = "Дозаряд + балансировка";
static const char N_IMBAL[]   PROGMEM = "Разбаланс, слабая ячейка #7";
static const char N_HOT[]     PROGMEM = "Перегрев под нагрузкой";
static const char N_NOREPLY[] PROGMEM = "Сбой: нет ответа";
static const char N_BADCRC[]  PROGMEM = "Сбой: битая контрольная сумма";

const ModeDesc MODES[] PROGMEM = {
//   name       profile       p1      p2     p3     soc spread weak amb speed faults
    { N_FULL,    prof_rest,    0.0f,   0.0f,  0.0f,  100,  3,    0,  22,  1,   FAULT_NONE     },
    { N_SMOOTH,  prof_steady,  18.0f,  3.0f,  40.0f,  95,  5,    0,  22,  20,  FAULT_NONE     },
    { N_DRIVE,   prof_drive,   140.0f, 35.0f, 0.4f,   80,  5,    0,  24,  1,   FAULT_NONE     },
    { N_MID,     prof_rest,    0.3f,   0.0f,  0.0f,   50,  5,    0,  20,  1,   FAULT_NONE     },
    { N_LOW,     prof_steady,  10.0f,  1.5f,  25.0f,   7,  8,    0,  18,  10,  FAULT_NONE     },
    { N_CHARGE,  prof_charge,  30.0f,  0.0f,  0.0f,   15,  8,    0,  22,  30,  FAULT_NONE     },
    { N_TOPOFF,  prof_charge,  20.0f,  0.0f,  0.0f,   92,  15,   0,  22,  10,  FAULT_NONE     },
    { N_IMBAL,   prof_steady,  25.0f,  5.0f,  60.0f,  60,  30,   7,  22,  10,  FAULT_NONE     },
    { N_HOT,     prof_steady,  180.0f, 20.0f, 30.0f,  90,  5,    0,  45,  5,   FAULT_NONE     },
    { N_NOREPLY, prof_rest,    0.0f,   0.0f,  0.0f,   -1,  0,    0,  22,  1,   FAULT_NO_REPLY },
    { N_BADCRC,  prof_steady,  12.0f,  2.0f,  20.0f,  -1,  0,    0,  22,  1,   FAULT_BAD_CRC  },
};

const uint8_t MODE_COUNT = sizeof(MODES) / sizeof(MODES[0]);
