// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from Remote-LDGControl 0d242cc; see NOTICE.
#pragma once


#include <stdint.h>
#include "meter_mapping.h"

namespace ldg {
struct MeterSettings {
    uint16_t watts = 1000;
    bool logarithmic = false;
};
inline bool validMeterSettings(const MeterSettings& settings) {
    return settings.watts >= 1 && settings.watts <= 1000;
}
struct StoredMeterSettings {
    uint32_t version = 1;
    uint16_t watts = 1000;
    uint8_t logarithmic = 0;
    uint8_t reserved = 0;
};
template<class Storage> bool writeMeterSettings(Storage& storage, const MeterSettings& settings) {
    if (!validMeterSettings(settings)) return false;
    StoredMeterSettings stored;
    stored.watts = settings.watts;
    stored.logarithmic = settings.logarithmic;
    return storage.putBytes("meterScaleV1", &stored, sizeof(stored)) == sizeof(stored);
}
template<class Storage> bool readMeterSettings(Storage& storage, MeterSettings& settings) {
    StoredMeterSettings stored;
    if (storage.getBytesLength("meterScaleV1") != sizeof(stored) ||
        storage.getBytes("meterScaleV1", &stored, sizeof(stored)) != sizeof(stored) ||
        stored.version != 1 || stored.logarithmic > 1 || stored.reserved ||
        stored.watts < 1 || stored.watts > 1000) return false;
    settings.watts = stored.watts;
    settings.logarithmic = stored.logarithmic;
    return true;
}


}
