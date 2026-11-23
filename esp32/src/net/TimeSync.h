#pragma once

#include <cstdint>

namespace device {

/**
 * Network time. OCPP replies carry the time and the wallbox sets its clock from them, so the OCPP server
 * must not start answering until this has succeeded.
 */
class TimeSync {
public:
    /**
     * Starts SNTP in the background and returns at once. The router's address is added as a third time server
     * (most routers answer NTP), so the clock can be set even while the internet is down.
     */
    /** `posixZone` is the clock rule for the site (daylight saving included), e.g. "PST8PDT,M3.2.0,M11.1.0". */
    void begin(const char* gatewayAddress, const char* posixZone);

    /** True once the clock has been set (by any of the servers). */
    static bool isValid();
};

}  // namespace device
