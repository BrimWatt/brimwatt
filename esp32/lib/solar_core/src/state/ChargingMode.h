#pragma once

#include <optional>
#include <string>

namespace solar {

/** FAST is shown as "Custom" on the dashboard: a fixed current that ignores the solar rules. */
enum class ChargingMode { Eco, MaxSolar, Fast, Cheapest };   // Cheapest = "Cheapest hours": max current outside the expensive hours, no meter needed   // MaxSolar used to be called Balanced; the old name is still accepted when parsing

const char* toString(ChargingMode mode);

/** Case-insensitive; empty if the text is not a mode. */
std::optional<ChargingMode> parseChargingMode(const std::string& text);

}  // namespace solar
