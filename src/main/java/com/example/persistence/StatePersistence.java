package com.example.persistence;

import com.example.config.AppConfig;
import com.example.model.ChargingMode;
import com.example.state.SystemState;
import com.example.wallbox.Charger;
import com.example.wallbox.ChargerSnapshot;
import com.example.wallbox.WallboxSessionRegistry;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;
import java.util.Optional;
import java.util.TreeMap;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

/**
 * Keeps the mode, the fast-mode current and the last known state of each charger on disk, so a
 * restart of the app (or a dropped charger connection) does not lose track of a charge in progress.
 *
 * <p>Writes happen only when something changed, plus a timestamp refresh every few minutes, to
 * keep wear on the Pi's SD card low.
 */
public class StatePersistence {

    private static final Logger logger = LoggerFactory.getLogger(StatePersistence.class);

    private final StateStore store;
    private final SystemState state;
    private final WallboxSessionRegistry sessions;
    private final LongSupplier clock;

    private final Map<String, ChargerSnapshot> known = new ConcurrentHashMap<>();
    private String lastWritten;

    public StatePersistence(StateStore store, SystemState state, WallboxSessionRegistry sessions) {
        this(store, state, sessions, System::currentTimeMillis);
    }

    StatePersistence(StateStore store, SystemState state, WallboxSessionRegistry sessions, LongSupplier clock) {
        this.store = store;
        this.state = state;
        this.sessions = sessions;
        this.clock = clock;
    }

    /** Loads the saved mode, fast-mode current and charger snapshots. Call once, before serving requests. */
    private boolean restoredFromDamagedFile;

    /** True if the saved settings could not be read at all, so the app started paused instead of with defaults. */
    public boolean restoredFromDamagedFile() { return restoredFromDamagedFile; }

    public void restore() {
        store.load().ifPresentOrElse(saved -> {
            if (saved.mode() != null) state.setMode(saved.mode());
            if (saved.fastModeAmps() >= AppConfig.MIN_AMPS && saved.fastModeAmps() <= AppConfig.MAX_FAST_AMPS) {
                state.setFastModeAmps(saved.fastModeAmps());
            }
            Integer allowance = saved.gridAllowanceWatts();   // missing in files written by older versions
            if (allowance != null && allowance >= 0 && allowance <= AppConfig.MAX_GRID_ALLOWANCE_WATTS) {
                state.setGridAllowanceWatts(allowance);
            }
            if (saved.daylightWindowEnabled() != null) state.setDaylightWindowEnabled(saved.daylightWindowEnabled());
            Integer dayStart = saved.daylightStartMinutes(), dayEnd = saved.daylightEndMinutes();   // missing in older files
            if (dayStart != null && dayEnd != null && validMargin(dayStart) && validMargin(dayEnd)) {
                state.setDaylightMargins(dayStart, dayEnd);
            }
            Integer peak = saved.peakRateCents(), peakFrom = saved.peakStartHour(), peakTo = saved.peakEndHour();   // missing in older files
            if (peak != null && peakFrom != null && peakTo != null && peak >= 0 && peak <= AppConfig.MAX_RATE_CENTS
                    && peakFrom >= 0 && peakFrom <= 23 && peakTo >= 1 && peakTo <= 24) {
                state.setPeakRate(peak, peakFrom, peakTo);
            }
            if (saved.peakWeekends() != null) state.setPeakWeekends(saved.peakWeekends());   // missing in older files: every day
            if (saved.siteLat() != null && saved.siteLon() != null && saved.siteTz() != null) {   // missing in older files: the built-in default
                state.setSite(saved.siteLat(), saved.siteLon(), saved.siteTz(), saved.siteName());
            }
            Integer goalKwh = saved.goalKwh(), goalMinutes = saved.goalMinutes();   // missing in older files
            if (goalKwh != null && goalMinutes != null && goalKwh >= 0 && goalKwh <= AppConfig.MAX_GOAL_KWH && goalMinutes >= 0 && goalMinutes <= 1439) {
                state.setGoal(goalKwh, goalMinutes);
            }
            if (saved.notifyUrl() != null && com.example.notify.Notifier.isValidUrl(saved.notifyUrl())) state.setNotifyUrl(saved.notifyUrl());
            Integer maxAmps = saved.maxAmps();   // missing in older files: keep the default
            if (maxAmps != null && maxAmps >= AppConfig.MIN_AMPS && maxAmps <= AppConfig.MAX_FAST_AMPS) state.setMaxAmps(maxAmps);
            if (state.fastModeAmps() > state.maxAmps()) state.setFastModeAmps(state.maxAmps());
            if (saved.paused() != null) state.setPaused(saved.paused());
            Integer rate = saved.rateCents();   // missing in files from older versions
            if (rate != null && rate >= 0 && rate <= AppConfig.MAX_RATE_CENTS) state.setRateCents(rate);   // missing in files from older versions
            if (saved.chargers() != null) known.putAll(saved.chargers());
            lastWritten = store.toJson(saved);
            logger.info("Restored state from {}: mode {}{}, fast {}A, grid allowance {}W, {} known charger(s).",
                    store.path(), state.mode(), state.paused() ? " (PAUSED)" : "", state.fastModeAmps(),
                    state.gridAllowanceWatts(), known.size());
        }, () -> {
            if (store.lastLoadWasDamaged()) {
                // The settings include the kill switch, so a damaged file must not quietly turn it off: start paused.
                state.setPaused(true);
                restoredFromDamagedFile = true;
                logger.warn("The saved settings are damaged: starting PAUSED. Press Resume on the dashboard to continue.");
            } else {
                logger.info("No saved state at {}; starting with defaults.", store.path());
            }
        });
    }

