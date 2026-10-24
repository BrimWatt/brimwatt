package com.example.enphase;

import com.example.charging.ChargingPlanner;
import com.example.charging.ChargingPlanner.Decision;
import com.example.charging.ChargingPolicy;
import com.example.config.AppConfig;
import com.example.daylight.DaylightSchedule;
import com.example.model.ChargingMode;
import com.example.model.EnphaseMeterReading;
import com.example.state.SystemState;
import com.example.wallbox.ChargerGateway;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.time.Instant;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

/**
 * Periodically reads solar/grid meters and pushes the resulting current limit to the wallbox.
 * If the gateway stays unreachable for too long, falls back to a safe limit instead of leaving
 * the charger on whatever it was last given.
 */
public class EnphasePoller {

    private static final Logger logger = LoggerFactory.getLogger(EnphasePoller.class);

    private static final long FAILSAFE_AFTER_MILLIS = TimeUnit.SECONDS.toMillis(AppConfig.ENPHASE_FAILSAFE_AFTER_SECONDS);
    private static final long ERROR_LOG_INTERVAL_MILLIS =
            TimeUnit.SECONDS.toMillis(AppConfig.ENPHASE_ERROR_LOG_INTERVAL_SECONDS);

    private final MeterReadingSource source;
    private final SystemState state;
    private final ChargerGateway wallbox;
    private final LongSupplier clock;
    private final DaylightSchedule schedule;
    private Boolean windowWasOpen;
    private volatile com.example.history.HistoryService history;

    private final Map<ChargingMode, ChargingPlanner> planners = new EnumMap<>(ChargingMode.class);
    /** The mode the active planner has been following; null forces a reset before the next decision. */
    private ChargingMode plannedMode;

    private long failingSince = -1;      // start of the current run of failed polls, or -1
    private long lastErrorLogged;
    private long lastDisplayErrorLogged = Long.MIN_VALUE / 2;
    private boolean failSafeActive;
    private double lastAllowanceWatts = Double.NaN;

    public EnphasePoller(MeterReadingSource source, SystemState state, ChargerGateway wallbox,
                         DaylightSchedule schedule) {
        this(source, state, wallbox, schedule, System::currentTimeMillis);
    }

    EnphasePoller(MeterReadingSource source, SystemState state, ChargerGateway wallbox, LongSupplier clock) {
        this(source, state, wallbox, DaylightSchedule.ALWAYS_OPEN, clock);
    }

    EnphasePoller(MeterReadingSource source, SystemState state, ChargerGateway wallbox,
                  DaylightSchedule schedule, LongSupplier clock) {
        this.schedule = schedule;
        this.source = source;
        this.state = state;
        this.wallbox = wallbox;
        this.clock = clock;
        planners.put(ChargingMode.ECO, new ChargingPlanner(ChargingPolicy.eco()));
        planners.put(ChargingMode.MAX_SOLAR, new ChargingPlanner(ChargingPolicy.maxSolar()));
    }

    /** Where to keep the minute-by-minute history and the activity log. Optional. */
    public void setHistory(com.example.history.HistoryService history) { this.history = history; }

    private void event(String text) {
        com.example.history.HistoryService h = history;
        if (h != null) h.addEvent(text);
    }

    public void start() {
        ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();
        scheduler.scheduleAtFixedRate(this::poll,
                AppConfig.POLL_INITIAL_DELAY_SECONDS, AppConfig.POLL_INTERVAL_SECONDS, TimeUnit.SECONDS);
        logger.info("Enphase polling scheduled every {} seconds.", AppConfig.POLL_INTERVAL_SECONDS);
    }

