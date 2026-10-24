#include "net/TimeSync.h"

#include <Arduino.h>
#include <string.h>
#include <time.h>

#include "common/Log.h"
#include "config/DeviceConfig.h"

namespace device {

bool TimeSync::isValid() { return time(nullptr) > 1700000000; }   // later than November 2023

void TimeSync::begin(const char* gatewayAddress) {
    static char gateway[24];   // the SNTP client keeps the pointer, so the text must outlive this call
    strncpy(gateway, gatewayAddress, sizeof gateway - 1);
    configTzTime(kTimezone, "pool.ntp.org", "time.google.com", gateway);
    LOG_INFO("Time: asking pool.ntp.org, time.google.com and the router (%s) for the time, in the background.", gateway);
}

}  // namespace device