    /** The last known state of a charger, if it is recent enough to be worth trusting. */
    public Optional<ChargerSnapshot> recall(String chargerId) {
        ChargerSnapshot snapshot = known.get(chargerId);
        if (snapshot == null) return Optional.empty();
        long ageMillis = clock.getAsLong() - snapshot.savedAtMillis();
        return ageMillis <= AppConfig.STATE_MAX_AGE_MILLIS ? Optional.of(snapshot) : Optional.empty();
    }

    /** Captures a charger's state the moment its connection goes away, before the next scheduled save can miss it. */
    public synchronized void onDisconnect(Charger charger) {
        known.put(charger.id(), charger.snapshot(clock.getAsLong()));
        save(false);
    }

    public void start() {
        ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "state-persistence");
            thread.setDaemon(true);
            return thread;
        });
        scheduler.scheduleWithFixedDelay(() -> save(false),
                AppConfig.STATE_SAVE_INTERVAL_SECONDS, AppConfig.STATE_SAVE_INTERVAL_SECONDS, TimeUnit.SECONDS);
        // A normal stop (systemd restart, Ctrl+C) saves one last time with fresh timestamps.
        Runtime.getRuntime().addShutdownHook(new Thread(() -> save(true), "state-persistence-shutdown"));
    }

    private static boolean validMargin(int minutes) { return minutes >= 0 && minutes <= AppConfig.MAX_DAYLIGHT_MARGIN_MINUTES; }

    /** @param refreshTimestamps true to stamp every connected charger with the current time */
    synchronized void save(boolean refreshTimestamps) {
        try {
            long now = clock.getAsLong();
            sessions.forEach((id, charger) -> remember(id, charger, now, refreshTimestamps));

            String json = store.toJson(new PersistedState(state.mode(), state.fastModeAmps(), new TreeMap<>(known),
                    state.gridAllowanceWatts(), state.daylightWindowEnabled(), state.paused(),
                    state.rateCents(), state.daylightStartMinutes(), state.daylightEndMinutes(), state.maxAmps(),
                    state.peakRateCents(), state.peakStartHour(), state.peakEndHour(), state.goalKwh(), state.goalMinutes(), state.notifyUrl(), state.peakWeekends(),
                    state.siteLatitude(), state.siteLongitude(), state.siteTimezone(), state.siteName()));
            if (!json.equals(lastWritten)) {
                store.write(json);
                lastWritten = json;
            }
        } catch (Exception e) {
            logger.warn("Could not save state to {}: {}", store.path(), e.getMessage());
        }
    }

    private void remember(String id, Charger charger, long now, boolean refreshTimestamps) {
        ChargerSnapshot current = charger.snapshot(now);
        ChargerSnapshot previous = known.get(id);
        boolean changed = !current.sameStateAs(previous);
        boolean stale = previous != null && now - previous.savedAtMillis() >= AppConfig.STATE_REFRESH_MILLIS;
        if (changed || stale || refreshTimestamps) known.put(id, current);
    }
}
