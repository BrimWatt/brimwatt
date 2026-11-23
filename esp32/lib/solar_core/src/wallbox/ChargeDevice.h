#pragma once

#include <optional>
#include <string>

#include "history/HistoryService.h"
#include "wallbox/Charger.h"

namespace solar {

/**
 * What the controller needs from whatever holds the charge: the OCPP wallbox, or the car itself over Bluetooth. The planner asks for a
 * current with sendChargingLimit() and learns what is really happening from the other calls. Exactly one device is in command at a time
 * (see ChargeRouter).
 */
class ChargeDevice {
public:
    virtual ~ChargeDevice() = default;

    /** Something to control is there (a wallbox with a charger connected, or a paired car). */
    virtual bool hasConnectedChargers() const = 0;
    /** The current to give: 0 or less means stop. Safe to repeat every poll. */
    virtual void sendChargingLimit(int amps) = 0;
    /** A fixed current (0 = stop) kept in force until cleared with nullopt. */
    virtual void holdFixedLimit(std::optional<int> amps, const std::string& context = "") = 0;
    virtual void setHistory(HistoryService* history) = 0;
    /** The reason given for the next commands, shown in the activity log. Empty clears it. */
    virtual void setCommandContext(const std::string& context) = 0;
    /** Asks for fresh numbers (the answer arrives before the next poll). */
    virtual void refreshMeasurements() = 0;

    virtual double currentDrawAmps() = 0;
    virtual bool drawIsMeasured() = 0;
    /** The limit in force while charging, if known. */
    virtual std::optional<int> currentLimitAmps() = 0;
    virtual bool anyRestored() = 0;
    virtual std::optional<ChargerStatus> primaryStatus() = 0;

    virtual const std::string& lastCommand() const = 0;
    virtual int64_t lastCommandAtSeconds() const = 0;
};

}  // namespace solar
