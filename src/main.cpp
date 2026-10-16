// =====================================================================
//  Эмулятор ANT BMS (старый UART-протокол, 140 байт) на Arduino Nano
//
//  BMS-порт:  SoftwareSerial, D10 = RX (от опрашивающей платы), D11 = TX
//  Кнопка:    D2 -> GND. Короткое нажатие — следующий режим,
//                       долгое (>0.8 с) — перезапуск текущего режима.
//  Светодиод: D13 мигает номером режима (1..N).
//  USB Serial 115200: статус раз в секунду и команды (h — справка).
//
//  Все настройки — в config.h, режимы — в modes.cpp.
// =====================================================================
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "config.h"
#include "emulator.h"

static SoftwareSerial bmsPort(PIN_BMS_RX, PIN_BMS_TX);
static Emulator emu;
static uint8_t  pkt[ANT_PACKET_SIZE];
static uint8_t  rxw[6];                 // окно последних 6 принятых байт
static uint32_t reqOk = 0, reqBad = 0, ledT0 = 0;
static bool     hexDump = false;

// ------------------------------- лог ---------------------------------
#if CFG_DEBUG_BAUD
static void printMode()
{
    Serial.print(F("\r\n>>> Режим "));
    Serial.print(emu.modeIndex() + 1);
    Serial.print('/');
    Serial.print(MODE_COUNT);
    Serial.print(F(": "));
    Serial.println((const __FlashStringHelper *)emu.mode().name);
}

static void printHelp()
{
    Serial.println(F("Команды: n — след. режим, p — пред., r — перезапуск, x — hex-дамп пакетов вкл/выкл,"));
    Serial.println(F("         <число>+Enter — выбрать режим, l — список режимов, h — справка"));
}

static void printList()
{
    for (uint8_t i = 0; i < MODE_COUNT; ++i) {
        ModeDesc m;
        memcpy_P(&m, &MODES[i], sizeof(m));
        Serial.print(i + 1);
        Serial.print(F(". "));
        Serial.println((const __FlashStringHelper *)m.name);
    }
}

static void printStatus()
{
    const BatteryModel &b = emu.model;
    Serial.print('[');  Serial.print(emu.modeIndex() + 1);
    Serial.print(F("] t=")); Serial.print((uint32_t)emu.modeTime());
    Serial.print(F("s x")); Serial.print(emu.mode().speed);
    Serial.print(F(" | SOC ")); Serial.print(b.soc_pct);
    Serial.print(F("% (ист ")); Serial.print(b.socTrue() * 100.0f, 1);
    Serial.print(F("%) ")); Serial.print(b.pack_mv * 0.001f, 2);
    Serial.print(F("V ")); Serial.print(b.current_meas_a, 1);
    Serial.print(F("A")); if (b.cv_active) Serial.print(F(" CV")); if (b.charger_done) Serial.print(F(" FULL"));
    Serial.print(F(" | ")); Serial.print(b.min_mv); Serial.print(F(".."));
    Serial.print(b.max_mv); Serial.print(F("mV d")); Serial.print(b.max_mv - b.min_mv);
    Serial.print(F(" | Tmos ")); Serial.print(b.t_mos);
    Serial.print(F(" Tbal ")); Serial.print(b.t_bal);
    Serial.print(F(" Tc ")); Serial.print(b.t_cell[0]);
    Serial.print(F(" | CHG ")); Serial.print(b.chg_status);
    Serial.print(F(" DSG ")); Serial.print(b.dsg_status);
    Serial.print(F(" BAL ")); Serial.print(b.bal_status);
    Serial.print(F(" 0x")); Serial.print(b.bal_bits, HEX);
    Serial.print(F(" | req ")); Serial.print(reqOk);
    Serial.print('/'); Serial.println(reqBad);
}

static void dumpPacket()
{
    for (uint8_t i = 0; i < ANT_PACKET_SIZE; ++i) {
        if (pkt[i] < 0x10) Serial.print('0');
        Serial.print(pkt[i], HEX);
        Serial.print((i % 20 == 19) ? '\n' : ' ');
    }
}

