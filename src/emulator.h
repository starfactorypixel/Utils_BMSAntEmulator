#pragma once
#include "platform.h"
#include "battery_model.h"
#include "ant_packet.h"
#include "modes.h"

// Связывает модель батареи, режимы и протокол. Не зависит от железа.
class Emulator {
public:
    void begin(uint32_t now_ms);
    void setMode(uint8_t idx);
    void nextMode()    { setMode((_idx + 1) % MODE_COUNT); }
    void prevMode()    { setMode((_idx + MODE_COUNT - 1) % MODE_COUNT); }
    void restartMode() { setMode(_idx); }

    void update(uint32_t now_ms);                 // шаг физики (вызывать часто)
    bool replyEnabled() const { return !(_m.faults & FAULT_NO_REPLY); }
    void buildReply(uint8_t *buf, uint32_t now_ms); // 140 байт, с учётом неисправностей

    uint8_t         modeIndex() const { return _idx; }
    const ModeDesc &mode() const      { return _m; }
    float           modeTime() const  { return _t; }

    BatteryModel model;
    uint32_t     packets = 0;

private:
    ModeDesc _m;
    uint8_t  _idx = 0;
    float    _t = 0;
    uint32_t _last_ms = 0;
};
