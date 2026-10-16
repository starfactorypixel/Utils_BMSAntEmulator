#include "emulator.h"

void Emulator::begin(uint32_t now_ms)
{
    _last_ms = now_ms;
    model.begin();
    setMode(0);
}

void Emulator::setMode(uint8_t idx)
{
    if (idx >= MODE_COUNT) idx = 0;
    _idx = idx;
    memcpy_P(&_m, &MODES[idx], sizeof(ModeDesc));
    _t = 0;
    model.setAmbient(_m.ambient_c);
    model.resetCharger();
    if (_m.start_soc >= 0)
        model.reset(_m.start_soc / 100.0f, _m.spread_x10 / 1000.0f, _m.weak_cell);
}

void Emulator::update(uint32_t now_ms)
{
    float dt = (now_ms - _last_ms) * 0.001f;
    if (dt < 0.05f) return;                 // шаг ~50…100 мс
    _last_ms = now_ms;
    if (dt > 0.5f) dt = 0.5f;               // после долгой передачи не «прыгаем»
    float dts = dt * _m.speed;
    model.step(dt, dts, _m.profile(_m, _t));
    _t += dts;
}

void Emulator::buildReply(uint8_t *buf, uint32_t now_ms)
{
    antBuildPacket(model, CFG_INITIAL_UPTIME_S + now_ms / 1000UL, buf);
    ++packets;
    if ((_m.faults & FAULT_BAD_CRC) && (packets & 1))
        buf[ANT_CHECKSUM + 1] ^= 0x5A;
}
