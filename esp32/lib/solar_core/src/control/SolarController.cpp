#include "control/SolarController.h"

#include "planner/CheapHours.h"

#include "notify/Notifier.h"
#include "planner/GoalPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "common/Config.h"
#include "common/Log.h"

namespace solar {

SolarController::SolarController(Platform& platform, SystemState& state, ChargeDevice& wallbox,
                                 DaylightSchedule& schedule, SettingsStore& store)
    : platform_(platform), state_(state), wallbox_(wallbox), schedule_(schedule), store_(store) {
    planners_.emplace(ChargingMode::Eco, ChargingPlanner(ChargingPolicy::eco()));
    planners_.emplace(ChargingMode::MaxSolar, ChargingPlanner(ChargingPolicy::maxSolar()));
}

// ---------------------------------------------------------------- settings

void SolarController::restoreSettings() {
    auto saved = store_.load();
    if (!saved) {
        LOG_INFO("Settings: none saved yet, starting with defaults (ECO, %d A custom, %d W allowance).",
                 state_.fastModeAmps(), state_.gridAllowanceWatts());
        return;
    }
    state_.setMode(saved->mode);
    if (saved->maxAmps >= config::kMinAmps && saved->maxAmps <= config::kMaxFastAmps) state_.setMaxAmps(saved->maxAmps);
    if (saved->fastModeAmps >= config::kMinAmps && saved->fastModeAmps <= config::kMaxFastAmps) {
        state_.setFastModeAmps(saved->fastModeAmps);
    }
    if (state_.fastModeAmps() > state_.maxAmps()) state_.setFastModeAmps(state_.maxAmps());
    if (saved->gridAllowanceWatts >= 0 && saved->gridAllowanceWatts <= config::kMaxGridAllowanceWatts) {
        state_.setGridAllowanceWatts(saved->gridAllowanceWatts);
    }
    state_.setDaylightWindowEnabled(saved->daylightWindowEnabled);
    auto marginOk = [](int m) { return m >= 0 && m <= config::kMaxDaylightMarginMinutes; };
    if (marginOk(saved->daylightStartMinutes) && marginOk(saved->daylightEndMinutes)) {
        state_.setDaylightMargins(saved->daylightStartMinutes, saved->daylightEndMinutes);
        schedule_.setMargins(saved->daylightStartMinutes * 60, saved->daylightEndMinutes * 60);
    }
    state_.setPaused(saved->paused);
    if (saved->rateCents >= 0 && saved->rateCents <= config::kMaxRateCents) state_.setRateCents(saved->rateCents);
    if (Notifier::isValidUrl(saved->notifyUrl)) state_.setNotifyUrl(saved->notifyUrl);
    if (saved->goalKwh >= 0 && saved->goalKwh <= config::kMaxGoalKwh && saved->goalMinutes >= 0 && saved->goalMinutes <= 1439) {
        state_.setGoal(saved->goalKwh, saved->goalMinutes);
    }
    state_.setPeakWeekends(saved->peakWeekends);
    if (saved->peakRateCents >= 0 && saved->peakRateCents <= config::kMaxRateCents && saved->peakStartHour >= 0 &&
        saved->peakStartHour <= 23 && saved->peakEndHour >= 1 && saved->peakEndHour <= 24) {
        state_.setPeakRate(saved->peakRateCents, saved->peakStartHour, saved->peakEndHour);
    }
    LOG_INFO("Settings: restored mode %s%s, custom %d A, allowance %d W, daylight window %s.", toString(state_.mode()),
             state_.paused() ? " (PAUSED)" : "", state_.fastModeAmps(), state_.gridAllowanceWatts(),
             state_.daylightWindowEnabled() ? "on" : "off");
    applyHold();
}

void SolarController::saveSettings() {
    Settings settings;
    settings.mode = state_.mode();
    settings.fastModeAmps = state_.fastModeAmps();
    settings.gridAllowanceWatts = state_.gridAllowanceWatts();
    settings.daylightWindowEnabled = state_.daylightWindowEnabled();
    settings.daylightStartMinutes = state_.daylightStartMinutes();
    settings.daylightEndMinutes = state_.daylightEndMinutes();
    settings.paused = state_.paused();
    settings.rateCents = state_.rateCents();
    settings.maxAmps = state_.maxAmps();
    settings.peakRateCents = state_.peakRateCents();
    settings.peakStartHour = state_.peakStartHour();
    settings.peakEndHour = state_.peakEndHour();
    settings.peakWeekends = state_.peakWeekends();
    settings.notifyUrl = state_.notifyUrl();
    settings.goalKwh = state_.goalKwh();
    settings.goalMinutes = state_.goalMinutes();
    store_.save(settings);
}

/**
 * What the wallbox should be held to between polls: while paused a stop (also against a car that starts by
 * itself); in FAST its fixed current (including for a car plugged in later); in the other modes nothing.
 */
void SolarController::applyHold() {
    if (state_.paused()) {
        wallbox_.holdFixedLimit(0, "paused by you");
    } else if (state_.mode() == ChargingMode::Fast) {
        wallbox_.holdFixedLimit(state_.fastModeAmps(), "custom current");
    } else if (state_.mode() == ChargingMode::Cheapest) {
        const bool expensive = expensiveNow();
        wallbox_.holdFixedLimit(expensive ? 0 : state_.maxAmps(), expensive ? "expensive hours" : "cheap hours");
    } else {
        wallbox_.holdFixedLimit(std::nullopt);
    }
}

bool SolarController::expensiveNow() const {
    const int64_t now = platform_.epochSeconds();
    return CheapHours::expensive(now, platform_.utcOffsetSeconds(now), state_.peakStartHour(), state_.peakEndHour(), state_.peakWeekends());
}

void SolarController::applyCheapHours() {
    const bool expensive = expensiveNow();
    if (wasExpensive_ && *wasExpensive_ != expensive) {
        event(expensive ? "Expensive hours began: charging stopped" : "Cheap hours began: charging allowed");
    }
    wasExpensive_ = expensive;
    applyHold();
}

static const char* modeLabel(ChargingMode mode) {
    return mode == ChargingMode::Fast ? "Custom" : mode == ChargingMode::Eco ? "Eco" : mode == ChargingMode::Cheapest ? "Cheapest hours" : "Max solar";
}

void SolarController::setMode(ChargingMode mode) {
    state_.setMode(mode);
    saveSettings();
    LOG_INFO("[MODE] Switched to %s.", toString(mode));
    event(std::string("Mode set to ") + modeLabel(mode));
    applyHold();
}

bool SolarController::setMaxAmps(int amps) {
    if (amps < config::kMinAmps || amps > config::kMaxFastAmps) return false;
    state_.setMaxAmps(amps);
    LOG_INFO("[MAX CURRENT] Maximum current set to %d A.", amps);
    event("Maximum current set to " + std::to_string(amps) + " A");
    if (state_.fastModeAmps() > amps) {
        state_.setFastModeAmps(amps);
        event("Custom speed lowered to " + std::to_string(amps) + " A");
    }
    saveSettings();
    applyHold();   // a fixed current that was lowered reaches the charger now
    return true;
}

bool SolarController::setFastAmps(int amps) {
    if (amps < config::kMinAmps || amps > state_.maxAmps()) return false;
    state_.setFastModeAmps(amps);
    saveSettings();
    LOG_INFO("[MODE] Custom current set to %d A.", amps);
    event("Custom speed set to " + std::to_string(amps) + " A");
    applyHold();
    return true;
}

bool SolarController::setGridAllowance(int watts) {
    if (watts < 0 || watts > config::kMaxGridAllowanceWatts) return false;
    state_.setGridAllowanceWatts(watts);
    saveSettings();
    LOG_INFO("[ALLOWANCE] Grid allowance set to %d W", watts);
    event("Grid allowance set to " + std::to_string(watts) + " W");
    return true;
}

void SolarController::setDaylightWindowEnabled(bool enabled) {
    state_.setDaylightWindowEnabled(enabled);
    saveSettings();
    LOG_INFO("[DAYLIGHT] Daylight window turned %s.", enabled ? "on" : "off");
}

bool SolarController::setDaylightMargins(int startMinutes, int endMinutes) {
    auto ok = [](int m) { return m >= 0 && m <= config::kMaxDaylightMarginMinutes; };
    if (!ok(startMinutes) || !ok(endMinutes)) return false;
    state_.setDaylightMargins(startMinutes, endMinutes);
    schedule_.setMargins(startMinutes * 60, endMinutes * 60);
    saveSettings();
    event("Solar hours set: " + std::to_string(startMinutes) + " min after sunrise to " + std::to_string(endMinutes) +
          " min before sunset");
    LOG_INFO("[DAYLIGHT] Window margins set to %d min after sunrise and %d min before sunset: %s", startMinutes,
             endMinutes, daylightText().c_str());
    return true;
}

void SolarController::setPaused(bool paused) {
    state_.setPaused(paused);
    saveSettings();
    const char* modeName = modeLabel(state_.mode());
    if (paused) {
        LOG_INFO("[PAUSE] Charging paused by the user (mode %s is kept).", toString(state_.mode()));
        event("Charging paused by you");
    } else {
        LOG_INFO("[PAUSE] Charging resumed: back to %s.", toString(state_.mode()));
        event(std::string("Charging resumed (") + modeName + ")");
    }
    applyHold();
}

bool SolarController::setGoal(int kwh, int byMinutes) {
    if (kwh < 0 || kwh > config::kMaxGoalKwh || byMinutes < 0 || byMinutes > 1439) return false;
    state_.setGoal(kwh, byMinutes);
    saveSettings();
    char when[8];
    std::snprintf(when, sizeof when, "%d:%02d", byMinutes / 60, byMinutes % 60);
    const std::string text = kwh == 0 ? std::string("Departure goal turned off")
                                      : "Departure goal: " + std::to_string(kwh) + " kWh by " + when;
    LOG_INFO("[GOAL] %s", text.c_str());
    event(text);
    return true;
}

bool SolarController::setNotifyUrl(const std::string& url) {
    if (!Notifier::isValidUrl(url)) return false;
    state_.setNotifyUrl(url);
    saveSettings();
    LOG_INFO("[NOTIFY] Notifications %s", url.empty() ? "turned off" : ("set to " + Notifier::maskedHost(url)).c_str());
    event(url.empty() ? "Notifications turned off" : "Notifications turned on");
    return true;
}

bool SolarController::setPrices(int cents, int peakCents, int startHour, int endHour, std::optional<bool> weekends) {
    if (cents < 0 || cents > config::kMaxRateCents || peakCents < 0 || peakCents > config::kMaxRateCents) return false;
    if (startHour < 0 || startHour > 23 || endHour < 1 || endHour > 24 || startHour == endHour) return false;
    const bool rateChanged = cents != state_.rateCents();
    const bool peakChanged = peakCents != state_.peakRateCents() || startHour != state_.peakStartHour() || endHour != state_.peakEndHour();
    const bool weekendsChanged = weekends && *weekends != state_.peakWeekends();
    if (weekends) state_.setPeakWeekends(*weekends);
    state_.setRateCents(cents);
    state_.setPeakRate(peakCents, startHour, endHour);
    saveSettings();
    LOG_INFO("[RATE] Price %d c/kWh, peak %d c/kWh from %d:00 to %d:00", cents, peakCents, startHour, endHour);
    if (rateChanged) event("Price set to " + std::to_string(cents) + "¢ per kWh");
    if (peakChanged) {
        event(peakCents == 0 ? std::string("Peak price turned off")
                             : "Peak price set to " + std::to_string(peakCents) + "¢ from " + std::to_string(startHour) + ":00 to " +
                                   std::to_string(endHour) + ":00");
    }
    if (weekendsChanged) event(state_.peakWeekends() ? "Expensive hours apply every day" : "Expensive hours apply Monday to Friday only");
    if ((peakChanged || weekendsChanged) && state_.mode() == ChargingMode::Cheapest) applyHold();   // the new window reaches the charger now
    return true;
}

bool SolarController::setRateCents(int cents) {
    if (cents < 0 || cents > config::kMaxRateCents) return false;
    state_.setRateCents(cents);
    saveSettings();
    LOG_INFO("[RATE] Electricity price set to %d cents per kWh", cents);
    event("Price set to " + std::to_string(cents) + "¢ per kWh");
    return true;
}

void SolarController::onCommanderChanged() {
    plannedMode_.reset();
    failingSince_.reset();
    goalForcing_ = false;
    applyHold();
}

void SolarController::stopNow() {
    wallbox_.holdFixedLimit(std::nullopt);
    wallbox_.setCommandContext("you asked to stop");
    wallbox_.sendChargingLimit(0);
    wallbox_.setCommandContext("");
    LOG_INFO("Stop requested.");
}

std::string SolarController::daylightText() const { return schedule_.describe(platform_.epochSeconds()); }

// ---------------------------------------------------------------- the poll cycle

/** When a departure goal's deadline has just passed, records whether the goal was reached (once per deadline). */
void SolarController::recordGoalOutcome() {
    if (!history_) return;
    const int kwh = state_.paused() || state_.mode() == ChargingMode::Fast ? 0 : state_.goalKwh();   // ignored in these, so nothing is judged
    const int64_t now = platform_.epochSeconds();
    const int64_t deadline = goalTracker_.due(now, platform_.utcOffsetSeconds(now), state_.goalMinutes(), kwh);
    if (deadline < 0) return;
    const double done = history_->carEnergyWhSince(deadline - goal::kWindowSeconds);
    if (done < 50 && !carPluggedIn()) return;   // no car that night: nothing to judge
    history_->recordGoalResult(deadline, done >= kwh * 1000.0, kwh * 1000.0, done);
}

bool SolarController::tick() {
    recordGoalOutcome();
    ChargingMode mode = state_.mode();
    bool charger = wallbox_.hasConnectedChargers();
    if (mode == ChargingMode::Cheapest && !state_.paused() && charger) {
        // Cheapest hours needs no solar: the price period decides. A departure goal that is running out of time still wins.
        if (goalTakesOver()) {
            wallbox_.holdFixedLimit(std::nullopt);   // do not fight the goal's current; the hold comes back when it lets go
            wasExpensive_.reset();
            return true;
        }
        applyCheapHours();
    }
    // FAST uses a fixed current and a paused system acts on nothing, so in both the meters are only read for the
    // dashboard; with no charger there is nothing to show or control. Either way history goes stale, so the
    // planner and any outage tracking start fresh.
    if (mode == ChargingMode::Fast || mode == ChargingMode::Cheapest || state_.paused() || !charger) {
        plannedMode_.reset();
        failingSince_.reset();
        failSafeActive_ = false;
        state_.setFailSafe(false);
        goalForcing_ = false;
        state_.setGoalProgress(0, false, 0);
        if (charger) {
            // The kill switch keeps a stop in force every poll, whatever the mode.
            if (state_.paused()) {
                wallbox_.setCommandContext("paused by you");
                wallbox_.sendChargingLimit(0);
                wallbox_.setCommandContext("");
            }
            wallbox_.refreshMeasurements();
            return true;
        }
        LOG_DEBUG("Skipping poll (mode %s, chargers connected: %s).", toString(mode), charger ? "yes" : "no");
        return false;
    }

    if (goalTakesOver()) return true;   // the meters are still read, for the dashboard
    if (!insideDaylightWindow(platform_.epochSeconds())) {
        pauseForNight();
        return false;
    }
    // Ask the charger for its real current; the reply lands in time for the next poll.
    wallbox_.refreshMeasurements();
    return true;
}

bool SolarController::carPluggedIn() {
    std::optional<ChargerStatus> status = wallbox_.primaryStatus();
    return status && (*status == ChargerStatus::Preparing || *status == ChargerStatus::Charging ||
                      *status == ChargerStatus::SuspendedEV || *status == ChargerStatus::SuspendedEVSE ||
                      *status == ChargerStatus::Finishing);
}

/**
 * The departure goal ("at least X kWh by a time"): while there is time to spare nothing changes, but once the time left
 * is only just enough it takes over from the solar rules (and the daylight window) and asks for the lowest current that
 * still gets there. Returns true if it did.
 */
bool SolarController::goalTakesOver() {
    const int goalKwh = state_.goalKwh();
    if (goalKwh <= 0 || !history_ || !carPluggedIn()) {
        goalForcing_ = false;
        state_.setGoalProgress(0, false, 0);
        return false;
    }
    const int64_t now = platform_.epochSeconds();
    const int64_t deadline = goal::nextDeadline(now, platform_.utcOffsetSeconds(now), state_.goalMinutes());
    const double done = history_->carEnergyWhSince(deadline - goal::kWindowSeconds);
    goal::Decision d = goal::decide(goalKwh * 1000.0, done, deadline - now, config::kMinAmps, state_.maxAmps(),
                                    config::kHouseholdVoltage);
    if (d.forcedAmps == 0) {
        goalForcing_ = false;
        state_.setGoalProgress(done, false, 0);
        return false;
    }
    if (!goalForcing_) {
        char kwh[16];
        std::snprintf(kwh, sizeof kwh, "%.1f", std::round(d.remainingWh / 100.0) / 10.0);
        event(std::string("Departure goal needs charging now: ") + kwh + " kWh still to go");
    }
    goalForcing_ = true;
    state_.setGoalProgress(done, true, d.forcedAmps);
    plannedMode_.reset();
    failingSince_.reset();
    failSafeActive_ = false;
    state_.setFailSafe(false);
    wallbox_.refreshMeasurements();
    wallbox_.setCommandContext("departure goal");
    wallbox_.sendChargingLimit(d.forcedAmps);
    wallbox_.setCommandContext("");
    return true;
}

bool SolarController::insideDaylightWindow(int64_t epochSeconds) {
    bool open = !state_.daylightWindowEnabled() || schedule_.isOpen(epochSeconds);
    if (!windowWasOpen_ || *windowWasOpen_ != open) {
        bool firstTime = !windowWasOpen_;
        windowWasOpen_ = open;
        if (!firstTime) event(open ? "Solar hours began" : "Solar hours ended");
        if (open) {
            LOG_INFO("[DAYLIGHT] Window open (%s): solar modes are active.", schedule_.describe(epochSeconds).c_str());
        } else {
            LOG_INFO("[DAYLIGHT] Outside the daylight window (%s): meter reads are paused and ECO/MAX_SOLAR charging "
                     "is stopped until it opens.",
                     schedule_.describe(epochSeconds).c_str());
        }
    }
    return open;
}

/** Outside daylight there is no solar to follow: stop charging and leave the gateway alone. */
void SolarController::pauseForNight() {
    plannedMode_.reset();
    failingSince_.reset();
    failSafeActive_ = false;
    state_.setFailSafe(false);
    state_.setEnphaseStatus(true, "");
    wallbox_.setCommandContext("outside solar hours");
    wallbox_.sendChargingLimit(0);
    wallbox_.setCommandContext("");
}

void SolarController::onMeterResult(const MeterResult& result) {
    ChargingMode mode = state_.mode();
    // The charger may have left while the meters were being read.
    if (!wallbox_.hasConnectedChargers()) return;

    uint64_t now = platform_.nowMillis();
    if (mode == ChargingMode::Fast || mode == ChargingMode::Cheapest || state_.paused() || goalForcing_) {   // (or it changed to one of them meanwhile)
        showReading(now, result);
        return;
    }
    if (!result.ok) {
        onFailure(now, mode, result.error);
        return;
    }
    onSuccess(now);
    control(now, mode, result.reading);
}

/**
 * Custom (FAST) mode: the meters are read so the dashboard shows real solar, grid and home power, but never acted
 * on. A failing gateway is reported on the dashboard and is never a fail-safe, since Custom needs no data.
 */
void SolarController::showReading(uint64_t now, const MeterResult& result) {
    if (!result.ok) {
        state_.setEnphaseStatus(false, result.error);
        if (!lastDisplayErrorLogged_ || now - *lastDisplayErrorLogged_ >= config::kEnphaseErrorLogIntervalMillis) {
            lastDisplayErrorLogged_ = now;
            LOG_ERROR("Failed to read from the Envoy (Custom mode, display only): %s", result.error.c_str());
        }
        return;
    }
    lastDisplayErrorLogged_.reset();
    state_.setEnphaseStatus(true, "");
    state_.setSolarWatts(result.reading.solarWatts < 0 ? 0 : result.reading.solarWatts);
    const bool paused = state_.paused();
    if (history_) {
        history_->recordSample(state_.solarWatts(), result.reading.gridWatts,
                               wallbox_.currentDrawAmps() * config::kHouseholdVoltage, state_.mode(), paused);
    }
    if (goalForcing_ && !paused && state_.mode() != ChargingMode::Fast) {
        state_.setControl({0, 0, state_.goalForcedAmps(), "departure goal", result.reading.gridWatts, now});
        LOG_INFO("[GOAL] solar %.0fW, grid %.0fW (+import/-export), charger %.1fA, departure goal asks for %dA", state_.solarWatts(),
                 result.reading.gridWatts, wallbox_.currentDrawAmps(), state_.goalForcedAmps());
        return;
    }
    if (state_.mode() == ChargingMode::Cheapest) {
        const bool expensive = expensiveNow();
        state_.setControl({0, 0, paused || expensive ? 0 : state_.maxAmps(), paused ? "paused" : expensive ? "expensive hours" : "cheap hours",
                           result.reading.gridWatts, now});   // no wait timer: the price change time is telemetry secondsToPriceChange
        LOG_INFO("[%s] solar %.0fW, grid %.0fW (+import/-export), charger %.1fA, %s", paused ? "PAUSED" : "CHEAPEST", state_.solarWatts(),
                 result.reading.gridWatts, wallbox_.currentDrawAmps(),
                 paused ? "charging paused" : expensive ? "expensive hours: not charging" : ("cheap hours: up to " + std::to_string(state_.maxAmps()) + "A").c_str());
        return;
    }
    state_.setControl({0, 0, paused ? 0 : state_.fastModeAmps(), paused ? "paused" : "custom current",
                       result.reading.gridWatts, now});
    LOG_INFO("[%s] solar %.0fW, grid %.0fW (+import/-export), charger %.1fA (%s), %s", paused ? "PAUSED" : "FAST",
             state_.solarWatts(), result.reading.gridWatts, wallbox_.currentDrawAmps(),
             wallbox_.drawIsMeasured() ? "measured" : "assumed from limit",
             paused ? "charging paused (display only)" : ("fixed " + std::to_string(state_.fastModeAmps()) + "A (display only)").c_str());
}

void SolarController::control(uint64_t now, ChargingMode mode, const MeterReading& reading) {
    double solarWatts = reading.solarWatts < 0 ? 0 : reading.solarWatts;
    double netGridWatts = reading.gridWatts;
    double exportWatts = -netGridWatts;
    state_.setSolarWatts(solarWatts);

    ChargingPlanner& planner = planners_.at(mode);
    planner.setMaxAmps(state_.maxAmps());
    // A fixed amount of grid power is treated as free in both modes, and it is the most they will ever import:
    // the planner stops rather than charge below its minimum. The user can change it.
    double allowanceWatts = state_.gridAllowanceWatts();
    if (!lastAllowanceWatts_ || *lastAllowanceWatts_ != allowanceWatts) {
        // The averages were built with the old allowance; start them from the new value so the change shows up
        // now instead of after several minutes of smoothing. Timers keep running.
        planner.reseedAverages();
        lastAllowanceWatts_ = allowanceWatts;
    }
    if (!plannedMode_ || *plannedMode_ != mode) {
        planner.reset(now, wallbox_.currentLimitAmps());
        plannedMode_ = mode;
    }

    planner.adoptRunning(now, wallbox_.currentLimitAmps());
    double drawAmps = wallbox_.currentDrawAmps();
    double availableAmps;
    ChargingPlanner::Decision decision;
    if (mode == ChargingMode::MaxSolar) {
        // Max solar: the car follows the panels and ignores the house; the allowance only tops it up to a whole amp.
        availableAmps = solarWatts / config::kHouseholdVoltage;
        decision = planner.next(now, availableAmps, allowanceWatts / config::kHouseholdVoltage);
    } else {
        // Eco: the surplus left after the house, plus the allowance. What the charger already draws counts as available.
        availableAmps = drawAmps + (exportWatts + allowanceWatts) / config::kHouseholdVoltage;
        if (availableAmps < 0) availableAmps = 0;
        decision = planner.next(now, availableAmps);
    }

    const double carWatts = drawAmps * config::kHouseholdVoltage;
    const double homeWatts = std::max(0.0, solarWatts + netGridWatts - carWatts);
    if (history_) history_->recordSample(solarWatts, netGridWatts, carWatts, mode, false);

    LOG_INFO("[%s] solar %.0fW, grid %.0fW (+import/-export), charger %.1fA (%s), allowance %.0fW -> available %.1fA, "
             "smoothed %.1fA => %dA (%s)",
             toString(mode), solarWatts, netGridWatts, drawAmps,
             wallbox_.drawIsMeasured() ? "measured" : "assumed from limit", allowanceWatts, availableAmps,
             decision.smoothedAmps, decision.amps, decision.reason.c_str());
    state_.setControl({availableAmps, decision.smoothedAmps, decision.amps, decision.reason, netGridWatts, now, decision.waitSeconds});
    wallbox_.setCommandContext(reasonForLimit(decision.amps, wallbox_.currentLimitAmps(), solarWatts, homeWatts, mode));
    wallbox_.sendChargingLimit(decision.amps);
    wallbox_.setCommandContext("");
}

/** The short reason shown in the activity log next to a charging command. */
std::string SolarController::reasonForLimit(int newAmps, std::optional<int> before, double solarWatts, double homeWatts,
                                            ChargingMode mode) {
    if (newAmps > 0) {
        if (!before) return "enough solar";
        return newAmps > *before ? "more solar" : "less solar left";
    }
    // Stopped for lack of surplus: is the sun the problem, or is the house using it? (Max solar ignores the house.)
    (void)homeWatts;
    if (mode == ChargingMode::MaxSolar) return "not enough solar";
    const double minWatts = config::kMinAmps * config::kHouseholdVoltage;
    return solarWatts + state_.gridAllowanceWatts() >= minWatts ? "home is using the solar" : "not enough solar";
}

void SolarController::onSuccess(uint64_t now) {
    if (failingSince_) {
        LOG_INFO("Envoy reachable again after %llus.", (unsigned long long)((now - *failingSince_) / 1000));
    }
    if (failSafeActive_) event("Solar meter is back");
    failingSince_.reset();
    if (failSafeActive_) {
        failSafeActive_ = false;
        state_.setFailSafe(false);
        plannedMode_.reset();   // the fail-safe changed the charger, so decide from scratch
    }
    state_.setEnphaseStatus(true, "");
}

void SolarController::onFailure(uint64_t now, ChargingMode mode, const std::string& message) {
    state_.setEnphaseStatus(false, message);

    if (!failingSince_) {
        failingSince_ = now;
        lastErrorLogged_ = now;
        LOG_ERROR("Failed to read from the Envoy: %s", message.c_str());
    } else if (now - lastErrorLogged_ >= config::kEnphaseErrorLogIntervalMillis) {
        lastErrorLogged_ = now;
        LOG_ERROR("Envoy still unavailable after %llus: %s", (unsigned long long)((now - *failingSince_) / 1000),
                  message.c_str());
    }

    if (now - *failingSince_ >= config::kEnphaseFailsafeAfterMillis) {
        if (!failSafeActive_) {
            failSafeActive_ = true;
            state_.setFailSafe(true);
            plannedMode_.reset();
            // Without readings the grid import is unknown, so neither mode may keep charging.
            LOG_WARN("[FAILSAFE] No Envoy data for %llus; %s stops charging until it returns.",
                     (unsigned long long)((now - *failingSince_) / 1000), toString(mode));
            event("Lost contact with the solar meter: charging stopped");
        }
        wallbox_.setCommandContext("no meter data");
        wallbox_.sendChargingLimit(0);
        wallbox_.setCommandContext("");
    }
}

}  // namespace solar
