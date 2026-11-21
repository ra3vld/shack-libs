// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tuner_types.h"
#include <cstddef>
#include <cstdint>
namespace ldg {
enum class Result : uint8_t {
    Pending,
    Success,
    Failed,
    Timeout,
    Cancelled,
    TargetChanged,
    Expired,
    Busy,
    Invalid,
    Unavailable,
    Unsafe,
    Unknown,
    DuplicateConflict
};
const char *resultName(Result);
constexpr uint8_t Probe = 0x84;
struct Wire {
    void *context{};
    bool (*write)(void *, uint8_t){};
    bool (*authorize)(void *, uint8_t){};
};
class Protocol {
  public:
    explicit Protocol(Wire wire) : wire_(wire) {}
    bool start(uint8_t command, uint32_t now);
    void tick(uint32_t now);
    void feed(const uint8_t *, size_t, uint32_t now);
    void fault();
    void voltage(float v) { voltage_ = v; }
    bool busy() const { return phase_ != Phase::Idle; }
    bool takeResult(Result &r) {
        if (!complete_)
            return false;
        complete_ = false;
        r = result_;
        return true;
    }
    tuner_state_t state(uint32_t now) const;
    uint32_t frames() const { return frames_; }
    uint32_t errors() const { return errors_; }
    uint8_t lastAck() const { return lastAck_; }
    bool lateAck() const { return late_; }
    tuner_ant_t probeRestore() const {
        return wanted_ == Probe && toggles_ == 1 ? target_ : ANT_UNKNOWN;
    }

  private:
    enum class Phase : uint8_t {
        Idle,
        Wake1,
        Wake2,
        Send,
        Settle,
        Await,
        Guard,
        Quarantine,
        Refresh
    };
    Wire wire_;
    Phase phase_ = Phase::Idle;
    uint32_t at_ = 0, meterAt_ = 0, frames_ = 0, errors_ = 0, restartAt_ = 0;
    uint8_t wanted_ = 0, exchange_ = 0, resume_ = 0, sync_ = 0, toggles_ = 0, lastAck_ = 0;
    uint8_t packet_[8]{};
    size_t used_ = 0;
    tuner_state_t state_{};
    tuner_ant_t target_ = ANT_UNKNOWN;
    Result result_ = Result::Pending;
    bool needsSync_ = false, complete_ = false, late_ = false, seen_ = false, restoring_ = false,
         refresh_ = false, faultPending_ = false;
    uint32_t refreshFrame_ = 0;
    float voltage_ = 13.8f;
    void wake(uint8_t, uint32_t);
    void acknowledged(uint8_t, uint32_t);
    void advance(uint32_t);
    void restore(uint32_t);
    void meter(uint32_t);
    bool validAck(uint8_t) const;
};
} // namespace ldg
