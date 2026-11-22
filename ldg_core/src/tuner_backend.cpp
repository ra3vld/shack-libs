// SPDX-License-Identifier: GPL-3.0-or-later
#include "ldg_tuner_backend.hpp"
#include "controller.hpp"

namespace ldg {
namespace {
using tuner::Action;
using tuner::Mode;
using tuner::Status;

Status statusOf(Result result) {
    switch (result) {
    case Result::Pending: return Status::Running;
    case Result::Success: return Status::Success;
    case Result::Failed: return Status::Failed;
    case Result::Timeout: return Status::Timeout;
    case Result::Cancelled: return Status::Cancelled;
    case Result::TargetChanged: return Status::TargetChanged;
    case Result::Expired: return Status::Expired;
    case Result::Busy: return Status::Busy;
    case Result::Invalid: return Status::Invalid;
    case Result::Unavailable: return Status::Unavailable;
    case Result::Unsafe: return Status::Unsafe;
    case Result::Unknown: return Status::Unknown;
    case Result::DuplicateConflict: return Status::Conflict;
    }
    return Status::Unknown;
}

Mode modeOf(const tuner_state_t &state) {
    if (state.mode == MODE_BYPASS)
        return Mode::Bypass;
    switch (operatingMode(state)) {
    case MODE_MANUAL: return Mode::Manual;
    case MODE_AUTO: return Mode::Auto;
    case MODE_CONTROLLED: return Mode::Controlled;
    default: return Mode::Unknown;
    }
}
} // namespace

tuner::Capabilities TunerBackend::capabilities() const {
    tuner::Capabilities caps;
    caps.tune = caps.memoryTune = caps.fullTune = true;
    caps.toggleBypass = caps.manualMode = caps.autoMode = caps.controlledMode = true;
    caps.rfMeter = true;
    caps.antennaPorts = 2;
    return caps;
}

tuner::Snapshot TunerBackend::snapshot(uint32_t now) const {
    const auto state = controller_.state(now);
    tuner::Snapshot result;
    result.connected = state.meterAgeMs < 3000;
    result.busy = controller_.busy();
    result.sequence = controller_.sequence();
    result.meterAgeMs = state.meterAgeMs;
    result.mode = modeOf(state);
    if (state.meterValid)
        result.meter = tuner::Meter{state.meter.forward_power_watts,
                                    state.meter.reflected_power_watts, state.meter.swr};
    if (knownAntenna(state.antenna))
        result.antenna = uint8_t(state.antenna) - 1;
    return result;
}

tuner::Submission TunerBackend::submit(const tuner::Request &request, uint32_t now) {
    Request wire{request.id, request.target, request.expiresAtMs};
    wire.sourceAction = uint8_t(request.action) + 1;
    switch (request.action) {
    case Action::Tune:
    case Action::MemoryTune:
        wire.command = TUNER_CMD_MEM_TUNE;
        break;
    case Action::FullTune:
        wire.command = TUNER_CMD_FULL_TUNE;
        break;
    case Action::ToggleBypass: {
        const auto *previous = controller_.operations().find(request.id);
        if (previous && previous->request.sourceAction == wire.sourceAction) {
            // A replay keeps the original direction after the device changes mode.
            wire.command = previous->request.command;
            break;
        }
        const auto state = controller_.state(now);
        if (state.mode != MODE_BYPASS)
            wire.command = TUNER_CMD_BYPASS;
        else
            wire.command = state.tuningManaged ? TUNER_CMD_CONTROLLED_MODE : TUNER_CMD_AUTO_MODE;
        break;
    }
    case Action::SetManual:
        wire.command = TUNER_CMD_MANUAL_MODE;
        break;
    case Action::SetAuto:
        wire.command = TUNER_CMD_AUTO_MODE;
        break;
    case Action::SetControlled:
        wire.command = TUNER_CMD_CONTROLLED_MODE;
        break;
    case Action::SelectAntenna:
        if (request.antenna >= capabilities().antennaPorts)
            return {Status::Invalid};
        wire.command = request.antenna == 0 ? TUNER_CMD_SELECT_ANT_A : TUNER_CMD_SELECT_ANT_B;
        break;
    default:
        return {Status::Unsupported};
    }
    unsigned index;
    const auto result = controller_.submit(wire, now, index);
    return {result == Result::Success ? Status::Accepted : statusOf(result), resultName(result)};
}

std::optional<tuner::Status> TunerBackend::operation(const tuner::Id &id) const {
    const auto *operation = controller_.operations().find(id);
    if (!operation)
        return std::nullopt;
    if (operation->state == OperationState::Queued)
        return Status::Accepted;
    if (operation->state == OperationState::Running)
        return Status::Running;
    return statusOf(operation->result);
}

bool TunerBackend::cancel(const tuner::Id &id, uint32_t now) {
    return controller_.cancel(id, now);
}

void TunerBackend::tick(uint32_t now) {
    controller_.tick(now);
}
} // namespace ldg
