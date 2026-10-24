#pragma once

#include <cstdint>

namespace device {

/** Joins the Wi-Fi network under a recognisable device name and keeps the connection up. */
class WifiConnection {
public:
    explicit WifiConnection(const char* hostname) : hostname_(hostname) {}

    /** Blocks until connected or the timeout passes. */
    bool connect(uint32_t timeoutMillis);

    bool isConnected() const;
    int rssi() const;

private:
    const char* hostname_;
};

}  // namespace device
