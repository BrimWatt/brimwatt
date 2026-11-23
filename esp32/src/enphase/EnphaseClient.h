#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include <string>

#include "common/MeterReading.h"

namespace device {

/** Reads live meter values from the Envoy gateway over HTTPS (same call as the Java app). */
class EnphaseClient {
public:
    EnphaseClient() = default;

    /** @param fixedIp the gateway's address, or empty to find it as envoy.local */
    void configure(const std::string& token, const std::string& fixedIp) {
        token_ = token;
        fixedIp_ = fixedIp;
        haveIp_ = false;
    }

    /** True on success. On failure `error` says why. */
    bool read(solar::MeterReading& out, std::string& error);

private:
    bool findGateway(std::string& error);

    std::string token_;
    std::string fixedIp_;
    IPAddress ip_;
    bool haveIp_ = false;
};

}  // namespace device
