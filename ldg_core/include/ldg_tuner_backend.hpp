// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <tuner/backend.hpp>

namespace ldg {
class Controller;

// The controller owns the UART state machine and the sole operation journal.
class TunerBackend final : public tuner::ITunerBackend {
  public:
    explicit TunerBackend(Controller &controller) : controller_(controller) {}

    tuner::Capabilities capabilities() const override;
    tuner::Snapshot snapshot(uint32_t now) const override;
    tuner::Submission submit(const tuner::Request &, uint32_t now) override;
    std::optional<tuner::Status> operation(const tuner::Id &) const override;
    bool cancel(const tuner::Id &, uint32_t now) override;
    void tick(uint32_t now) override;

  private:
    Controller &controller_;
};
} // namespace ldg
