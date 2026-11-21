// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace ldg {
template <size_t N> class ImagePrefix {
    std::array<uint8_t, N> bytes_{};
    size_t used_ = 0;

  public:
    void reset() { used_ = 0; }
    size_t append(const uint8_t *p, size_t n) {
        size_t take = std::min(n, N - used_);
        memcpy(bytes_.data() + used_, p, take);
        used_ += take;
        return take;
    }
    bool complete() const { return used_ == N; }
    const uint8_t *data() const { return bytes_.data(); }
};
struct ContentRange {
    uint32_t first = 0, last = 0, total = 0;
    bool valid = false;
};
inline ContentRange parseContentRange(const char *value) {
    ContentRange r;
    if (!value || strncmp(value, "bytes ", 6))
        return r;
    const char *p = value + 6;
    auto number = [&](uint32_t &n) {
        if (*p < '0' || *p > '9')
            return false;
        n = 0;
        while (*p >= '0' && *p <= '9') {
            unsigned d = *p++ - '0';
            if (n > (UINT32_MAX - d) / 10)
                return false;
            n = n * 10 + d;
        }
        return true;
    };
    r.valid = number(r.first) && *p++ == '-' && number(r.last) && *p++ == '/' && number(r.total) &&
              *p == 0 && r.first <= r.last && r.last < r.total;
    return r;
}
} // namespace ldg
