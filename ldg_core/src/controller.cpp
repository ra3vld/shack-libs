// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
namespace ldg {
Controller::Controller(Wire w, Id boot, Operations &operations)
    : wire_(w),
      protocol_({this,
                 [](void *p, uint8_t b) {
                     auto &c = *static_cast<Controller *>(p);
                     return c.wire_.write && c.wire_.write(c.wire_.context, b);
                 },
                 [](void *p, uint8_t b) { return static_cast<Controller *>(p)->authorize(b); }}),
      operations_(operations), target_(boot) {}
Result Controller::submit(const Request &r, uint32_t now, unsigned &index) {
    if (paused_)
        return Result::Busy;
    switch (r.command) {
    case 'A':
    case 'T':
    case 'F':
    case 'P':
    case 'C':
    case 'M':
    case 0x81:
    case 0x82:
    case 0x83:
        break;
    case SelectExternal:
        if (!switch_ || r.port < 1 || r.port > switch_->ports())
            return Result::Unavailable;
        break;
    default:
        return Result::Invalid;
    }
    const bool duplicate = operations_.find(r.id) != nullptr;
    auto result = operations_.accept(r, target_, now, index);
    if (result == Result::Success && !duplicate) {
        ++manualRevision_;
        manualPending_ = true;
    }
    return result;
}
bool Controller::authorize(uint8_t cmd) {
    if (!automatic_)
        return true;
    auto s = state(now_);
    if (cmd == 'A')
        return s.meterAgeMs <= 500 && std::isfinite(s.meter.forward_power_watts) &&
               s.meter.forward_power_watts < 100 && (!requireZero_ || RoutePlanner::safe(s)) &&
               (selection_ == Selection::None ||
                (selectionRevision_ == manualRevision_ &&
                 (s.meter.forward_power_raw == 0 || s.meter.band == selectionBand_)));
    if (cmd == 'C' && selection_ == Selection::Restore)
        return true;
    if (cmd == 'T' && selection_ == Selection::Tuning)
        return selectionRevision_ == manualRevision_ && s.meterAgeMs <= 500 &&
               s.meter.forward_power_raw > 0 && s.meter.forward_power_watts > 0 &&
               s.meter.forward_power_watts < 100 && s.meter.band == selectionBand_;
    if (cmd == 'T' || cmd == 'C')
        return tuning_.allowsRfCommand(now_);
    return true;
}
bool Controller::startAutomatic(uint8_t cmd, bool zero) {
    automatic_ = true;
    requireZero_ = zero;
    autoCommand_ = cmd;
    return protocol_.start(cmd, now_);
}
void Controller::beginSelection(const AntennaAutomation::Request &r) {
    const auto s = state(now_);
    selectionTarget_ = r.antenna;
    selectionBand_ = antennas_.currentBand();
    selectionRevision_ = manualRevision_;
    selectionAttempts_ = modeAttempts_ = 0;
    selectionSucceeded_ = false;
    preventive_ = antennas_.usesRadio();
    const bool guard = s.mode == MODE_AUTO || r.recovery;
    restoreAuto_ = guard && !tuning_.enabled();
    selectionAt_ = now_;
    selectionFrames_ = frames_;
    if (guard) {
        selection_ = Selection::Manual;
        ++modeAttempts_;
        startAutomatic('M');
    } else {
        selection_ = Selection::WaitSelect;
        selectionFrames_ = frames_ - 1;
    }
}
void Controller::endSelection() {
    bool deferred = false;
    if (!selectionSucceeded_ && selectionRevision_ == manualRevision_) {
        if (preventive_) {
            antennas_.deferRadioSelection();
            deferred = true;
        } else if (tuning_.enabled() && selectionAttempts_ == 0 && state(now_).meterAgeMs > 500) {
            antennas_.selectionWaitForMeter(now_);
            deferred = true;
        }
    }
    if (!deferred)
        antennas_.selectionComplete(selectionSucceeded_);
    if (selectionSucceeded_)
        tuning_.antennaSelected();
    selection_ = Selection::None;
    autoCommand_ = 0;
    automatic_ = false;
}
void Controller::restoreSelectionMode() {
    if (restoreAuto_) {
        selection_ = Selection::Restore;
        modeAttempts_ = 1;
        startAutomatic('C');
    } else
        endSelection();
}
void Controller::selectionResult(Result result) {
    autoCommand_ = 0;
    automatic_ = false;
    switch (selection_) {
    case Selection::Manual:
        if (result != Result::Success) {
            if (modeAttempts_++ < 2 && selectionRevision_ == manualRevision_) {
                startAutomatic('M');
                return;
            }
            if (selectionRevision_ == manualRevision_ &&
                antennas_.selectionDeferred(selectionBand_, now_)) {
                selection_ = Selection::None;
                return;
            }
            restoreSelectionMode();
            return;
        }
        selection_ = Selection::WaitSelect;
        selectionAt_ = now_;
        selectionFrames_ = frames_;
        return;
    case Selection::Selecting:
        selectionSucceeded_ = result == Result::Success;
        if (!selectionSucceeded_ && selectionAttempts_ < 2 &&
            selectionRevision_ == manualRevision_ && result != Result::Unsafe) {
            selection_ = Selection::WaitSelect;
            selectionAt_ = now_;
            selectionFrames_ = frames_;
            return;
        }
        if (selectionSucceeded_ && restoreAuto_ && !preventive_) {
            selection_ = Selection::WaitTune;
            selectionAt_ = now_;
            selectionFrames_ = frames_;
            return;
        }
        restoreSelectionMode();
        return;
    case Selection::Tuning:
        restoreSelectionMode();
        return;
    case Selection::Restore:
        if (result != Result::Success && modeAttempts_++ < 2) {
            startAutomatic('C');
            return;
        }
        if (result != Result::Success) {
            restoreAfterRf_ = true;
            selectionSucceeded_ = false;
        }
        endSelection();
        return;
    default:
        return;
    }
}
void Controller::serviceSelection() {
    if (selection_ != Selection::WaitSelect && selection_ != Selection::WaitTune)
        return;
    if (selectionRevision_ != manualRevision_) {
        restoreSelectionMode();
        return;
    }
    const auto s = state(now_);
    if (frames_ == selectionFrames_ || s.meterAgeMs > 500) {
        if (now_ - selectionAt_ > 1500)
            restoreSelectionMode();
        return;
    }
    const bool power = std::isfinite(s.meter.forward_power_watts) &&
                       s.meter.forward_power_watts >= 0 && s.meter.forward_power_watts < 100;
    const bool band = s.meter.forward_power_raw == 0 || s.meter.band == selectionBand_;
    if (!power || !band || (preventive_ && !RoutePlanner::safe(s))) {
        restoreSelectionMode();
        return;
    }
    if (selection_ == Selection::WaitTune) {
        if (!s.meter.forward_power_raw) {
            restoreSelectionMode();
            return;
        }
        selection_ = Selection::Tuning;
        startAutomatic('T');
    } else {
        selection_ = Selection::Selecting;
        ++selectionAttempts_;
        startAutomatic(selectionTarget_ == ANT_A ? 0x81 : 0x82, preventive_);
    }
}
tuner_state_t Controller::state(uint32_t now) const {
    auto s = protocol_.state(now);
    s.radio = radio_;
    s.tuningManaged = tuning_.enabled();
    s.tuningState = tuning_.state();
    s.antennaInit = antennas_.init();
    s.antennaAuto = antennas_.automatic();
    s.antennaAutoBand = antennas_.currentBand();
    s.antennaFromRadio = antennas_.usesRadio();
    return s;
}
RouteReport Controller::routeReport(uint32_t now) const {
    RouteReport r;
    if (!routes_.enabled)
        return r;
    r.band = planner_.band();
    if (!knownBand(r.band)) {
        r.stage = "waiting_band";
        return r;
    }
    auto route = routes_.bands[r.band];
    r.ldg = route.ldg;
    r.external = route.external;
    if (planner_.manualHold()) {
        r.stage = "manual";
        return r;
    }
    auto local = state(now);
    auto external = switchState();
    const bool ld = !r.ldg || local.antenna == r.ldg,
               ex = !r.external || external.confirmed == r.external;
    r.partial = (r.ldg && ld && !ex) || (r.external && ex && !ld);
    if (ld && ex) {
        r.stage = "complete";
        r.result = Result::Success;
        return r;
    }
    if (!ld) {
        r.stage = "ldg";
        r.result = antennas_.automatic() == AntennaAuto::Error ? Result::Failed : Result::Pending;
        return r;
    }
    if (!switch_) {
        r.stage = "external";
        r.result = Result::Unavailable;
        return r;
    }
    if (external.requested == r.external && external.result == Result::Failed) {
        r.stage = "external";
        r.result = Result::Failed;
        return r;
    }
    r.stage = external.busy ? "switching" : RoutePlanner::safe(local) ? "external" : "waiting_rf";
    return r;
}
void Controller::tick(uint32_t now) {
    if (manualPending_) {
        manualPending_ = false;
        probeRestore_ = ANT_UNKNOWN;
        restoreAfterRf_ = false;
        antennas_.manualStarted();
        tuning_.manualStarted();
        planner_.manual();
    }
    const uint32_t elapsed = now - now_;
    now_ = now;
    radio_.ageMs = radio_.ageMs > UINT32_MAX - elapsed ? UINT32_MAX : radio_.ageMs + elapsed;
    radio_.stableMs =
        radio_.stableMs > UINT32_MAX - elapsed ? UINT32_MAX : radio_.stableMs + elapsed;
    if (radio_.ownsBand() && radio_.ageMs > RadioState::LivenessMs) {
        radio_.connection = RadioConnection::Disconnected;
        radio_.frequencyValid = false;
    }
    ++sequence_;
    if (!paused_ || protocol_.busy())
        protocol_.tick(now);
    if (switch_)
        switch_->tick(now);
    auto s = state(now);
    bool fresh = frames_ != protocol_.frames();
    if (fresh) {
        frames_ = protocol_.frames();
        antennas_.observe(s.meter, now, manualRevision_);
        tuning_.observe(s.meter, now);
    }
    antennas_.observeRadio(radio_, manualRevision_);
    tuning_.setPtt(radio_.connection == RadioConnection::Ready && radio_.rxKnown &&
                       radio_.tuneKnown,
                   radio_.transmitting || radio_.tuning);
    planner_.observe(s, now, fresh);
    Result result;
    if (protocol_.takeResult(result)) {
        if (selection_ != Selection::None) {
            selectionResult(result);
            return;
        }
        if (active_ >= 0 && operations_.at(active_).request.command != SelectExternal) {
            const auto cmd = operations_.at(active_).request.command;
            operations_.finish(active_, result, now);
            active_ = -1;
            if (cmd == 'A' || cmd == 0x81 || cmd == 0x82) {
                antennas_.manualAntennaComplete(result == Result::Success);
                tuning_.antennaSelected();
            }
            if (result == Result::Success &&
                (cmd == 'C' || cmd == 'M' || cmd == 0x83)) {
                tuningSettings_.managed = cmd == 0x83;
                managedChanged_ = true;
                tuning_.configure(tuningSettings_);
                if (cmd == 0x83)
                    tuning_.controlledSelected();
            }
        } else if (autoCommand_ == Probe || probeRecovering_) {
            if (result == Result::Unsafe) {
                if (!probeRecovering_)
                    probeRestore_ = protocol_.probeRestore();
                if (knownAntenna(probeRestore_))
                    antennas_.restoring();
                else
                    antennas_.probeDeferred();
            } else {
                probeRestore_ = ANT_UNKNOWN;
                antennas_.probeComplete(result == Result::Success);
            }
            probeRecovering_ = false;
        } else if (autoCommand_ == 0x81 || autoCommand_ == 0x82) {
            antennas_.selectionComplete(result == Result::Success);
            if (result == Result::Success)
                tuning_.antennaSelected();
        }
        if (tuningAction_ == TuningAutomation::Action::MemoryTune)
            tuning_.memoryComplete(result == Result::Success, now);
        else if (tuningAction_ != TuningAutomation::Action::None)
            tuning_.modeComplete(tuningAction_, result == Result::Success);
        tuningAction_ = TuningAutomation::Action::None;
        autoCommand_ = 0;
        automatic_ = false;
    }
    if (active_ >= 0 && operations_.at(active_).request.command == SelectExternal &&
        !switch_->state().busy) {
        operations_.finish(active_, switch_->state().result, now);
        active_ = -1;
    }
    if (paused_ || protocol_.busy() || active_ >= 0 || (switch_ && switch_->state().busy))
        return;
    if (selection_ != Selection::None) {
        serviceSelection();
        return;
    }
    int index = operations_.next(target_, now);
    if (index >= 0) {
        auto &op = operations_.at(index);
        automatic_ = false;
        if (op.request.command == SelectExternal) {
            if (!RoutePlanner::safe(s)) {
                operations_.finish(index, Result::Unsafe, now);
                return;
            }
            if (!switch_->select(op.request.port, now)) {
                operations_.finish(index, Result::Busy, now);
                return;
            }
        } else if (!protocol_.start(op.request.command, now)) {
            operations_.finish(index, Result::Busy, now);
            return;
        }
        active_ = index;
        op.state = OperationState::Running;
        return;
    }
    if (knownAntenna(probeRestore_)) {
        if (RoutePlanner::safe(s)) {
            probeRecovering_ = true;
            startAutomatic(probeRestore_ == ANT_A ? 0x81 : 0x82, true);
        }
        return;
    }
    if (restoreAfterRf_ && !tuning_.enabled() && RoutePlanner::safe(s)) {
        restoreAfterRf_ = false;
        automatic_ = false;
        autoCommand_ = 'C';
        protocol_.start('C', now_);
        return;
    }
    auto request = antennas_.poll(now, manualRevision_, false, s.antenna, s.mode == MODE_UNKNOWN,
                                  tuning_.enabled());
    if (request.action == AntennaAutomation::Action::Probe) {
        startAutomatic(Probe, true);
        return;
    }
    if (request.action == AntennaAutomation::Action::RefreshMeter) {
        startAutomatic('S');
        return;
    }
    if (request.action == AntennaAutomation::Action::Select) {
        beginSelection(request);
        return;
    }
    if (switch_ && !antennas_.selectionPending() && antennas_.automatic() != AntennaAuto::Error) {
        auto port = planner_.poll(s, routes_, switch_->state());
        if (port) {
            switch_->select(port, now);
            return;
        }
    }
    tuningAction_ = tuning_.poll(
        now, s.mode, antennas_.readyForTuning(now) && knownAntenna(s.antenna), false, true);
    if (tuningAction_ != TuningAutomation::Action::None)
        startAutomatic(tuningAction_ == TuningAutomation::Action::MemoryTune ? 'T'
                       : tuningAction_ == TuningAutomation::Action::Manual   ? 'M'
                                                                             : 'C');
}
} // namespace ldg
