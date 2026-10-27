#pragma once

#include <optional>
#include <string>

#include "common/Platform.h"
#include "history/HistoryService.h"
#include "wallbox/ChargeDevice.h"
#include "wallbox/ChargerRegistry.h"

namespace solar {

/**
 * Turns a desired charging current into OCPP commands: remote start when a vehicle is waiting, a charging
 * profile update while charging, and remote stop when there is nothing to give.
 *
 * Nothing here decides *what* current to use. A caller (for now the serial console, later the solar planner)
 * passes it in with sendChargingLimit().
 */
class WallboxController : public ChargeDevice {
public:
    WallboxController(Platform& platform, ChargerRegistry& chargers) : platform_(platform), chargers_(chargers) {}

    bool hasConnectedChargers() const override { return !chargers_.empty(); }

    /** Applies the same limit to every connected charger; 0 or less means stop. */
    void sendChargingLimit(int amps) override;

    /**
     * Keeps a fixed limit in force: it is sent now and again whenever a charger reports a new status (so a
     * car plugged in later is started). Pass nullopt to stop doing that.
     */
    void holdFixedLimit(std::optional<int> amps, const std::string& context = "") override;

    /** Where to record what was sent (the activity log). Optional. */
    void setHistory(HistoryService* history) override { history_ = history; }

    /** The reason given for the next commands sent, shown in the activity log ("enough solar"). Empty clears it. */
    void setCommandContext(const std::string& context) override { context_ = context; }

    /** Called when a charger reports a new status or transaction. */
    void onChargerChanged(Charger& charger);

    /** Asks charging chargers for their real current; the reply arrives as a MeterValues message. */
    void refreshMeasurements() override;

    /** Asks a charger for its status and meter values, so we learn what it is really doing after a restart. */
    void requestStatus(Charger& charger);

    /** requestStatus() for every connected charger. */
    void requestStatusAll();

    /** Current the connected chargers draw: measured if they report it, else the limit we set. */
    double currentDrawAmps() override;
    bool drawIsMeasured() override;

    /**
     * The limit in effect on a charger that is currently charging, if there is one. If we never set a limit (for
     * example after a restart) the measured current stands in for it, and failing that the maximum is assumed, so
     * the planner sets a real limit on its next decision.
     */
    std::optional<int> currentLimitAmps() override;

    /** True if any connected charger still shows state restored from the saved copy. */
    bool anyRestored() override;

    /** Status of the charger that matters most for display: a charging one first, otherwise any. */
    std::optional<ChargerStatus> primaryStatus() override;

    /** One line per charger, for the console and the status log. */
    std::string describe();

    /** Plain-language description of the last command sent, e.g. "Started charging at 10 A". */
    const std::string& lastCommand() const override { return lastCommand_; }

    /** When the last command was sent (epoch seconds), or 0 if none yet. */
    int64_t lastCommandAtSeconds() const override { return lastCommandAt_; }

private:
    void apply(Charger& charger, int amps);
    void start(Charger& charger, int amps);
    void updateLimit(Charger& charger, int amps);
    void stop(Charger& charger);
    template <typename FrameBuilder>
    void send(Charger& charger, const std::string& action, FrameBuilder build);
    void recordCommand(const std::string& description);

    Platform& platform_;
    ChargerRegistry& chargers_;
    std::optional<int> fixedAmps_;
    std::string fixedContext_;
    std::string context_;
    HistoryService* history_ = nullptr;
    std::string lastCommand_ = "None yet";
    int64_t lastCommandAt_ = 0;
};

}  // namespace solar
