#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace device {

/** mDNS: announces this board (brimwatt.local) and finds other devices such as the Envoy. */
namespace Mdns {

bool start(const char* hostname, uint16_t ocppPort);

/** Looks up `host`.local; returns false if nothing answers in time. */
bool resolve(const char* host, IPAddress& out, uint32_t timeoutMillis);

}  // namespace Mdns

}  // namespace device
