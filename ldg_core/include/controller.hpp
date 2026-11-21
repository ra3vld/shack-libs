// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operations.hpp"
#include "antenna_automation.h"
#include "tuning_automation.h"
#include "antenna_switch.hpp"
namespace ldg {
constexpr uint8_t SelectExternal = 0x85;
class Controller {
  public:
    Controller(Wire wire, Id boot, Operations &operations);
    Result submit(const Request &, uint32_t, unsigned &);
    bool cancel(const Id &id, uint32_t now) { return operations_.cancel(id, now); }
    const Operations &operations() const { return operations_; }
    void tick(uint32_t);
    void feed(const uint8_t *p, size_t n, uint32_t now) { protocol_.feed(p, n, now); }
    void fault() {
        protocol_.fault();
        antennas_.invalidateFrame();
        tuning_.invalidateFrame();
    }
    void radio(const RadioState &s) { radio_ = s; }
    void configure(const AntennaSettings &a, const TuningSettings &t, const RouteSettings &r,
                   float v) {
        antennas_.configure(a);
        tuning_.configure(t);
        tuningSettings_ = t;
        routes_ = r;
        if (!a.enabled)
            for (auto &route : routes_.bands)
                route.ldg = 0;
        protocol_.voltage(v);
    }
    void voltage(float v) { protocol_.voltage(v); }
    void switchDriver(AntennaSwitch *d) { switch_ = d; }
    tuner_state_t state(uint32_t) const;
    SwitchState switchState() const { return switch_ ? switch_->state() : SwitchState{}; }
    RouteReport routeReport(uint32_t now) const;
    bool busy() const {
        return protocol_.busy() || active_ >= 0 || selection_ != Selection::None ||
               (switch_ && switch_->state().busy);
    }
    bool canPause(uint32_t now) const {
        for (unsigned i = 0; i < Operations::Capacity; ++i)
            if (operations_.at(i).state == OperationState::Running)
                return false;
        const auto s = state(now);
        return !busy() && !(s.meterAgeMs <= 500 && s.meter.forward_power_raw > 0) &&
               !(s.radio.ownsBand() && (s.radio.transmitting || s.radio.tuning));
    }
    void pause(bool p) { paused_ = p; }
    Id target() const { return target_; }
    void changeTarget(Id t) { target_ = t; }
    bool authorize(uint8_t);
    bool takeManagedChange(bool &value) {
        if (!managedChanged_)
            return false;
        managedChanged_ = false;
        value = tuningSettings_.managed;
        return true;
    }
    uint32_t errors() const { return protocol_.errors(); }
    uint32_t frames() const { return protocol_.frames(); }
    uint32_t sequence() const { return sequence_; }

  private:
    Wire wire_;
    Protocol protocol_;
    Operations &operations_;
    Id target_;
    AntennaAutomation antennas_;
    TuningAutomation tuning_;
    TuningSettings tuningSettings_{};
    RouteSettings routes_{};
    RoutePlanner planner_;
    AntennaSwitch *switch_ = nullptr;
    RadioState radio_{};
    int active_ = -1;
    uint8_t autoCommand_ = 0;
    uint32_t now_ = 0, frames_ = 0, manualRevision_ = 0, sequence_ = 0;
    bool paused_ = false, automatic_ = false, requireZero_ = false;
    bool manualPending_ = false, managedChanged_ = false, probeRecovering_ = false,
         restoreAfterRf_ = false;
    tuner_ant_t probeRestore_ = ANT_UNKNOWN;
    enum class Selection : uint8_t {
        None,
        Manual,
        WaitSelect,
        Selecting,
        WaitTune,
        Tuning,
        Restore
    };
    Selection selection_ = Selection::None;
    tuner_ant_t selectionTarget_ = ANT_UNKNOWN;
    tuner_band_t selectionBand_ = BAND_UNKNOWN;
    bool restoreAuto_ = false, selectionSucceeded_ = false, preventive_ = false;
    uint32_t selectionRevision_ = 0, selectionAt_ = 0, selectionFrames_ = 0;
    uint8_t selectionAttempts_ = 0, modeAttempts_ = 0;
    void beginSelection(const AntennaAutomation::Request &);
    void selectionResult(Result);
    void serviceSelection();
    void endSelection();
    void restoreSelectionMode();
    TuningAutomation::Action tuningAction_ = TuningAutomation::Action::None;
    bool startAutomatic(uint8_t, bool zero = false);
};
} // namespace ldg
