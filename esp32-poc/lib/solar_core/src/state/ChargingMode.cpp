#include "state/ChargingMode.h"

#include <strings.h>

namespace solar {

const char* toString(ChargingMode mode) {
    switch (mode) {
        case ChargingMode::Eco: return "ECO";
        case ChargingMode::MaxSolar: return "MAX_SOLAR";
        case ChargingMode::Cheapest: return "CHEAPEST";
        default: return "FAST";
    }
}

std::optional<ChargingMode> parseChargingMode(const std::string& text) {
    // The old name and a friendly spelling still work (saved API clients, the serial console).
    if (strcasecmp("BALANCED", text.c_str()) == 0 || strcasecmp("MAXSOLAR", text.c_str()) == 0) return ChargingMode::MaxSolar;
    if (strcasecmp("CHEAP", text.c_str()) == 0 || strcasecmp("CHEAPEST_HOURS", text.c_str()) == 0) return ChargingMode::Cheapest;
    for (ChargingMode mode : {ChargingMode::Eco, ChargingMode::MaxSolar, ChargingMode::Fast, ChargingMode::Cheapest}) {
        if (strcasecmp(toString(mode), text.c_str()) == 0) return mode;
    }
    return std::nullopt;
}

}  // namespace solar
