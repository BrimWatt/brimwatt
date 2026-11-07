#pragma once

#include "fleet/FleetCar.h"

namespace device {

/** Runs one Fleet job at a time in its own short FreeRTOS task (a TLS exchange takes seconds and needs a big stack; the main loop must not wait for it). */
class EspFleetExecutor : public solar::fleet::FleetExecutor {
public:
    bool start(std::function<void()> job) override;
    void sleepMs(uint32_t ms) override;
};

}  // namespace device
