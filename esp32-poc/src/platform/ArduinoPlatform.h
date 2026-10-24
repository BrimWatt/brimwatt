#pragma once

#include "common/Platform.h"

namespace device {

/** The real clock and random numbers for the portable core. */
class ArduinoPlatform : public solar::Platform {
public:
    uint64_t nowMillis() const override;
    int64_t epochSeconds() const override;
    std::string utcIso() const override;
    uint32_t random32() override;
    int utcOffsetSeconds(int64_t epochSeconds) const override;
};

/** Sends the core's log lines to the serial port. */
void installSerialLogSink();

}  // namespace device
