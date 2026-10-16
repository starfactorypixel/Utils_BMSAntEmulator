#pragma once
#include "platform.h"

// Флаги неисправностей канала связи (для проверки драйвера)
enum : uint8_t {
    FAULT_NONE     = 0,
    FAULT_NO_REPLY = 1 << 0,   // BMS молчит
    FAULT_BAD_CRC  = 1 << 1,   // каждый 2-й пакет с испорченной контрольной суммой
};

struct ModeDesc;
// Профиль режима: желаемый ток в А (> 0 — разряд/нагрузка, < 0 — зарядник)
// t — модельное время с начала режима, с.
// Зарядник автоматически ограничивается CC/CV и отключается в конце заряда.
typedef float (*ProfileFn)(const ModeDesc &m, float t);

struct ModeDesc {
    const char *name;       // строка в PROGMEM
    ProfileFn   profile;
    float       p1, p2, p3; // параметры профиля (смысл — у каждой функции свой)
    int8_t      start_soc;  // SOC при входе в режим, %; -1 — продолжить с текущего состояния
    uint8_t     spread_x10; // разброс SOC ячеек при входе, десятые доли %
    uint8_t     weak_cell;  // номер «слабой» ячейки (меньше ёмкость, больше R), 0 — нет
    int8_t      ambient_c;  // температура окружающей среды
    uint8_t     speed;      // ускорение модельного времени (1 = реальное)
    uint8_t     faults;     // FAULT_*
};

extern const ModeDesc MODES[] PROGMEM;
extern const uint8_t  MODE_COUNT;
