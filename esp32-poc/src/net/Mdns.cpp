#include "net/Mdns.h"

#include <ESPmDNS.h>

#include "common/Log.h"

namespace device::Mdns {

bool start(const char* hostname, uint16_t ocppPort) {
    if (!MDNS.begin(hostname)) {
        LOG_ERROR("mDNS: could not start; the board will not answer to %s.local", hostname);
        return false;
    }
    MDNS.setInstanceName("Solar charger (ESP32)");
    MDNS.addService("ocpp", "tcp", ocppPort);
    LOG_INFO("mDNS: this board is %s.local", hostname);
    return true;
}

bool resolve(const char* host, IPAddress& out, uint32_t timeoutMillis) {
    IPAddress found = MDNS.queryHost(host, timeoutMillis);
    uint32_t raw = (uint32_t)found;
    if (raw == 0 || raw == 0xFFFFFFFF) return false;
    out = found;
    return true;
}

}  // namespace device::Mdns
