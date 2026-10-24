#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "common/MeterReading.h"
#include "common/Platform.h"
#include "daylight/DaylightSchedule.h"
#include "history/HistoryService.h"
#include "planner/ChargingPlanner.h"
#include "state/SettingsStore.h"
#include "state/SystemState.h"
#include "wallbox/WallboxController.h"

namespace solar {

/**
 * Decides, once per poll, how much current the wallbox should get, and applies the settings the user changes.
 * It is the equivalent of the Java app's EnphasePoller (the control part) plus the dashboard's setters.
 *
 * It never talks to the Envoy itself. The caller reads the meters (asynchronously on the board):
 *
 *     if (controller.tick()) { ... read the meters ...; controller.onMeterResult(result); }
 *
 * Single-threaded: call everything from the main loop.
 */
class SolarController {
public:
    /** What reading the meters produced. */
    struct MeterResult {
        bool ok = false;
        MeterReading reading;
        std::string error;
    };

    SolarController(Platform& platform, SystemState& state, WallboxController& wallbox,
                    DaylightSchedule& schedule, SettingsStore& store);

    /** Where to keep the minute-by-minute history and the activity log. Optional. */
    void setHistory(HistoryService* history) {
        history_ = history;
        wallbox_.setHistory(history);
    }

    /** Applies the saved settings, if any. Call once at start-up. */
    void restoreSettings();

    // ---- settings (console now, dashboard later); each is saved and takes effect at once ----

    /** Switching to FAST sends the Custom current right away; leaving it hands the charger to the planner. */
    void setMode(ChargingMode mode);
    /** 6 to 40 A. Sent at once if the mode is FAST. Returns false if out of range. */
    bool setFastAmps(int amps);
    /** 0 to the maximum allowance. Returns false if out of range. */
    bool setGridAllowance(int watts);
    void setDaylightWindowEnabled(bool enabled);
    /** Minutes after sunrise the window opens and before sunset it closes (each 0 to 180). False if out of range. */
    bool setDaylightMargins(int startMinutes, int endMinutes);
    /** The most current any mode will ask for (6 to 40 A). False if out of range. */
    bool setMaxAmps(int amps);
    /** The departure goal: at least kwh kWh (0 turns it off, otherwise up to the largest goal) by byMinutes after local midnight. */
    bool setGoal(int kwh, int byMinutes);
    /** The address notifications are posted to (empty turns them off). False if it is not an http(s) address. */
    bool setNotifyUrl(const std::string& url);

    /**
     * The kill switch. Pausing stops the charge at once and keeps it stopped in every mode, even if a car starts
     * by itself; resuming hands the charger back to the mode that was set before. Saved, so it survives a restart.
     */
    void setPaused(bool paused);

    /** What a kWh costs, 0 to 200 cents; used to put a value on the sun's share of the car's energy. */
    bool setRateCents(int cents);
    /** The normal price and an optional peak price (0 = none) with its hours. False, changing nothing, if any is out of range. */
    bool setPrices(int cents, int peakCents, int startHour, int endHour);

    /** Stops the charge now. In ECO and MAX_SOLAR the planner may start it again by its normal rules. */
    void stopNow();

    // ---- the poll cycle ----

    /**
     * Call once per poll interval. Runs the checks that need no meter data (no charger, night) and returns true
     * if the meters should be read now. In Custom (FAST) mode and while paused they are read too, but only for the
     * dashboard.
     */
    bool tick();

    /** Feeds the outcome of the reading that tick() asked for. */
    void onMeterResult(const MeterResult& result);

    const SystemState& state() const { return state_; }

    /** Today's daylight window as text, e.g. "07:12 to 17:55". */
    std::string daylightText() const;

private:
    void saveSettings();
    void event(const std::string& text) {
        if (history_) history_->addEvent(text);
    }
    std::string reasonForLimit(int newAmps, std::optional<int> before, double solarWatts, double homeWatts, ChargingMode mode);
    void applyHold();
    bool insideDaylightWindow(int64_t epochSeconds);
    void pauseForNight();
    bool carPluggedIn();
    bool goalTakesOver();
    void control(uint64_t now, ChargingMode mode, const MeterReading& reading);
    void showReading(uint64_t now, const MeterResult& result);
    void onSuccess(uint64_t now);
    void onFailure(uint64_t now, ChargingMode mode, const std::string& message);

    Platform& platform_;
    SystemState& state_;
    WallboxController& wallbox_;
    DaylightSchedule& schedule_;
    SettingsStore& store_;

    std::map<ChargingMode, ChargingPlanner> planners_;
    /** The mode the active planner has been following; empty forces a reset before the next decision. */
    std::optional<ChargingMode> plannedMode_;

    std::optional<bool> windowWasOpen_;
    std::optional<uint64_t> failingSince_;
    uint64_t lastErrorLogged_ = 0;
    bool failSafeActive_ = false;
    bool goalForcing_ = false;
    std::optional<double> lastAllowanceWatts_;
    std::optional<uint64_t> lastDisplayErrorLogged_;
    HistoryService* history_ = nullptr;
};

}  // namespace solar
