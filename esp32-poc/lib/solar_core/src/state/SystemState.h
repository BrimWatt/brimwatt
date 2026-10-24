#pragma once

#include <optional>
#include <string>

#include "common/Config.h"
#include "state/ChargingMode.h"

namespace solar {

/** Shared state read by the console (and later the web layer) and written by the controller. Single-threaded. */
class SystemState {
public:
    /** Result of the latest control decision, shown on the dashboard. */
    struct Control {
        double availableAmps;
        double smoothedAmps;
        int targetAmps;
        std::string reason;
        double gridWatts;
        uint64_t readingAtMillis;
    };

    ChargingMode mode() const { return mode_; }
    void setMode(ChargingMode mode) { mode_ = mode; }

    double solarWatts() const { return solarWatts_; }
    void setSolarWatts(double watts) { solarWatts_ = watts; }

    /** The most grid power ECO and MAX_SOLAR will import. */
    int gridAllowanceWatts() const { return gridAllowanceWatts_; }
    void setGridAllowanceWatts(int watts) { gridAllowanceWatts_ = watts; }

    /** What a kWh costs, in cents; the savings shown are the sun's share of the car's energy at this price. */
    /** The most current the charger is ever asked for, in any mode (ECO and Max solar are also limited to 32 A). */
    int maxAmps() const { return maxAmps_; }
    void setMaxAmps(int amps) { maxAmps_ = amps; }

    /** The peak price in cents per kWh, or 0 for no peak pricing; and the hours it applies (end is exclusive, 1 to 24). */
    int peakRateCents() const { return peakRateCents_; }
    int peakStartHour() const { return peakStartHour_; }
    int peakEndHour() const { return peakEndHour_; }
    void setPeakRate(int cents, int startHour, int endHour) {
        peakRateCents_ = cents;
        peakStartHour_ = startHour;
        peakEndHour_ = endHour;
    }

    /** The departure goal: at least goalKwh kWh (0 = no goal) by goalMinutes after local midnight. */
    /** The web address notifications are posted to, or empty for none. */
    const std::string& notifyUrl() const { return notifyUrl_; }
    void setNotifyUrl(const std::string& url) { notifyUrl_ = url; }

    int goalKwh() const { return goalKwh_; }
    int goalMinutes() const { return goalMinutes_; }
    void setGoal(int kwh, int minutes) {
        goalKwh_ = kwh;
        goalMinutes_ = minutes;
    }
    /** What the goal looks like right now, for the dashboard. */
    double goalDoneWh() const { return goalDoneWh_; }
    bool goalForcing() const { return goalForcing_; }
    int goalForcedAmps() const { return goalForcedAmps_; }
    void setGoalProgress(double doneWh, bool forcing, int forcedAmps) {
        goalDoneWh_ = doneWh;
        goalForcing_ = forcing;
        goalForcedAmps_ = forcedAmps;
    }

    int rateCents() const { return rateCents_; }
    void setRateCents(int cents) { rateCents_ = cents; }

    /** Custom (FAST) mode current. */
    int fastModeAmps() const { return fastModeAmps_; }
    void setFastModeAmps(int amps) { fastModeAmps_ = amps; }

    /** Whether ECO and MAX_SOLAR pause outside the daylight window. */
    bool daylightWindowEnabled() const { return daylightWindowEnabled_; }
    void setDaylightWindowEnabled(bool enabled) { daylightWindowEnabled_ = enabled; }
    /** Minutes after sunrise that the window opens, and before sunset that it closes. */
    int daylightStartMinutes() const { return daylightStartMinutes_; }
    int daylightEndMinutes() const { return daylightEndMinutes_; }
    void setDaylightMargins(int startMinutes, int endMinutes) {
        daylightStartMinutes_ = startMinutes;
        daylightEndMinutes_ = endMinutes;
    }

    /**
     * The kill switch: while paused nothing is ever charged, whatever the mode. The mode itself is untouched, so
     * resuming simply hands the charger back to it.
     */
    bool paused() const { return paused_; }
    void setPaused(bool paused) { paused_ = paused; }

    /** Empty until the first successful reading. */
    const std::optional<Control>& control() const { return control_; }
    void setControl(Control control) { control_ = std::move(control); }

    bool failSafe() const { return failSafe_; }
    void setFailSafe(bool failSafe) { failSafe_ = failSafe; }

    bool enphaseOk() const { return enphaseOk_; }
    const std::string& enphaseMessage() const { return enphaseMessage_; }
    void setEnphaseStatus(bool ok, std::string message) {
        enphaseOk_ = ok;
        enphaseMessage_ = std::move(message);
    }

private:
    ChargingMode mode_ = ChargingMode::Eco;
    double solarWatts_ = 0;
    int gridAllowanceWatts_ = config::kGridAllowanceWatts;
    int fastModeAmps_ = config::kDefaultFastAmps;
    int rateCents_ = config::kDefaultRateCents;
    int maxAmps_ = config::kMaxFastAmps;
    std::string notifyUrl_;
    int goalKwh_ = 0;
    int goalMinutes_ = 420;   // 7:00 am
    double goalDoneWh_ = 0;
    bool goalForcing_ = false;
    int goalForcedAmps_ = 0;
    int peakRateCents_ = 0;
    int peakStartHour_ = 16;
    int peakEndHour_ = 21;
    bool daylightWindowEnabled_ = true;
    int daylightStartMinutes_ = config::kDaylightStartAfterSunriseSeconds / 60;
    int daylightEndMinutes_ = config::kDaylightEndBeforeSunsetSeconds / 60;
    bool paused_ = false;
    std::optional<Control> control_;
    bool failSafe_ = false;
    bool enphaseOk_ = true;
    std::string enphaseMessage_;
};

}  // namespace solar
