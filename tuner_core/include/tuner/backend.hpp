// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace tuner {
using Id = std::array<uint8_t, 16>;

enum class Action : uint8_t {
    Tune, MemoryTune, FullTune, ToggleBypass, SetManual, SetAuto, SetControlled, SelectAntenna
};
enum class Mode : uint8_t { Unknown, Manual, Auto, Controlled, Bypass };
enum class Status : uint8_t {
    Unknown, Accepted, Running, Success, Failed, Timeout, Cancelled, TargetChanged,
    Expired, Busy, Invalid, Unavailable, Unsafe, Conflict, Unsupported
};

struct Capabilities {
    bool tune = false;
    bool memoryTune = false;
    bool fullTune = false;
    bool toggleBypass = false;
    bool manualMode = false;
    bool autoMode = false;
    bool controlledMode = false;
    bool rfMeter = false;
    uint8_t antennaPorts = 0;
};

struct Meter {
    double forwardWatts;
    double reflectedWatts;
    double swr;
};

struct Snapshot {
    bool connected = false;
    bool busy = false;
    uint32_t sequence = 0;
    uint32_t meterAgeMs = UINT32_MAX;
    Mode mode = Mode::Unknown;
    std::optional<Meter> meter;
    std::optional<uint8_t> antenna; // Zero-based. Empty when the current port is unknown.
};

struct Request {
    Id id{}, target{};
    uint32_t expiresAtMs = 0; // Monotonic clock of the owner.
    Action action = Action::Tune;
    uint8_t antenna = 0; // Zero-based. Used by SelectAntenna.
};

struct Submission {
    Status status;
    const char *detail = ""; // Optional static diagnostic from the backend.
};

// The device owner serializes calls with its I/O and state machine.
class ITunerBackend {
  public:
    virtual ~ITunerBackend() = default;
    virtual Capabilities capabilities() const = 0;
    virtual Snapshot snapshot(uint32_t now) const = 0;
    virtual Submission submit(const Request &, uint32_t now) = 0;
    virtual std::optional<Status> operation(const Id &) const = 0;
    virtual bool cancel(const Id &, uint32_t now) = 0;
    virtual void tick(uint32_t now) = 0;
};
} // namespace tuner
