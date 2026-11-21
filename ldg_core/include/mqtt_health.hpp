// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
namespace ldg {
inline bool managedMqttBroker(const char* host) {
    const char* expected="rotator.r3v.ru";
    for (;;++host,++expected) {
        const char ch=*host>='A' && *host<='Z' ? char(*host+'a'-'A') : *host;
        if (ch!=*expected) return false;
        if (!ch) return true;
    }
}
struct MqttMemoryGate {
    uint32_t free=0, largest=0;
    bool allowed(uint32_t available,uint32_t block) const { return available>=free && block>=largest; }
};
inline MqttMemoryGate mqttMemoryGate(bool tls, bool managed) {
    return tls ? MqttMemoryGate{(managed ? 52u : 64u)*1024,20u*1024} : MqttMemoryGate{16u*1024,8u*1024};
}
struct MqttHealth {
    const char* state="disabled", *error="";
    bool errorActive=false,tls=false,managed=false;
    uint32_t attempts=0,attemptAt=0,connectedAt=0,errorAt=0,connectMs=0;
    uint32_t requiredFree=0,requiredLargest=0,published=0,publishFailures=0;
    int transport=0,tlsStack=0,verifyFlags=0,socket=0,broker=0;
    void failed(const char* reason,uint32_t now) {error=reason;errorAt=now;errorActive=true;state="disconnected";}
    void connected(uint32_t now) {state="connected";connectedAt=now;connectMs=now-attemptAt;errorActive=false;}
};
}
