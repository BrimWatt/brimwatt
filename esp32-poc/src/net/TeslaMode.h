#pragma once

#include <Arduino.h>

#include <functional>

#include "car/TeslaMode.h"
#include "platform/NvsTeslaStore.h"
#include "net/SetupPortal.h"

namespace device {

/**
 * Tesla mode on the board: reads the saved switch at start-up (off unless the owner turned it on), lets Bluetooth start only if it is on
 * (BleGate), and keeps the board's own Wi-Fi "wattsync-tesla" open next to the home Wi-Fi so the dashboard can be reached anywhere, e.g. next
 * to a friend's car. The setup network ("WattSync-Setup") has priority while it is open; the Tesla hotspot comes back when it closes.
 * Changing the switch restarts the board: that is when Bluetooth's memory is handed back or kept in reserve.
 */
class TeslaMode : public solar::TeslaModeControl {
public:
    TeslaMode(NvsTeslaStore& store, SetupPortal& portal) : store_(store), portal_(portal) {}

    void setBeforeRestart(std::function<void()> callback) { beforeRestart_ = std::move(callback); }

    /** Once at start-up, after the Wi-Fi mode is set and before the network comes up. */
    void begin();
    /** Often: the restart, and putting the hotspot back after the setup network closed. */
    void loop();

    bool enabled() const override { return on_; }
    bool restartPending() const override { return restartAt_ != 0; }
    void setEnabled(bool on) override;
    solar::HotspotInfo hotspot() const override;
    std::string hotspotPassword() override;
    bool setClock(int64_t epochSeconds) override;

private:
    void startHotspot();

    NvsTeslaStore& store_;
    SetupPortal& portal_;
    std::function<void()> beforeRestart_;
    bool on_ = false;
    uint32_t restartAt_ = 0;
    uint32_t lastCheck_ = 0;
};

}  // namespace device