    void poll() {
        ChargingMode mode = state.mode();
        // FAST uses a fixed current, so the meters are only read for the dashboard; with no charger there is
        // nothing to show or control. Either way history goes stale, so the planner and any outage tracking
        // start fresh next time.
        if (mode == ChargingMode.FAST || state.paused() || !wallbox.hasConnectedChargers()) {
            plannedMode = null;
            failingSince = -1;
            failSafeActive = false;
            state.setFailSafe(false);
            if (wallbox.hasConnectedChargers()) {
                // Paused is the kill switch: keep a stop in force every poll, whatever the mode, and still read
                // the meters so the dashboard stays live.
                if (state.paused()) {
                    wallbox.setCommandContext("paused by you");
                    wallbox.sendChargingLimit(0);
                    wallbox.setCommandContext("");
                }
                readForDisplay(clock.getAsLong());
            } else {
                logger.debug("Skipping poll (mode {}, chargers connected: {}).", mode, wallbox.hasConnectedChargers());
            }
            return;
        }

        long now = clock.getAsLong();
        if (goalTakesOver(now)) return;
        if (!insideDaylightWindow(now)) {
            pauseForNight();
            return;
        }
        // Ask the charger for its real current; the reply lands in time for the next poll.
        wallbox.refreshMeasurements();
        List<EnphaseMeterReading> readings;
        try {
            readings = source.fetchMeterReadings();
            if (readings == null || readings.size() < 2) {
                throw new IOException("Gateway returned fewer than 2 meter readings");
            }
        } catch (Exception e) {
            onFailure(now, mode, e);
            return;
        }
        onSuccess(now);
        control(now, mode, readings);
    }

    /**
     * Custom (FAST) mode: read the meters so the dashboard shows real solar, grid and home power, but never act
     * on them. A failing gateway is reported on the dashboard and is never a fail-safe, since FAST needs no data.
     */
    private void readForDisplay(long now) {
        wallbox.refreshMeasurements();
        List<EnphaseMeterReading> readings;
        try {
            readings = source.fetchMeterReadings();
            if (readings == null || readings.size() < 2) {
                throw new IOException("Gateway returned fewer than 2 meter readings");
            }
        } catch (Exception e) {
            String message = describe(e);
            state.setEnphaseStatus(false, message);
            if (now - lastDisplayErrorLogged >= ERROR_LOG_INTERVAL_MILLIS) {
                lastDisplayErrorLogged = now;
                logger.error("Failed to read from Enphase gateway (Custom mode, display only): {}", message);
            }
            return;
        }
        lastDisplayErrorLogged = Long.MIN_VALUE / 2;
        state.setEnphaseStatus(true, "");
        double solarWatts = Math.max(0.0, readings.get(0).activePower);
        double netGridWatts = readings.get(1).activePower;
        state.setSolarWatts(solarWatts);
        com.example.history.HistoryService h = history;
        if (h != null) {
            h.recordSample(solarWatts, netGridWatts, wallbox.currentDrawAmps() * AppConfig.voltage(), state.mode(),
                    state.paused());
        }
        int shownTarget = state.paused() ? 0 : state.fastModeAmps();
        state.setControl(new SystemState.Control(0, 0, shownTarget, state.paused() ? "paused" : "custom current",
                netGridWatts, now));
        logger.info("[{}] solar {}W, grid {}W (+import/-export), charger {}A ({}), {}",
                state.paused() ? "PAUSED" : "FAST", Math.round(solarWatts), Math.round(netGridWatts),
                String.format("%.1f", wallbox.currentDrawAmps()), wallbox.drawIsMeasured() ? "measured" : "assumed from limit",
                state.paused() ? "charging paused (display only)" : "fixed " + state.fastModeAmps() + "A (display only)");
    }

