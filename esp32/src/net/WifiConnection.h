#pragma once

#include <cstdint>
#include <string>

#include "net/WifiWatchdog.h"

namespace device {

/** Joins the Wi-Fi network under a recognisable device name and keeps the connection up. */
class WifiConnection {
public:
    explicit WifiConnection(const char* hostname) : hostname_(hostname) {}

    /**
     * Blocks until connected or the timeout passes. An empty password means an open network.
     * @param keepAccessPoint true while the setup network is open, so it is not switched off by the attempt
     */
    bool connect(const std::string& ssid, const std::string& password, uint32_t timeoutMillis, bool keepAccessPoint = false);

    /**
     * Call every pass of loop(): while the network is gone it nudges the radio (reconnect, fresh join, radio reset; see WifiWatchdog)
     * without blocking, and logs when the link comes back. Only after connect() has been given the network once.
     */
    void keepAlive();

    bool isConnected() const;
    int rssi() const;

    /** Why the last attempt failed, in words for the setup page ("wrong password", "network not found ..."). */
    const std::string& lastFailure() const { return lastFailure_; }

private:
    const char* hostname_;
    std::string lastFailure_;
    std::string ssid_;
    std::string password_;
    solar::WifiWatchdog watchdog_;
};

}  // namespace device
