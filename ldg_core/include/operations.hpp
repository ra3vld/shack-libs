// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "protocol.hpp"
#include <array>
#include <cstring>
namespace ldg {
using Id = std::array<uint8_t, 16>;
inline bool parseId(const char *s, Id &out, bool uuid = false) {
    if (!s || strlen(s) != (uuid ? 36u : 32u))
        return false;
    auto hex = [](char c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
    };
    unsigned p = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (uuid && (p == 8 || p == 13 || p == 18 || p == 23)) {
            if (s[p++] != '-')
                return false;
        }
        int a = hex(s[p++]), b = hex(s[p++]);
        if (a < 0 || b < 0)
            return false;
        out[i] = uint8_t(a * 16 + b);
    }
    return true;
}
inline void formatId(const Id &id, char *out, bool uuid = false) {
    static constexpr char h[] = "0123456789abcdef";
    unsigned p = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (uuid && (i == 4 || i == 6 || i == 8 || i == 10))
            out[p++] = '-';
        out[p++] = h[id[i] >> 4];
        out[p++] = h[id[i] & 15];
    }
    out[p] = 0;
}
struct Request {
    Id id{}, target{};
    uint32_t expires = 0;
    uint8_t command = 0, port = 0;
    uint8_t sourceAction = 0; // Preserves typed intent when an adapter translates a command.
};
enum class OperationState : uint8_t { Empty, Queued, Running, Done };
struct Operation {
    Request request{};
    uint32_t order = 0, finished = 0;
    OperationState state = OperationState::Empty;
    Result result = Result::Pending;
};
class Operations {
  public:
    static constexpr unsigned Capacity = 16;
    Result accept(const Request &r, const Id &target, uint32_t now, unsigned &index) {
        return accept(r, target, now, index, now);
    }
    Result accept(const Request &r, const Id &target, uint32_t now, unsigned &index,
                  uint32_t retainedNow) {
        for (unsigned i = 0; i < Capacity; ++i)
            if (entries_[i].state != OperationState::Empty && entries_[i].request.id == r.id) {
                index = i;
                const auto &old = entries_[i].request;
                return old.target == r.target && old.expires == r.expires &&
                               old.command == r.command && old.port == r.port &&
                               old.sourceAction == r.sourceAction
                           ? Result::Success
                           : Result::DuplicateConflict;
            }
        if (r.target != target)
            return Result::TargetChanged;
        const int32_t left = static_cast<int32_t>(r.expires - now);
        if (left <= 0 || left > 2000)
            return Result::Expired;
        for (unsigned i = 0; i < Capacity; ++i) {
            auto &op = entries_[i];
            if (op.state == OperationState::Empty ||
                (op.state == OperationState::Done && retainedNow - op.finished >= 60000)) {
                op = {r, ++sequence_, 0, OperationState::Queued, Result::Pending};
                index = i;
                return Result::Success;
            }
        }
        return Result::Busy;
    }
    int next(const Id &target, uint32_t now) { return next(target, now, now); }
    int next(const Id &target, uint32_t now, uint32_t retainedNow) {
        int found = -1;
        for (unsigned i = 0; i < Capacity; ++i) {
            auto &op = entries_[i];
            if (op.state != OperationState::Queued)
                continue;
            if (op.request.target != target) {
                finish(i, Result::TargetChanged, retainedNow);
                continue;
            }
            if (static_cast<int32_t>(now - op.request.expires) >= 0) {
                finish(i, Result::Expired, retainedNow);
                continue;
            }
            if (found < 0 || static_cast<int32_t>(op.order - entries_[found].order) < 0)
                found = int(i);
        }
        return found;
    }
    void finish(unsigned i, Result r, uint32_t now) {
        entries_[i].state = OperationState::Done;
        entries_[i].result = r;
        entries_[i].finished = now;
    }
    bool cancel(const Id &id, uint32_t now) {
        for (unsigned i = 0; i < Capacity; ++i)
            if (entries_[i].request.id == id && entries_[i].state == OperationState::Queued) {
                finish(i, Result::Cancelled, now);
                return true;
            }
        return false;
    }
    const Operation *find(const Id &id) const {
        for (const auto &o : entries_)
            if (o.state != OperationState::Empty && o.request.id == id)
                return &o;
        return nullptr;
    }
    Operation &at(unsigned i) { return entries_[i]; }
    const Operation &at(unsigned i) const { return entries_[i]; }

  private:
    std::array<Operation, Capacity> entries_{};
    uint32_t sequence_ = 0;
};
} // namespace ldg