    /**
     * The departure goal ("at least X kWh by a time"): while there is time to spare nothing changes, but once the time
     * left is only just enough, it takes over from the solar rules (and the daylight window) and asks for the lowest
     * current that still gets there. Returns true if it handled this poll.
     */
    private boolean goalTakesOver(long now) {
        int goalKwh = state.goalKwh();
        com.example.history.HistoryService h = history;
        if (goalKwh <= 0 || h == null || !wallbox.carPluggedIn()) {
            state.setGoalProgress(0, false, 0);
            return false;
        }
        long nowSec = now / 1000;
        int offset = h.zone().getRules().getOffset(Instant.ofEpochSecond(nowSec)).getTotalSeconds();
        long deadline = com.example.charging.GoalPlanner.nextDeadline(nowSec, offset, state.goalMinutes());
        double done = h.carEnergyWhSince(deadline - com.example.charging.GoalPlanner.WINDOW_SECONDS);
        var d = com.example.charging.GoalPlanner.decide(goalKwh * 1000.0, done, deadline - nowSec,
                AppConfig.MIN_AMPS, state.maxAmps(), AppConfig.voltage());
        if (d.forcedAmps() == 0) {
            state.setGoalProgress(done, false, 0);
            return false;
        }
        if (!state.goalForcing()) {
            event("Departure goal needs charging now: " + Math.round(d.remainingWh() / 100.0) / 10.0 + " kWh still to go");
        }
        state.setGoalProgress(done, true, d.forcedAmps());
        plannedMode = null;
        failingSince = -1;
        failSafeActive = false;
        state.setFailSafe(false);
        wallbox.refreshMeasurements();
        double solarWatts = 0, netGridWatts = 0;
        try {
            List<EnphaseMeterReading> readings = source.fetchMeterReadings();
            if (readings != null && readings.size() >= 2) {
                solarWatts = Math.max(0.0, readings.get(0).activePower);
                netGridWatts = readings.get(1).activePower;
                state.setSolarWatts(solarWatts);
                state.setEnphaseStatus(true, "");
            }
        } catch (Exception e) {
            // The goal does not need the meters; the car is charged either way.
        }
        h.recordSample(solarWatts, netGridWatts, wallbox.currentDrawAmps() * AppConfig.voltage(), state.mode(), false);
        state.setControl(new SystemState.Control(0, 0, d.forcedAmps(), "departure goal", netGridWatts, now));
        wallbox.setCommandContext("departure goal");
        wallbox.sendChargingLimit(d.forcedAmps());
        wallbox.setCommandContext("");
        return true;
    }

    private boolean insideDaylightWindow(long now) {
        Instant instant = Instant.ofEpochMilli(now);
        boolean open = !state.daylightWindowEnabled() || schedule.isOpen(instant);
        if (windowWasOpen == null || windowWasOpen != open) {
            if (windowWasOpen != null) event(open ? "Solar hours began" : "Solar hours ended");
            windowWasOpen = open;
            if (open) {
                logger.info("[DAYLIGHT] Window open ({}): solar modes are active.", schedule.describe(instant));
            } else {
                logger.info("[DAYLIGHT] Outside the daylight window ({}): Enphase polling is paused and ECO/MAX_SOLAR "
                        + "charging is stopped until it opens.", schedule.describe(instant));
            }
        }
        return open;
    }

    /** Outside daylight there is no solar to follow: stop charging and leave the gateway alone. */
    private void pauseForNight() {
        plannedMode = null;
        failingSince = -1;
        failSafeActive = false;
        state.setFailSafe(false);
        state.setEnphaseStatus(true, "");
        wallbox.setCommandContext("outside solar hours");
        wallbox.sendChargingLimit(0);
        wallbox.setCommandContext("");
    }

