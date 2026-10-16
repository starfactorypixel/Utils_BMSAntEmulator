#pragma once
// =====================================================================
//  Эмулятор ANT BMS (старый протокол, 140 байт, UART) — настройки
// =====================================================================

#define CHEM_NMC 1   // Li-ion NMC/NCA: 3.0 … 4.2 В
#define CHEM_LFP 2   // LiFePO4:        2.5 … 3.65 В

// ----------------------------- Батарея --------------------------------
#define CFG_CELL_COUNT          20          // ячеек последовательно, 1..32
#define CFG_CAPACITY_AH         105.0f      // ёмкость, А·ч (до ~500)
#define CFG_CHEMISTRY           CHEM_NMC    // CHEM_NMC или CHEM_LFP
#define CFG_CELL_R_MOHM         1.2f        // сопротивление ячейки + перемычки, мОм
#define CFG_CELL_CAP_SPREAD     0.015f      // разброс ёмкости ячеек, ±доля
#define CFG_CELL_R_SPREAD       0.15f       // разброс сопротивления, ±доля
#define CFG_INITIAL_CYCLE_AH    3150.0f     // стартовое значение «циклической ёмкости»
#define CFG_INITIAL_UPTIME_S    1036800UL   // BMS «уже работает» 12 суток
#define CFG_RANDOM_SEED         20261016UL  // фиксированный: после сброса та же «батарея»

// --------------------- Уставки BMS (защиты, балансир) -----------------
// Пороговые напряжения ячеек зависят от химии и заданы в battery_model.cpp
#define CFG_CHG_OC_A            80.0f       // защита по току заряда, А
#define CFG_DSG_OC_A            250.0f      // защита по току разряда, А
#define CFG_OC_LATCH_S          10.0f       // время блокировки после перегрузки, с
#define CFG_TEMP_MOS_PROT       90          // перегрев силовых ключей, °C
#define CFG_TEMP_MOS_REC        70
#define CFG_TEMP_DSG_PROT       60          // перегрев ячеек — запрет разряда
#define CFG_TEMP_DSG_REC        50
#define CFG_TEMP_CHG_PROT       50          // перегрев ячеек — запрет заряда
#define CFG_TEMP_CHG_REC        45
#define CFG_BAL_DIFF_MV         10          // порог разбаланса для балансировки, мВ
#define CFG_BAL_CURRENT_A       0.5f        // ток балансировки, А
#define CFG_CHARGE_TERM_C       0.02f       // окончание заряда (CV), доля C
#define CFG_ZERO_DEADBAND_A     0.15f       // ниже этого BMS показывает 0.0 А

// ----------------------------- Протокол -------------------------------
#define CFG_BMS_BAUD            19200       // 19200 у большинства плат ANT (в PDF ANT — 9600)
#define CFG_DISCHARGE_NEGATIVE  0           // 1: ток/мощность разряда < 0, заряда > 0
                                            // 0: наоборот. Сверьте с реальной BMS!
#define CFG_ACCEPT_DBDB         1           // отвечать и на запрос DB DB 00 00 00 00 (BLE-вариант)
#define CFG_REPLY_DELAY_MS      3           // задержка ответа после запроса
#define CFG_TIRE_LENGTH_MM      1000        // поля 106..110 BMS заполняет константами
#define CFG_PULSES_PER_REV      23
#define CFG_RELAY_STATE         1

// ----------------------------- Железо ---------------------------------
#define PIN_BMS_RX              6           // сюда TX опрашивающей платы
#define PIN_BMS_TX              5           // отсюда — в RX опрашивающей платы (5 В! см. README)
#define PIN_BUTTON              3           // кнопка на GND, внутренняя подтяжка
#define PIN_LED                 13          // мигает номером режима
#define CFG_LONG_PRESS_MS       800         // долгое нажатие = перезапуск текущего режима
#define CFG_DEBUG_BAUD          115200      // лог/команды по USB (0 = выключить)
#define CFG_STATUS_PERIOD_MS    1000

#if CFG_CELL_COUNT < 1 || CFG_CELL_COUNT > 32
#error "CFG_CELL_COUNT must be 1..32"
#endif
