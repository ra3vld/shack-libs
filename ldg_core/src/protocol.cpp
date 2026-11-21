// SPDX-License-Identifier: GPL-3.0-or-later
#include "protocol.hpp"
#include <cmath>
#include <cstring>
namespace ldg {
const char *resultName(Result r) {
    static constexpr const char *names[] = {"pending",           "success",   "failed",
                                            "timeout",           "cancelled", "target_changed",
                                            "expired",           "busy",      "invalid_request",
                                            "unavailable",       "unsafe",    "unknown",
                                            "duplicate_conflict"};
    return names[static_cast<unsigned>(r)];
}
static tuner_band_t decode(uint16_t n) {
    static constexpr uint16_t ranges[][2] = {{8230, 9145}, {4110, 4710}, {3060, 3080}, {2250, 2355},
                                             {1600, 1630}, {1140, 1180}, {900, 915},   {766, 782},
                                             {655, 662},   {550, 590},   {320, 360}};
    for (unsigned i = 0; i < 11; ++i)
        if (n >= ranges[i][0] && n <= ranges[i][1])
            return static_cast<tuner_band_t>(i + 1);
    return BAND_UNKNOWN;
}
void Protocol::wake(uint8_t cmd, uint32_t now) {
    exchange_ = cmd;
    sync_ = 0;
    phase_ = Phase::Wake1;
    at_ = now;
}
bool Protocol::start(uint8_t cmd, uint32_t now) {
    if (busy() || complete_)
        return false;
    switch (cmd) {
    case 'A':
    case 'T':
    case 'F':
    case 'P':
    case 'C':
    case 'M':
    case 'Z':
    case 'S':
    case TUNER_CMD_SELECT_ANT_A:
    case TUNER_CMD_SELECT_ANT_B:
    case TUNER_CMD_CONTROLLED_MODE:
    case Probe:
        break;
    default:
        return false;
    }
    wanted_ = cmd;
    result_ = Result::Pending;
    late_ = false;
    toggles_ = 0;
    restoring_ = false;
    refresh_ = false;
    resume_ = 0;
    if (cmd == 'Z')
        needsSync_ = true;
    target_ = cmd == TUNER_CMD_SELECT_ANT_A   ? ANT_A
              : cmd == TUNER_CMD_SELECT_ANT_B ? ANT_B
                                              : ANT_UNKNOWN;
    if (target_ != ANT_UNKNOWN && state_.antenna == target_ && !needsSync_ && seen_ &&
        now - meterAt_ < 3000) {
        result_ = Result::Success;
        complete_ = true;
        return true;
    }
    if (cmd == 'S') {
        restoring_ = true;
        result_ = Result::Success;
        wake('S', now);
    } else
        wake('X', now);
    return true;
}
void Protocol::restore(uint32_t now) {
    restoring_ = true;
    wake('S', now);
}
void Protocol::advance(uint32_t now) {
    if (result_ != Result::Pending) {
        restore(now);
        return;
    }
    if (refresh_) {
        refresh_ = false;
        resume_ = 1;
        wake('S', now);
        return;
    }
    if (needsSync_) {
        wake('Z', now);
        return;
    }
    if (wanted_ == 'Z') {
        result_ = Result::Success;
        restore(now);
        return;
    }
    uint8_t cmd = wanted_;
    if (cmd == TUNER_CMD_CONTROLLED_MODE)
        cmd = 'M';
    if (target_ != ANT_UNKNOWN || cmd == Probe)
        cmd = 'A';
    wake(cmd, now);
}
void Protocol::tick(uint32_t now) {
    if (faultPending_) {
        faultPending_ = false;
        if (busy()) {
            result_ = Result::Failed;
            phase_ = Phase::Quarantine;
            at_ = now + 250;
        }
    }
    if (phase_ == Phase::Refresh && frames_ != refreshFrame_) {
        resume_ = 0;
        wake('X', now);
    }
    if (phase_ == Phase::Idle) {
        if (!complete_ && (!seen_ || now - meterAt_ > 3000) && now - restartAt_ >= 1000) {
            restartAt_ = now;
            start('S', now);
        }
        return;
    }
    if (static_cast<int32_t>(now - at_) < 0)
        return;
    auto send = [&](uint8_t b) {
        if (wire_.write && wire_.write(wire_.context, b))
            return true;
        ++errors_;
        result_ = Result::Failed;
        needsSync_ = true;
        phase_ = Phase::Idle;
        complete_ = true;
        return false;
    };
    switch (phase_) {
    case Phase::Wake1:
        if (send(' ')) {
            phase_ = Phase::Wake2;
            at_ = now + 1;
        }
        break;
    case Phase::Wake2:
        if (send(' ')) {
            phase_ = Phase::Send;
            at_ = now + 1;
        }
        break;
    case Phase::Send:
        if (exchange_ != 'X' && exchange_ != 'S' && exchange_ != 'Z' && wire_.authorize &&
            !wire_.authorize(wire_.context, exchange_)) {
            result_ = Result::Unsafe;
            restore(now);
            break;
        }
        if (!send(exchange_))
            break;
        if (exchange_ == 'A') {
            state_.antenna = ANT_UNKNOWN;
            ++toggles_;
        }
        if (exchange_ == 'X') {
            phase_ = Phase::Settle;
            at_ = now + 250;
            used_ = 0;
        } else if (exchange_ == 'S') {
            used_ = 0;
            restartAt_ = now;
            if (resume_) {
                phase_ = Phase::Refresh;
                refreshFrame_ = frames_;
                at_ = now + 1500;
            } else {
                phase_ = Phase::Idle;
                complete_ = true;
            }
        } else {
            phase_ = Phase::Await;
            at_ = now + ((exchange_ == 'T' || exchange_ == 'F') ? 20000 : 2200);
        }
        break;
    case Phase::Settle:
        advance(now);
        break;
    case Phase::Await:
        result_ = Result::Timeout;
        needsSync_ = true;
        ++errors_;
        phase_ = Phase::Quarantine;
        at_ = now + 250;
        break;
    case Phase::Quarantine:
        restore(now);
        break;
    case Phase::Guard:
        advance(now);
        break;
    case Phase::Refresh:
        resume_ = 0;
        result_ = Result::Timeout;
        phase_ = Phase::Idle;
        complete_ = true;
        break;
    default:
        break;
    }
}
bool Protocol::validAck(uint8_t b) const {
    switch (exchange_) {
    case 'A':
        return b == '1' || b == '2';
    case 'T':
    case 'F':
        return b == 'T' || b == 'M' || b == 'F';
    case 'P':
        return b == 'P';
    case 'C':
        return b == 'A';
    case 'M':
        return b == 'M';
    default:
        return false;
    }
}
void Protocol::acknowledged(uint8_t b, uint32_t now) {
    lastAck_ = b;
    if (exchange_ == 'A') {
        state_.antenna = b == '1' ? ANT_A : ANT_B;
        state_.lastAntenna = state_.antenna;
    }
    if (phase_ == Phase::Quarantine) {
        late_ = true;
        phase_ = Phase::Guard;
        at_ = now + 250;
        return;
    }
    if (exchange_ == 'Z') {
        needsSync_ = false;
        if (wanted_ == 'Z')
            result_ = Result::Success;
    } else if (exchange_ == 'A') {
        if (wanted_ == Probe && toggles_ == 1)
            target_ = state_.antenna == ANT_A ? ANT_B : ANT_A;
        if ((wanted_ == Probe && toggles_ == 1) ||
            (target_ != ANT_UNKNOWN && state_.antenna != target_ && toggles_ < 2)) {
            refresh_ = true;
        } else
            result_ = (target_ == ANT_UNKNOWN || target_ == state_.antenna) ? Result::Success
                                                                            : Result::Failed;
    } else {
        if (exchange_ == 'C')
            state_.mode = MODE_AUTO;
        if (exchange_ == 'M')
            state_.mode = MODE_MANUAL;
        if (exchange_ == 'P')
            state_.mode = MODE_BYPASS;
        result_ = b == 'F' ? Result::Failed : Result::Success;
    }
    phase_ = Phase::Guard;
    at_ = now + 250;
}
void Protocol::feed(const uint8_t *bytes, size_t size, uint32_t now) {
    for (size_t i = 0; i < size; ++i) {
        const uint8_t b = bytes[i];
        if (phase_ == Phase::Await || phase_ == Phase::Quarantine) {
            if (exchange_ == 'Z') {
                static constexpr char pattern[] = "000000000000000AzAz";
                if (b == static_cast<uint8_t>(pattern[sync_]))
                    ++sync_;
                else
                    sync_ = b == '0' ? (sync_ >= 15 ? 15 : 1) : 0;
                if (sync_ == sizeof(pattern) - 1) {
                    acknowledged('Z', now);
                    sync_ = 0;
                }
            } else if (validAck(b))
                acknowledged(b, now);
        } else if (phase_ == Phase::Idle || phase_ == Phase::Refresh) {
            packet_[used_++] = b;
            if (used_ == 8) {
                if (packet_[6] == ';' && packet_[7] == ';') {
                    meter(now);
                    used_ = 0;
                } else {
                    memmove(packet_, packet_ + 1, 7);
                    used_ = 7;
                }
            }
        }
    }
}
void Protocol::meter(uint32_t now) {
    auto &m = state_.meter;
    auto u16 = [&](int i) { return static_cast<uint16_t>((packet_[i] << 8) | packet_[i + 1]); };
    m.forward_power_raw = u16(0);
    m.reflected_power_raw = u16(2);
    m.band_indicator = u16(4);
    m.band = decode(m.band_indicator);
    auto power = [&](uint16_t raw) {
        const double v = 1000.0 * voltage_ * raw / (65536.0 * 0.707);
        return v * v / 50.0;
    };
    m.forward_power_watts = power(m.forward_power_raw);
    m.reflected_power_watts = power(m.reflected_power_raw);
    m.swr = m.forward_power_raw > m.reflected_power_raw
                ? double(m.forward_power_raw + m.reflected_power_raw) /
                      (m.forward_power_raw - m.reflected_power_raw)
                : NAN;
    if (knownBand(m.band))
        state_.lastBand = m.band;
    seen_ = true;
    meterAt_ = now;
    ++frames_;
}
void Protocol::fault() {
    ++errors_;
    used_ = 0;
    seen_ = false;
    needsSync_ = true;
    faultPending_ = true;
}
tuner_state_t Protocol::state(uint32_t now) const {
    auto s = state_;
    s.meterAgeMs = seen_ ? now - meterAt_ : UINT32_MAX;
    s.meterValid = seen_ && s.meterAgeMs <= 3000 && !busy();
    return s;
}
} // namespace ldg