    private void control(long now, ChargingMode mode, List<EnphaseMeterReading> readings) {
        double solarWatts = Math.max(0.0, readings.get(0).activePower);
        double netGridWatts = readings.get(1).activePower;
        double exportWatts = -netGridWatts;
        state.setSolarWatts(solarWatts);

        ChargingPlanner planner = planners.get(mode);
        planner.setMaxAmps(state.maxAmps());
        // A fixed amount of grid power is treated as free in both modes, and it is the most they will ever
        // import: the planner stops rather than charge below its minimum. The dashboard can change it.
        double allowanceWatts = state.gridAllowanceWatts();
        if (allowanceWatts != lastAllowanceWatts) {
            // The averages were built with the old allowance; start them from the new value so the
            // change shows up now instead of after several minutes of smoothing. Timers (such as a
            // stop countdown) keep running.
            planner.reseedAverages();
            lastAllowanceWatts = allowanceWatts;
        }
        if (mode != plannedMode) {
            planner.reset(now, wallbox.currentLimitAmps());
            plannedMode = mode;
        }

        planner.adoptRunning(now, wallbox.currentLimitAmps());
        double drawAmps = wallbox.currentDrawAmps();
        double availableAmps;
        Decision decision;
        if (mode == ChargingMode.MAX_SOLAR) {
            // Max solar: the car follows the panels and ignores the house; the allowance only tops it up to a whole amp.
            availableAmps = solarWatts / AppConfig.voltage();
            decision = planner.next(now, availableAmps, allowanceWatts / AppConfig.voltage());
        } else {
            // Eco: the surplus left after the house, plus the allowance. What the charger already draws counts as available.
            availableAmps = Math.max(0.0, drawAmps + (exportWatts + allowanceWatts) / AppConfig.voltage());
            decision = planner.next(now, availableAmps);
        }

        double carWatts = drawAmps * AppConfig.voltage();
        com.example.history.HistoryService h = history;
        if (h != null) h.recordSample(solarWatts, netGridWatts, carWatts, mode, false);

        logger.info("[{}] solar {}W, grid {}W (+import/-export), charger {}A ({}), allowance {}W -> available {}A, smoothed {}A => {}A ({})",
                mode, Math.round(solarWatts), Math.round(netGridWatts), String.format("%.1f", drawAmps),
                wallbox.drawIsMeasured() ? "measured" : "assumed from limit",
                Math.round(allowanceWatts), String.format("%.1f", availableAmps), String.format("%.1f", decision.smoothedAmps()),
                decision.amps(), decision.reason());
        state.setControl(new SystemState.Control(availableAmps, decision.smoothedAmps(), decision.amps(),
                decision.reason(), netGridWatts, now));
        wallbox.setCommandContext(reasonForLimit(decision.amps(), wallbox.currentLimitAmps(), solarWatts, mode));
        wallbox.sendChargingLimit(decision.amps());
        wallbox.setCommandContext("");
    }

    /** The short reason shown in the activity log next to a charging command. */
    private String reasonForLimit(int newAmps, Integer before, double solarWatts, ChargingMode mode) {
        if (newAmps > 0) {
            if (before == null) return "enough solar";
            return newAmps > before ? "more solar" : "less solar left";
        }
        // Stopped for lack of surplus: is the sun the problem, or is the house using it? (Max solar ignores the house.)
        if (mode == ChargingMode.MAX_SOLAR) return "not enough solar";
        double minWatts = AppConfig.MIN_AMPS * AppConfig.voltage();
        return solarWatts + state.gridAllowanceWatts() >= minWatts ? "home is using the solar" : "not enough solar";
    }

    private void onSuccess(long now) {
        if (failingSince >= 0) {
            logger.info("Enphase gateway reachable again after {}s.", (now - failingSince) / 1000);
        }
        failingSince = -1;
        if (failSafeActive) {
            event("Solar meter is back");
            failSafeActive = false;
            state.setFailSafe(false);
            plannedMode = null;   // the fail-safe changed the charger, so decide from scratch
        }
        state.setEnphaseStatus(true, "");
    }

    private void onFailure(long now, ChargingMode mode, Exception e) {
        String message = describe(e);
        state.setEnphaseStatus(false, message);

        if (failingSince < 0) {
            failingSince = now;
            lastErrorLogged = now;
            logger.error("Failed to read from Enphase gateway: {}", message);
        } else if (now - lastErrorLogged >= ERROR_LOG_INTERVAL_MILLIS) {
            lastErrorLogged = now;
            logger.error("Enphase gateway still unavailable after {}s: {}", (now - failingSince) / 1000, message);
        }

        if (now - failingSince >= FAILSAFE_AFTER_MILLIS) {
            int fallbackAmps = 0;   // without readings the grid import is unknown, so neither mode may keep charging
            if (!failSafeActive) {
                failSafeActive = true;
                event("Lost contact with the solar meter: charging stopped");
                state.setFailSafe(true);
                plannedMode = null;
                logger.warn("[FAILSAFE] No Enphase data for {}s; {} falls back to {}A until it returns.",
                        (now - failingSince) / 1000, mode, fallbackAmps);
            }
            wallbox.setCommandContext("no meter data");
            wallbox.sendChargingLimit(fallbackAmps);
            wallbox.setCommandContext("");
        }
    }

    private static String describe(Exception e) {
        if (e.getMessage() != null) return e.getMessage();
        return e.getClass().getSimpleName();
    }
}
