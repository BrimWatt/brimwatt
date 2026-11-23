#include "wallbox/Charger.h"

#include <strings.h>

namespace solar {

namespace {
/** Maps a charger status to the name used on the wire and in the log. */
struct Entry {
    ChargerStatus status;
    const char* name;
};
const Entry kEntries[] = {
    {ChargerStatus::Available, "Available"},     {ChargerStatus::Preparing, "Preparing"},
    {ChargerStatus::Charging, "Charging"},       {ChargerStatus::SuspendedEV, "SuspendedEV"},
    {ChargerStatus::SuspendedEVSE, "SuspendedEVSE"}, {ChargerStatus::Finishing, "Finishing"},
    {ChargerStatus::Reserved, "Reserved"},       {ChargerStatus::Unavailable, "Unavailable"},
    {ChargerStatus::Faulted, "Faulted"},
};
}  // namespace

const char* toString(ChargerStatus status) {
    for (const Entry& e : kEntries) {
        if (e.status == status) return e.name;
    }
    return "Unknown";
}

ChargerStatus parseChargerStatus(const std::string& text) {
    for (const Entry& e : kEntries) {
        if (strcasecmp(e.name, text.c_str()) == 0) return e.status;
    }
    return ChargerStatus::Unknown;
}

bool canStartCharging(ChargerStatus s) {
    return s == ChargerStatus::Preparing || s == ChargerStatus::SuspendedEV || s == ChargerStatus::SuspendedEVSE ||
           s == ChargerStatus::Finishing;
}

}  // namespace solar
