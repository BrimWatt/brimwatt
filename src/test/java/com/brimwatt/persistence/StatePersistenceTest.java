package com.brimwatt.persistence;

import static org.junit.jupiter.api.Assertions.*;

import com.brimwatt.model.ChargingMode;
import com.brimwatt.state.SystemState;
import com.brimwatt.wallbox.Charger;
import com.brimwatt.wallbox.ChargerSnapshot;
import com.brimwatt.wallbox.WallboxSessionRegistry;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class StatePersistenceTest {

    @TempDir Path dir;

    private final AtomicLong clock = new AtomicLong(10_000_000);
    private final AtomicInteger writes = new AtomicInteger();
    private Path file;

    private StateStore store;
    private SystemState state;
    private WallboxSessionRegistry sessions;
    private StatePersistence persistence;

    @BeforeEach
    void setUp() {
        file = dir.resolve("state.json");
        store = countingStore();
        state = new SystemState();
        sessions = new WallboxSessionRegistry();
        persistence = new StatePersistence(store, state, sessions, clock::get);
    }

    private StateStore countingStore() {
        return new StateStore(file) {
            @Override public void write(String json) throws IOException {
                writes.incrementAndGet();
                super.write(json);
            }
        };
    }

    /** A new "process": fresh state objects reading the same file. */
    private StatePersistence restart(SystemState newState) {
        StatePersistence p = new StatePersistence(countingStore(), newState, new WallboxSessionRegistry(), clock::get);
        p.restore();
        return p;
    }

    @Test
    void theKillSwitchSurvivesARestart() {
        state.setPaused(true);
        persistence.save(false);

        SystemState fresh = new SystemState();
        restart(fresh);
        assertTrue(fresh.paused());

        state.setPaused(false);
        persistence.save(false);
        SystemState resumed = new SystemState();
        restart(resumed);
        assertFalse(resumed.paused());
    }

    @Test
    void theDepartureGoalAndNotificationAddressSurviveARestart() {
        state.setGoal(25, 390);
        state.setNotifyUrl("https://hooks.example.com/x");
        persistence.save(false);
        SystemState fresh = new SystemState();
        restart(fresh);
        assertEquals(25, fresh.goalKwh());
        assertEquals(390, fresh.goalMinutes());
        assertEquals("https://hooks.example.com/x", fresh.notifyUrl());
    }

    @Test
    void theSavedLocationSurvivesARestartAndOlderFilesHaveNone() throws IOException {
        assertFalse(state.siteSet());
        state.setSite(34.09, -118.41, "America/Los_Angeles", "Beverly Hills, California");
        persistence.save(false);
        SystemState fresh = new SystemState();
        restart(fresh);
        assertTrue(fresh.siteSet());
        assertEquals(34.09, fresh.siteLatitude(), 1e-9);
        assertEquals(-118.41, fresh.siteLongitude(), 1e-9);
        assertEquals("America/Los_Angeles", fresh.siteTimezone());
        assertEquals("Beverly Hills, California", fresh.siteName());

        Files.writeString(file, "{\"mode\":\"ECO\",\"fastModeAmps\":24,\"chargers\":{}}");   // a file from before the location existed
        SystemState old = new SystemState();
        restart(old);
        assertFalse(old.siteSet());
    }

    @Test
    void stateFilesFromOlderVersionsAreNotPaused() throws IOException {
        Files.writeString(file, "{\"mode\":\"ECO\",\"fastModeAmps\":20,\"chargers\":{}}");
        SystemState fresh = new SystemState();
        restart(fresh);
        assertFalse(fresh.paused());
    }

    private Charger chargingCharger(String id) {
        Charger charger = sessions.register(id, null);
        charger.setStatus(Charger.Status.CHARGING);
        charger.setTransactionId(42);
        charger.setAppliedAmps(14);
        return charger;
    }

    @Test
    void daylightMarginsSurviveARestartAndOlderFilesGetTheDefaults() throws IOException {
        state.setDaylightMargins(20, 90);
        persistence.save(false);
        SystemState after = new SystemState();
        restart(after);
        assertEquals(20, after.daylightStartMinutes());
        assertEquals(90, after.daylightEndMinutes());

        Files.writeString(file, "{\"mode\":\"ECO\",\"fastModeAmps\":20,\"chargers\":{}}");
        SystemState old = new SystemState();
        restart(old);
        assertEquals(30, old.daylightStartMinutes());
        assertEquals(60, old.daylightEndMinutes());
    }

    @Test
    void aDamagedSettingsFileStartsPausedInsteadOfQuietlyLiftingThePause() throws IOException {
        Files.writeString(file, "{\"mode\":\"ECO\",\"paused\":tr");
        SystemState fresh = new SystemState();
        StatePersistence p = restart(fresh);
        assertTrue(fresh.paused(), "the kill switch must not be lost to a damaged file");
        assertTrue(p.restoredFromDamagedFile());
    }

    @Test
    void aDamagedSettingsFileFallsBackToTheLastGoodBackup() throws IOException {
        state.setMode(ChargingMode.MAX_SOLAR);
        state.setGridAllowanceWatts(300);
        persistence.save(false);                     // the first good copy
        state.setGridAllowanceWatts(500);
        persistence.save(false);                     // keeps the first as the backup
        Files.writeString(file, "{ not json");

        SystemState fresh = new SystemState();
        StatePersistence p = restart(fresh);
        assertFalse(fresh.paused(), "a usable backup means no need to pause");
        assertFalse(p.restoredFromDamagedFile());
        assertEquals(ChargingMode.MAX_SOLAR, fresh.mode());
        assertEquals(300, fresh.gridAllowanceWatts(), "the backup is one save behind");
    }

    @Test
    void maximumCurrentSurvivesARestartAndAlsoLowersCustom() {
        state.setMaxAmps(16);
        state.setFastModeAmps(16);
        persistence.save(false);
        SystemState after = new SystemState();
        restart(after);
        assertEquals(16, after.maxAmps());

    }

    @Test
    void modeAndFastAmpsSurviveARestart() {
        state.setMode(ChargingMode.MAX_SOLAR);
        state.setFastModeAmps(20);
        persistence.save(false);

        SystemState after = new SystemState();
        restart(after);

        assertEquals(ChargingMode.MAX_SOLAR, after.mode());
        assertEquals(20, after.fastModeAmps());
    }

    @Test
    void daylightToggleSurvivesARestartAndDefaultsToOn() throws IOException {
        state.setDaylightWindowEnabled(false);
        persistence.save(false);
        SystemState after = new SystemState();
        restart(after);
        assertFalse(after.daylightWindowEnabled());

        Files.writeString(file, "{\"mode\":\"ECO\",\"fastModeAmps\":32,\"chargers\":{}}");
        SystemState older = new SystemState();
        restart(older);
        assertTrue(older.daylightWindowEnabled(), "files from older versions keep the window on");
    }

    @Test
    void gridAllowanceSurvivesARestart() {
        state.setGridAllowanceWatts(800);
        persistence.save(false);

        SystemState after = new SystemState();
        restart(after);
        assertEquals(800, after.gridAllowanceWatts());
    }

    @Test
    void fileWrittenByAnOlderVersionWithoutAllowanceAndWithTheOldBalancedNameStillLoads() throws IOException {
        Files.writeString(file, "{\"mode\":\"BALANCED\",\"fastModeAmps\":20,\"chargers\":{}}");
        SystemState after = new SystemState();
        restart(after);

        assertEquals(ChargingMode.MAX_SOLAR, after.mode());
        assertEquals(1000, after.gridAllowanceWatts());
    }

    @Test
    void outOfRangeAllowanceInFileIsIgnored() throws IOException {
        Files.writeString(file, "{\"mode\":\"ECO\",\"fastModeAmps\":32,\"chargers\":{},\"gridAllowanceWatts\":99999}");
        SystemState after = new SystemState();
        restart(after);
        assertEquals(1000, after.gridAllowanceWatts());
    }

    @Test
    void chargingSessionIsRecalledAfterARestart() {
        chargingCharger("mywallbox");
        persistence.save(false);

        ChargerSnapshot recalled = restart(new SystemState()).recall("mywallbox").orElseThrow();

        assertEquals("CHARGING", recalled.status());
        assertEquals(42, recalled.transactionId());
        assertEquals(14, recalled.appliedAmps());
    }

    @Test
    void restoringIntoAChargerMarksItUnverifiedUntilTheChargerReports() {
        chargingCharger("mywallbox");
        persistence.save(false);
        ChargerSnapshot snapshot = restart(new SystemState()).recall("mywallbox").orElseThrow();

        Charger fresh = new Charger("mywallbox", null);
        fresh.restoreFrom(snapshot);
        assertEquals(Charger.Status.CHARGING, fresh.status());
        assertEquals(42, fresh.transactionId());
        assertTrue(fresh.isRestored());

        fresh.setStatus(Charger.Status.CHARGING);    // the charger confirms
        assertFalse(fresh.isRestored());
    }

    @Test
    void oldSnapshotsAreIgnored() {
        chargingCharger("mywallbox");
        persistence.save(false);

        clock.addAndGet(2 * 60 * 60_000L);           // two hours later
        assertTrue(restart(new SystemState()).recall("mywallbox").isEmpty());
    }

    @Test
    void stateOfADisconnectedChargerIsKept() {
        chargingCharger("mywallbox");
        persistence.save(false);
        sessions.forEach((id, c) -> {});
        sessions = new WallboxSessionRegistry();     // charger gone
        persistence = new StatePersistence(store, state, sessions, clock::get);
        persistence.restore();
        persistence.save(false);

        assertTrue(restart(new SystemState()).recall("mywallbox").isPresent());
    }

    @Test
    void chargerThatDisconnectsBeforeAnyScheduledSaveIsStillRemembered() {
        Charger charger = chargingCharger("mywallbox");
        persistence.onDisconnect(charger);           // nothing was saved yet

        assertTrue(restart(new SystemState()).recall("mywallbox").isPresent());
    }

    @Test
    void corruptOrMissingFileFallsBackToDefaults() throws IOException {
        SystemState fresh = new SystemState();
        restart(fresh);
        assertEquals(ChargingMode.ECO, fresh.mode());

        Files.writeString(file, "{ this is not json");
        restart(fresh);
        assertEquals(ChargingMode.ECO, fresh.mode());
        assertEquals(32, fresh.fastModeAmps());
    }

    @Test
    void outOfRangeFastAmpsInFileIsIgnored() throws IOException {
        Files.writeString(file, "{\"mode\":\"FAST\",\"fastModeAmps\":500,\"chargers\":{}}");
        SystemState fresh = new SystemState();
        restart(fresh);
        assertEquals(ChargingMode.FAST, fresh.mode());
        assertEquals(32, fresh.fastModeAmps());
    }

    @Test
    void unchangedStateIsNotRewrittenButChangesAre() {
        chargingCharger("mywallbox");
        persistence.save(false);
        int afterFirst = writes.get();
        assertEquals(1, afterFirst);

        for (int i = 0; i < 20; i++) {
            clock.addAndGet(5_000);
            persistence.save(false);
        }
        assertEquals(afterFirst, writes.get(), "no rewrite while nothing changes");

        state.setMode(ChargingMode.FAST);
        persistence.save(false);
        assertEquals(afterFirst + 1, writes.get());
    }

    @Test
    void timestampIsRefreshedEveryFewMinutesSoLongChargesStayRestorable() {
        chargingCharger("mywallbox");
        persistence.save(false);

        for (int minutes = 0; minutes < 150; minutes++) {   // 2.5 hours of steady charging
            clock.addAndGet(60_000);
            persistence.save(false);
        }
        assertTrue(restart(new SystemState()).recall("mywallbox").isPresent());
        assertTrue(writes.get() < 60, "should write only occasionally, wrote " + writes.get());
    }

    @Test
    void shutdownSaveRefreshesTimestamps() {
        chargingCharger("mywallbox");
        persistence.save(false);
        clock.addAndGet(50 * 60_000L);
        persistence.save(true);                      // what the shutdown hook does
        clock.addAndGet(30 * 60_000L);               // 80 minutes after the first save

        assertTrue(restart(new SystemState()).recall("mywallbox").isPresent());
    }
}
