#pragma once

#include "ble/TeslaCar.h"
#include "ble/TeslaScan.h"
#include "car/CarControl.h"
#include "platform/NvsTeslaStore.h"

namespace device {

/**
 * The dashboard's view of the car: wraps the Bluetooth jobs (TeslaCar), keeps the "watch" switch in flash, reads the car every few
 * minutes while watching (read only: it never changes the car), and remembers the car's setting before a test change so it can be
 * put back. Nothing here controls the charge by itself; only the test buttons and console commands send commands.
 */
class CarService : public solar::CarControl {
public:
    CarService(TeslaCar& car, NvsTeslaStore& store, TeslaScan& scan) : car_(car), store_(store), scan_(scan), watch_(store.loadWatch()) {}

    /** Call from the main loop (also while the board has no Wi-Fi). Runs the Bluetooth job and the watch schedule. */
    void loop();

    solar::CarSnapshot snapshot() const override;
    solar::CarResult startPair(const std::string& vin) override;
    solar::CarResult startRefresh() override;
    solar::CarResult startStart() override;
    solar::CarResult startStop() override;
    solar::CarResult startSetAmps(int amps) override;
    void cancel() override { car_.cancel(); }
    void setWatch(bool on) override;
    void forget() override;

    static constexpr uint32_t kFirstReadMillis = 60UL * 1000;
    static constexpr uint32_t kWatchEveryMillis = 5UL * 60 * 1000;   // a Bluetooth read pauses the loop for a few seconds, so not more often

private:
    solar::CarResult blockedBy() const;
    solar::CarResult started(const std::string& error);
    void rememberRestore();

    TeslaCar& car_;
    NvsTeslaStore& store_;
    TeslaScan& scan_;
    bool watch_;
    uint32_t nextReadAt_ = 0;
    bool nextReadSet_ = false;
    uint32_t seenResultAt_ = 0;
    bool seenResult_ = false;
    int restoreAmps_ = 0;
    int pendingAmps_ = 0;
};

}  // namespace device
