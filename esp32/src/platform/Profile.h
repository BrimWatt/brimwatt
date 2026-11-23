#pragma once

#include <string>

#include "car/TeslaMode.h"

namespace device {

/** Which setup is running (decided once at start-up from the Tesla mode switch; changing it restarts the board). */
struct Profile {
    static bool& tesla() {
        static bool on = false;
        return on;
    }
    static std::string nvsNamespace() { return solar::profileNamespace(tesla()); }
    static std::string file(const std::string& name) { return solar::profileFile(tesla(), name); }
};

}  // namespace device
