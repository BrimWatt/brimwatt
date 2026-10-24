#pragma once

#include <cstdint>
#include <string>

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

    bool isConnected() const;
    int rssi() const;

    /** Why the last attempt failed, in words for the setup page ("wrong password", "network not found ..."). */
    const std::string& lastFailure() const { return lastFailure_; }

private:
    const char* hostname_;
    std::string lastFailure_;
};

}  // namespace device
