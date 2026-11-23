#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <IPAddress.h>

#include <functional>
#include <vector>

#include "setup/SetupApi.h"

namespace device {

enum class SetupReason {
    NoWifi,       // nothing to join yet: stays open until something is saved
    JoinFailed,   // the saved network could not be joined
    Manual,       // BOOT button held, or the console command
};

/**
 * The board's own temporary Wi-Fi network ("WattSync-Setup") with a setup page, for choosing the home Wi-Fi and
 * pasting the Enphase details from a phone. It runs next to the normal Wi-Fi connection (access point plus station).
 * While it is open, every web address typed on a phone joined to it leads to the setup page (a "captive portal").
 *
 * It opens when there is no Wi-Fi to join, when joining fails for a few minutes at start-up, or by hand (hold BOOT for
 * 5 seconds, or type "setup" in the serial console). Except when there is no Wi-Fi at all, it closes after 15 minutes
 * without use. Saving restarts the board.
 */
class SetupPortal : public solar::setup::SetupHost {
public:
    void setBeforeRestart(std::function<void()> callback) { beforeRestart_ = std::move(callback); }

    void open(SetupReason reason);
    void close();

    /** Call often, also while the board runs normally: the DNS answers, the scan, the time limit, the BOOT button, the restart. */
    void loop();

    bool isActive() const { return active_; }
    bool clientsConnected() const;
    SetupReason reason() const { return reason_; }

    /** True for an address handed out by the setup network (192.168.4.x). */
    static bool isPortalClient(const IPAddress& address) { return address[0] == 192 && address[1] == 168 && address[2] == 4; }

    // solar::setup::SetupHost
    bool active() const override { return active_; }
    bool scanning() const override { return scanning_; }
    std::vector<solar::setup::ScanEntry> networks() const override { return networks_; }
    void startScan() override;
    uint32_t secondsLeft() const override;
    void userActive() override { lastActivity_ = millis(); }
    void restartSoon() override { restartAt_ = millis() + 1500; }

private:
    void pollScan();
    void watchBootButton();

    bool active_ = false;
    SetupReason reason_ = SetupReason::Manual;
    DNSServer dns_;
    std::function<void()> beforeRestart_;

    bool scanning_ = false;
    uint32_t scanStarted_ = 0;
    std::vector<solar::setup::ScanEntry> networks_;

    uint32_t lastActivity_ = 0;
    int lastStations_ = 0;
    uint32_t restartAt_ = 0;
    uint32_t pressedSince_ = 0;
    bool buttonReady_ = false;
};

}  // namespace device
