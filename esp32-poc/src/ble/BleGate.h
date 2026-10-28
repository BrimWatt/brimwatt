#pragma once

namespace device {

/**
 * Whether Bluetooth may be started at all. It follows Tesla mode (off by default) and is set once at start-up; with it off the radio
 * stack is never initialised, so its memory stays free. Every place that would start a Bluetooth job asks first.
 */
struct BleGate {
    static bool& on() {
        static bool allowed = false;
        return allowed;
    }
    static const char* offText() { return "Tesla mode is off. Turn it on in Settings > Tesla first (or type: tesla mode on)."; }
};

}  // namespace device