static void pollConsole()
{
    static uint8_t num = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); continue; }
        if ((c == '\r' || c == '\n') && num) { emu.setMode(num - 1); num = 0; ledT0 = millis(); printMode(); continue; }
        num = 0;
        switch (c) {
            case 'n': emu.nextMode();    ledT0 = millis(); printMode(); break;
            case 'p': emu.prevMode();    ledT0 = millis(); printMode(); break;
            case 'r': emu.restartMode(); ledT0 = millis(); printMode(); break;
            case 'x': hexDump = !hexDump; break;
            case 'l': printList(); break;
            case 'h': printHelp(); break;
        }
    }
}
#else
static void printMode() {}
#endif

// ---------------------------- кнопка / LED ---------------------------
static void pollButton(uint32_t now)
{
    static bool raw = HIGH, stable = HIGH, longDone = false;
    static uint32_t tChange = 0, tDown = 0;
    bool r = digitalRead(PIN_BUTTON);
    if (r != raw) { raw = r; tChange = now; }
    if (now - tChange > 30 && raw != stable) {
        stable = raw;
        if (stable == LOW) { tDown = now; longDone = false; }
        else if (!longDone) { emu.nextMode(); ledT0 = now; printMode(); }
    }
    if (stable == LOW && !longDone && now - tDown >= CFG_LONG_PRESS_MS) {
        longDone = true;
        emu.restartMode(); ledT0 = now; printMode();
    }
}

static void ledTask(uint32_t now)
{
    uint32_t n = emu.modeIndex() + 1;
    uint32_t x = (now - ledT0) % (n * 400UL + 1500UL);
    digitalWrite(PIN_LED, (x < n * 400UL && (x % 400UL) < 150UL) ? HIGH : LOW);
}

// ------------------------------ протокол -----------------------------
static void onRxByte(uint8_t b)
{
    memmove(rxw, rxw + 1, 5);
    rxw[5] = b;
    bool isRead = (rxw[0] == 0x5A && rxw[1] == 0x5A);
#if CFG_ACCEPT_DBDB
    isRead = isRead || (rxw[0] == 0xDB && rxw[1] == 0xDB);
#endif
    if (!isRead) return;
    if ((uint8_t)(rxw[2] + rxw[3] + rxw[4]) != rxw[5]) { ++reqBad; return; }
    uint8_t addr = rxw[2];
    memset(rxw, 0, sizeof(rxw));
    if (addr != 0) return;              // чтение параметров/команды — не эмулируются
    ++reqOk;
    if (!emu.replyEnabled()) return;

    delay(CFG_REPLY_DELAY_MS);
    emu.buildReply(pkt, millis());
    bmsPort.write(pkt, ANT_PACKET_SIZE); // ~73 мс при 19200, приём на это время отключён
#if CFG_DEBUG_BAUD
    if (hexDump) dumpPacket();
#endif
}

// --------------------------------------------------------------------
void setup()
{
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_LED, OUTPUT);
#if CFG_DEBUG_BAUD
    Serial.begin(CFG_DEBUG_BAUD);
    Serial.println(F("\r\nANT BMS emulator"));
    Serial.print(CFG_CELL_COUNT); Serial.print(F("S, "));
    Serial.print(CFG_CAPACITY_AH, 0); Serial.print(F(" Ah, UART "));
    Serial.println(CFG_BMS_BAUD);
    printHelp();
    printList();
#endif
    bmsPort.begin(CFG_BMS_BAUD);
    emu.begin(millis());
    ledT0 = millis();
    printMode();
}

void loop()
{
    uint32_t now = millis();

    while (bmsPort.available()) onRxByte((uint8_t)bmsPort.read());

    emu.update(now);
    pollButton(now);
    ledTask(now);

#if CFG_DEBUG_BAUD
    pollConsole();
    static uint32_t tStat = 0;
    if (now - tStat >= CFG_STATUS_PERIOD_MS) { tStat = now; printStatus(); }
#endif
}
