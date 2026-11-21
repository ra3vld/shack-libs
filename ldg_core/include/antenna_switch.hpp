// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "protocol.hpp"
#include "radio_band.h"
namespace ldg {
enum class Confirmation : uint8_t { Unknown, Sent, Acknowledged, Physical };
struct SwitchState {
    uint8_t requested = 0, confirmed = 0;
    Confirmation evidence = Confirmation::Unknown;
    bool busy = false;
    Result result = Result::Unavailable;
};
class AntennaSwitch {
  public:
    virtual ~AntennaSwitch() = default;
    virtual uint8_t ports() const = 0;
    virtual bool select(uint8_t, uint32_t) = 0;
    virtual void tick(uint32_t) = 0;
    virtual SwitchState state() const = 0;
};
class SimulatedSwitch final : public AntennaSwitch {
    SwitchState state_{};
    uint32_t started_ = 0;
    bool fail_ = false;

  public:
    uint8_t ports() const override { return 16; }
    bool select(uint8_t p, uint32_t now) override {
        if (p < 1 || p > ports() || state_.busy)
            return false;
        state_.requested = p;
        state_.busy = true;
        state_.evidence = Confirmation::Sent;
        state_.result = Result::Pending;
        started_ = now;
        return true;
    }
    void tick(uint32_t now) override {
        if (state_.busy && now - started_ >= 50) {
            state_.busy = false;
            state_.result = fail_ ? Result::Failed : Result::Success;
            if (!fail_) {
                state_.confirmed = state_.requested;
                state_.evidence = Confirmation::Acknowledged;
            }
        }
    }
    SwitchState state() const override { return state_; }
    void fail(bool v) { fail_ = v; }
};
struct Route {
    uint8_t ldg = 0, external = 0;
};
struct RouteReport {
    tuner_band_t band = BAND_UNKNOWN;
    uint8_t ldg = 0, external = 0;
    const char *stage = "disabled";
    bool partial = false;
    Result result = Result::Pending;
};
struct RouteSettings {
    bool enabled = false;
    Route bands[BAND_COUNT]{};
};
class RoutePlanner {
    tuner_band_t band_ = BAND_UNKNOWN, candidate_ = BAND_UNKNOWN, manual_ = BAND_UNKNOWN;
    unsigned samples_ = 0;
    uint32_t at_ = 0;
    bool pending_ = false, manualBefore_ = false, radioOwned_ = false;

  public:
    void manual() {
        manual_ = band_;
        manualBefore_ = !knownBand(band_);
        pending_ = false;
    }
    void reset() { *this = RoutePlanner(); }
    void observe(const tuner_state_t &s, uint32_t now, bool newMeter) {
        bool owns = s.radio.ownsBand();
        if (owns != radioOwned_) {
            pending_ = false;
            candidate_ = BAND_UNKNOWN;
            samples_ = 0;
            radioOwned_ = owns;
            band_ = BAND_UNKNOWN;
        }
        tuner_band_t next = BAND_UNKNOWN;
        if (owns) {
            if (!s.radio.frequencyValid || s.radio.stableMs < 200) {
                band_ = BAND_UNKNOWN;
                pending_ = false;
                return;
            }
            next = frequencyBand(s.radio.txFrequencyHz);
        } else if (newMeter) {
            if (now - at_ > 500) {
                samples_ = 0;
                candidate_ = BAND_UNKNOWN;
            }
            at_ = now;
            if (s.meter.forward_power_raw && knownBand(s.meter.band)) {
                if (candidate_ != s.meter.band) {
                    candidate_ = s.meter.band;
                    samples_ = 0;
                }
                if (++samples_ >= 3)
                    next = candidate_;
            } else {
                samples_ = 0;
                candidate_ = BAND_UNKNOWN;
            }
        }
        if (knownBand(next) && next != band_) {
            band_ = next;
            pending_ = !manualBefore_ && manual_ != next;
            if (manualBefore_)
                manual_ = next;
            manualBefore_ = false;
        }
    }
    static bool safe(const tuner_state_t &s) {
        return s.meterAgeMs <= 500 && s.meter.forward_power_raw == 0 &&
               s.meter.forward_power_watts == 0 && (!s.radio.ownsBand() || s.radio.safeRx());
    }
    uint8_t poll(const tuner_state_t &s, const RouteSettings &cfg, const SwitchState &sw) {
        if (!cfg.enabled || !pending_ || !knownBand(band_) || !safe(s) || sw.busy)
            return 0;
        if (cfg.bands[band_].ldg && s.antenna != cfg.bands[band_].ldg)
            return 0;
        const auto p = cfg.bands[band_].external;
        if (!p || p == sw.confirmed) {
            pending_ = false;
            return 0;
        }
        pending_ = false;
        return p;
    }
    tuner_band_t band() const { return band_; }
    bool manualHold() const { return manualBefore_ || manual_ == band_; }
};
} // namespace ldg
