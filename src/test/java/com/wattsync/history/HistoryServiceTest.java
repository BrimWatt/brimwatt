package com.wattsync.history;

import static org.junit.jupiter.api.Assertions.*;

import com.wattsync.model.ChargingMode;
import java.time.LocalDate;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

class HistoryServiceTest {

    private static final ZoneId LA = ZoneId.of("America/Los_Angeles");

    private final AtomicLong clock = new AtomicLong();
    private HistoryService history;

    @BeforeEach
    void setUp() {
        clock.set(LocalDate.of(2026, 10, 14).atTime(12, 0).atZone(LA).toEpochSecond() * 1000);
        history = new HistoryService(LA, clock::get);
    }

    private void minute(double solar, double grid, double car) { minute(solar, grid, car, ChargingMode.ECO, false); }

    private void minute(double solar, double grid, double car, ChargingMode mode, boolean paused) {
        clock.addAndGet(60_000);
        history.recordSample(solar, grid, car, mode, paused);
    }

    private LocalDate today() { return history.localDay(clock.get() / 1000); }

    @Test
    void homeIsWhatIsLeftAfterTheCar() {
        minute(3000, 500, 2400);                  // 3000 + 500 - 2400 = 1100
        var s = history.series(0, 60);
        assertEquals(1, s.size());
        assertEquals(1100, s.get(0).home());
        assertEquals(500, s.get(0).grid());
        assertEquals(2400, s.get(0).car());
        assertTrue((s.get(0).flags() & HistoryService.FLAG_CHARGING) != 0);
    }

    @Test
    void energyIsCountedBetweenReadingsAndSplitIntoSolarAndGrid() {
        minute(3000, -500, 2000);
        minute(3000, -500, 2000);
        var t = history.totalsForDay(today());
        assertEquals(3000.0 / 60, t.solarWh, 0.01);
        assertEquals(2000.0 / 60, t.carWh, 0.01);
        assertEquals(2000.0 / 60, t.carSolarWh, 0.01);   // 2500 W of sun is left after the house: the whole car
        assertEquals(500.0 / 60, t.exportWh, 0.01);
        assertEquals(0, t.importWh, 0.01);
    }

    @Test
    void thePartOfTheCarTheSunCannotCoverCountsAsGrid() {
        minute(1000, 1900, 2400);                // home 500: only 500 W of sun is left for the car
        minute(1000, 1900, 2400);
        var t = history.totalsForDay(today());
        assertEquals(500.0 / 60, t.carSolarWh, 0.01);
        assertEquals(2400.0 / 60, t.carWh, 0.01);
        assertEquals(1900.0 / 60, t.importWh, 0.01);
    }

    @Test
    void aLongGapWithoutReadingsIsNotCountedAsEnergy() {
        minute(3000, 0, 0);
        clock.addAndGet(3_600_000);
        history.recordSample(3000, 0, 0, ChargingMode.ECO, false);
        assertEquals(0, history.totalsForDay(today()).solarWh, 0.001);
    }

    @Test
    void energyGoesIntoTheLocalDayNotTheUtcDay() {
        clock.set(LocalDate.of(2026, 10, 14).atTime(23, 59, 30).atZone(LA).toEpochSecond() * 1000);
        history.recordSample(1000, 0, 0, ChargingMode.ECO, false);
        clock.addAndGet(60_000);                  // 00:00:30 on the 15th, local
        history.recordSample(1000, 0, 0, ChargingMode.ECO, false);
        assertEquals(1000.0 / 60, history.totalsForDay(LocalDate.of(2026, 10, 15)).solarWh, 0.01);
        assertEquals(0, history.totalsForDay(LocalDate.of(2026, 10, 14)).solarWh, 0.001);
    }

    @Test
    void theStartOfTheLocalDayIsLocalMidnight() {
        long midnight = LocalDate.of(2026, 10, 14).atStartOfDay(LA).toEpochSecond();
        assertEquals(midnight, history.startOfLocalDay(clock.get() / 1000));
    }

    @Test
    void samplesCanBeAveragedIntoBuckets() {
        long from = clock.get() / 1000 + 60;
        for (int i = 0; i < 10; i++) minute(i < 5 ? 1000 : 3000, 0, 0);
        var buckets = history.series(from, 300);
        assertEquals(2, buckets.size());
        assertEquals(1000, buckets.get(0).solar());
        assertEquals(3000, buckets.get(1).solar());
    }

    @Test
    void aBucketRemembersThatChargingHappenedInIt() {
        long from = clock.get() / 1000;
        minute(2000, 0, 0);
        minute(2000, 0, 2400);
        minute(2000, 0, 0);
        var buckets = history.series(from, 300);
        assertEquals(1, buckets.size());
        assertTrue((buckets.get(0).flags() & HistoryService.FLAG_CHARGING) != 0);
    }

    @Test
    void onlyTheLatestTwoDaysOfMinutesAreKept() {
        for (int i = 0; i < HistoryService.MAX_SAMPLES + 50; i++) minute(1000, 0, 0);
        assertEquals(HistoryService.MAX_SAMPLES, history.minuteSampleCount());
        assertTrue(history.longRecordCount() > 500, "the older minutes live on as 5-minute and hourly records");
    }

    private List<String> longLines(boolean onlyNew) {
        List<String> lines = new ArrayList<>();
        history.forEachLongLine(lines::add, onlyNew);
        return lines;
    }

    @Test
    void minutesAreRolledUpIntoFiveMinuteAndHourlyRecords() {
        for (int i = 0; i < 200; i++) minute(2000, 0, 0);         // a little over three hours
        long five = longLines(false).stream().filter(l -> l.startsWith("M")).count();
        long hourly = longLines(false).stream().filter(l -> l.startsWith("H")).count();
        assertTrue(five >= 30, "five-minute records: " + five);
        assertTrue(hourly >= 2, "hourly records: " + hourly);
    }

    @Test
    void anOldDayComesFromTheCoarserTiers() {
        long from = clock.get() / 1000 + 60;
        for (int i = 0; i < 3 * 1440; i++) minute(i % 2 == 1 ? 1000 : 3000, 0, 0);   // three days
        assertEquals(HistoryService.MAX_SAMPLES, history.minuteSampleCount());
        var firstDay = history.seriesRange(from, from + 86399, 3600);
        assertTrue(firstDay.size() >= 20, "hourly buckets: " + firstDay.size());
        for (var s : firstDay) assertTrue(s.solar() >= 1700 && s.solar() <= 2300, "averaged " + s.solar());
    }

    @Test
    void recentTimeStillUsesTheMinuteSamples() {
        for (int i = 0; i < 30; i++) minute(1000 + i * 10, 0, 0);
        var last = history.seriesRange(clock.get() / 1000 - 600, Long.MAX_VALUE, 60);
        assertEquals(11, last.size());
        assertEquals(1000 + 29 * 10, last.get(last.size() - 1).solar());
    }

    @Test
    void onlyNewLongRecordsAreWrittenWhenAsked() {
        for (int i = 0; i < 70; i++) minute(1000, 0, 0);
        assertEquals(longLines(false).size(), longLines(true).size());   // nothing saved yet
        assertTrue(history.longPending());
        history.markLongSaved();
        assertFalse(history.longPending());
        for (int i = 0; i < 20; i++) minute(1000, 0, 0);
        int later = longLines(true).size();
        assertTrue(later >= 3 && later <= 6, "new records: " + later);
    }

    @Test
    void theLongRecordsSurviveBeingSavedAndLoaded() {
        for (int i = 0; i < 400; i++) minute(1500, -200, 800);
        HistoryService fresh = new HistoryService(LA, clock::get);
        longLines(false).forEach(fresh::loadLine);
        assertEquals(history.longRecordCount(), fresh.longRecordCount());
        assertFalse(fresh.longPending());
    }

    @Test
    void aSessionReportsTheWallboxEnergyAndOurSunShare() {
        minute(3000, 0, 0);
        history.sessionStarted(5_000_000);
        for (int i = 0; i < 60; i++) minute(3000, -500, 2400);     // an hour at 2.4 kW
        history.sessionStopped(5_002_350, "Remote");                // the wallbox's own meter says 2350 Wh
        var done = history.recentSessions(5);
        assertEquals(1, done.size());
        assertEquals(2350, done.get(0).energyWh(), 0.5);
        assertEquals(2400, done.get(0).estimatedWh, 5);
        assertEquals(2400, done.get(0).sunWh, 5);                   // the sun covered all of it
        assertEquals("Remote", done.get(0).reason);
        assertNull(history.openSession());
        assertTrue(history.recentEvents(1).get(0).text().contains("kWh, 100% from the sun"));
    }

    @Test
    void aSessionRemembersItsModeAndWhetherItChanged() {
        minute(2000, 0, 0);
        history.sessionStarted(-1);
        for (int i = 0; i < 5; i++) minute(3000, 0, 1000, ChargingMode.MAX_SOLAR, false);
        assertEquals(1, history.openSession().mode);
        assertFalse(history.openSession().mixed);
        minute(3000, 0, 1000, ChargingMode.ECO, false);
        assertTrue(history.openSession().mixed);
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        assertEquals(1, fresh.openSession().mode);
        assertTrue(fresh.openSession().mixed);
    }

    @Test
    void theEstimateIsUsedWhenTheWallboxGivesNoMeterReadings() {
        minute(2000, 0, 0);
        history.sessionStarted(-1);
        for (int i = 0; i < 30; i++) minute(500, 1900, 2400);       // grid powered: 500 W of sun
        history.sessionStopped(-1, "");
        var done = history.recentSessions(1).get(0);
        assertEquals(1200, done.energyWh(), 5);
        assertEquals(250, done.sunWh, 5);
    }

    @Test
    void aSessionInProgressSurvivesARestart() {
        minute(2000, 0, 0);
        history.sessionStarted(100);
        for (int i = 0; i < 10; i++) minute(3000, 0, 1000);
        assertNotNull(history.openSession());
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);

        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        assertNotNull(fresh.openSession());
        assertEquals(history.openSession().estimatedWh, fresh.openSession().estimatedWh, 0.1);
        clock.addAndGet(60_000);
        fresh.recordSample(3000, 0, 1000, ChargingMode.ECO, false);
        fresh.sessionStopped(600, "EVDisconnected");                // the stop arrives after the restart
        assertEquals(1, fresh.recentSessions(5).size());
        assertEquals(500, fresh.recentSessions(5).get(0).energyWh(), 0.5);
    }

    @Test
    void finishedSessionsSurviveBeingSavedAndLoaded() {
        minute(2000, 0, 0);
        for (int s = 0; s < 3; s++) {
            history.sessionStarted(s * 1000);
            for (int i = 0; i < 20; i++) minute(3000, 0, 1000);
            history.sessionStopped(s * 1000 + 300, "Remote");
        }
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        assertEquals(3, fresh.recentSessions(10).size());
        assertEquals(history.recentSessions(1).get(0).start, fresh.recentSessions(1).get(0).start);
    }

    @Test
    void aStopWithoutAStartIsIgnoredAndANewStartClosesALostOne() {
        minute(1000, 0, 0);
        history.sessionStopped(500, "Remote");
        assertEquals(0, history.recentSessions(5).size());
        history.sessionStarted(0);
        minute(1000, 0, 0);
        history.sessionStarted(10);
        assertEquals(1, history.recentSessions(5).size());
        assertEquals("unknown", history.recentSessions(5).get(0).reason);
        assertNotNull(history.openSession());
    }

    @Test
    void onlyTheLatestFourHundredSessionsAreKept() {
        minute(1000, 0, 0);
        for (int i = 0; i < HistoryService.MAX_SESSIONS + 20; i++) {
            history.sessionStarted(0);
            minute(1000, 0, 500);
            history.sessionStopped(0, "Remote");
        }
        assertEquals(HistoryService.MAX_SESSIONS, history.recentSessions(1000).size());
    }

    @Test
    void aYearOfDaysIsKept() {
        assertTrue(HistoryService.MAX_DAYS >= 366);
    }

    @Test
    void eventsComeBackNewestFirstAndRepeatsAreDropped() {
        history.addEvent("Car plugged in");
        clock.addAndGet(30_000);
        history.addEvent("Car plugged in");
        clock.addAndGet(60_000);
        history.addEvent("Started charging at 10 A");
        var events = history.recentEvents(10);
        assertEquals(2, events.size());
        assertEquals("Started charging at 10 A", events.get(0).text());
        assertEquals("Car plugged in", events.get(1).text());

        clock.addAndGet(300_000);
        history.addEvent("Car plugged in");
        assertEquals(3, history.recentEvents(10).size());
        assertEquals(1, history.recentEvents(1).size());
    }

    @Test
    void goingBackToAnEarlierSettingIsStillLogged() {
        history.addEvent("Mode set to Eco");
        clock.addAndGet(5_000);
        history.addEvent("Mode set to Custom");
        clock.addAndGet(5_000);
        history.addEvent("Mode set to Eco");           // the same line as two events ago, seconds later
        var events = history.recentEvents(10);
        assertEquals(3, events.size());
        assertEquals("Mode set to Eco", events.get(0).text(), "the newest line matches the real state");
    }

    @Test
    void theEventLogKeepsTheLatestFifty() {
        for (int i = 0; i < 70; i++) {
            clock.addAndGet(200_000);
            history.addEvent("event " + i);
        }
        var events = history.recentEvents(100);
        assertEquals(HistoryService.MAX_EVENTS, events.size());
        assertEquals("event 69", events.get(0).text());
    }

    @Test
    void everythingSurvivesBeingSavedAndLoaded() {
        minute(3000, -500, 2000);
        minute(3100, -400, 2000, ChargingMode.MAX_SOLAR, true);
        history.addEvent("Mode set to Balanced");
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);

        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        var samples = fresh.series(0, 60);
        assertEquals(2, samples.size());
        assertEquals(3100, samples.get(1).solar());
        assertEquals(1, samples.get(1).mode());
        assertTrue((samples.get(1).flags() & HistoryService.FLAG_PAUSED) != 0);
        assertEquals("Mode set to Balanced", fresh.recentEvents(5).get(0).text());
        assertEquals(history.totalsForDay(today()).solarWh, fresh.totalsForDay(today()).solarWh, 0.1);
    }

    @Test
    void loadingGarbageDoesNoHarm() {
        for (String line : new String[] {"", "X\t1", "S\tbroken", "S\t0\t1\t2\t3\t4\t5\t6", "E", "D\t1\t2", "\t\t\t"}) {
            history.loadLine(line);
        }
        assertEquals(0, history.series(0, 60).size());
        assertEquals(0, history.recentEvents(5).size());
    }

    @Test
    void dirtyTracksUnsavedChanges() {
        assertFalse(history.dirty());
        minute(1000, 0, 0);
        assertTrue(history.dirty());
        history.markClean();
        assertFalse(history.dirty());
    }

    @Test
    void eventsAndSessionsCanBeReadOnePageAtATimeNewestFirst() {
        for (int i = 1; i <= 12; i++) {
            clock.addAndGet(300_000);
            history.addEvent("Event " + i);
        }
        assertEquals(12, history.eventCount());
        assertEquals("Event 12", history.recentEvents(5, 0).get(0).text());
        assertEquals("Event 7", history.recentEvents(5, 5).get(0).text());
        assertEquals(2, history.recentEvents(5, 10).size());
        assertEquals("Event 1", history.recentEvents(5, 10).get(1).text());
        assertEquals(0, history.recentEvents(5, 50).size());

        for (int i = 1; i <= 7; i++) {
            clock.addAndGet(600_000);
            history.sessionStarted(i * 1000);
            clock.addAndGet(60_000);
            history.sessionStopped(i * 1000 + 100, "Remote");
        }
        assertEquals(7, history.sessionCount());
        assertEquals(2, history.recentSessions(5, 5).size());
        assertTrue(history.recentSessions(5, 5).get(0).start > history.recentSessions(5, 5).get(1).start);
    }

    @Test
    void moneySavedUsesThePriceOfTheHourTheEnergyWasCharged() {
        history.setPriceProvider(t -> java.time.Instant.ofEpochSecond(t).atZone(LA).getHour() >= 16 ? 60.0 : 30.0);
        history.sessionStarted(0);
        minute(5000, -2000, 2000);                 // 12:01, the whole car is covered by the sun, normal price
        minute(5000, -2000, 2000);                 // 12:02: one minute of sun-charging counts at the normal price
        clock.set(LocalDate.of(2026, 10, 14).atTime(17, 0).atZone(LA).toEpochSecond() * 1000);
        history.recordSample(5000, -2000, 2000, ChargingMode.ECO, false);   // a gap: nothing is counted across it
        minute(5000, -2000, 2000);                 // 17:01, peak price
        minute(5000, -2000, 2000);                 // 17:02: one minute of sun-charging counts at the peak price
        double oneMinuteKwh = 2000.0 / 60 / 1000;
        var t = history.totalsForDay(today());
        assertEquals(oneMinuteKwh * 30 + oneMinuteKwh * 60 * 2, t.savedCents, 0.01, "12:02 at 30 c, then 17:01 and 17:02 at 60 c");
        assertEquals(t.savedCents, history.openSession().savedCents, 0.0001);
    }

    @Test
    void moneySavedSurvivesBeingSavedAndLoadedAndOlderLinesStillLoad() {
        history.setPriceProvider(t -> 40.0);
        history.sessionStarted(0);
        minute(5000, -2000, 2000);
        minute(5000, -2000, 2000);
        java.util.List<String> lines = new java.util.ArrayList<>();
        history.forEachLine(lines::add);

        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        assertEquals(history.totalsForDay(today()).savedCents, fresh.totalsForDay(today()).savedCents, 0.01);
        assertEquals(history.openSession().savedCents, fresh.openSession().savedCents, 0.01);

        HistoryService old = new HistoryService(LA, clock::get);
        old.loadLine("D\t" + today().toEpochDay() + "\t1000.0\t500.0\t400.0\t100.0\t200.0");          // seven columns: before the money column
        long nowS = clock.get() / 1000;
        old.loadLine("T\t" + (nowS - 1000) + "\t" + (nowS - 900) + "\t-1.0\t-1.0\t300.0\t250.0\tRemote");   // a session without it (from the last 30 days)
        assertEquals(0, old.totalsForDay(today()).savedCents);
        assertEquals(1, old.recentSessions(5).size());
        assertEquals(0, old.recentSessions(5).get(0).savedCents);
    }

    @Test
    void aSessionRemembersWhenTheCarWasPluggedIn() {
        minute(2000, 0, 0);
        history.notePluggedIn();
        long pluggedAt = clock.get() / 1000;
        for (int i = 0; i < 90; i++) minute(2000, 0, 0);
        history.sessionStarted(0);
        for (int i = 0; i < 10; i++) minute(3000, 0, 1000);
        assertEquals(pluggedAt, history.openSession().pluggedAt);
        history.sessionStopped(100, "Remote");
        assertEquals(pluggedAt, history.recentSessions(1).get(0).pluggedAt);
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        assertEquals(pluggedAt, fresh.recentSessions(1).get(0).pluggedAt);
    }

    @Test
    void aSessionWithNoPlugInSeenOrAnUnpluggedCarHasNoPlugInTime() {
        minute(2000, 0, 0);
        history.sessionStarted(0);
        assertEquals(0, history.openSession().pluggedAt);
        history.sessionStopped(0, "Remote");
        history.notePluggedIn();
        history.notePluggedOut();
        history.sessionStarted(0);
        assertEquals(0, history.openSession().pluggedAt);
    }

    @Test
    void aStalePlugInTimeIsIgnored() {
        minute(2000, 0, 0);
        history.notePluggedIn();
        clock.addAndGet(4L * 86400 * 1000);
        history.sessionStarted(0);
        assertEquals(0, history.openSession().pluggedAt);
    }

    @Test
    void aGoalResultIsKeptPerDaySurvivesSavingAndOldFilesStillLoad() {
        minute(2000, 0, 0);
        long deadline = clock.get() / 1000;
        history.recordGoalResult(deadline, true, 20000, 21500);
        assertEquals(1, history.goalOutcome(today()));
        assertEquals(0, history.goalOutcome(today().minusDays(1)));
        history.recordGoalResult(deadline - 86400, false, 20000, 9000);
        assertEquals(2, history.goalOutcome(today().minusDays(1)));
        assertTrue(history.recentEvents(5).stream().anyMatch(e -> e.text().startsWith("Departure goal met: 21.5 of 20.0 kWh")));
        assertTrue(history.recentEvents(5).stream().anyMatch(e -> e.text().startsWith("Departure goal missed")));
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        assertTrue(lines.stream().anyMatch(l -> l.startsWith("G\t")));
        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        assertEquals(1, fresh.goalOutcome(today()));
        assertEquals(2, fresh.goalOutcome(today().minusDays(1)));
        HistoryService old = new HistoryService(LA, clock::get);
        lines.stream().filter(l -> !l.startsWith("G\t")).forEach(old::loadLine);   // a file from before this existed
        assertEquals(0, old.goalOutcome(today()));
    }

    @Test
    void aJournalCarriesRecentGoalResults() {
        minute(2000, 0, 0);
        history.markClean();
        minute(2000, 0, 0);
        history.recordGoalResult(clock.get() / 1000, true, 20000, 22000);
        List<String> lines = new ArrayList<>();
        history.forEachJournalLine(lines::add);
        assertTrue(lines.stream().anyMatch(l -> l.startsWith("G\t")));
    }

    private long countLines(String prefix) {
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        return lines.stream().filter(l -> l.startsWith(prefix)).count();
    }

    @Test
    void activityLinesOlderThanADayLeaveTheListAtOnceAndAreRemovedLater() {
        minute(1000, 0, 0);
        long t0 = clock.get();
        history.addEvent("old");
        clock.set(t0 + 3 * 3_600_000L);
        history.addEvent("middle");
        clock.set(t0 + 23 * 3_600_000L);
        history.addEvent("recent");
        assertEquals(3, history.eventCount());
        clock.set(t0 + 25 * 3_600_000L);                       // "old" is 25 hours old; nothing recorded since
        assertEquals(2, history.eventCount());
        assertEquals(2, history.recentEvents(10).size());
        assertEquals("recent", history.recentEvents(10).get(0).text());
        assertEquals(1, history.recentEvents(10, 1).size());   // paging counts only what is shown
        minute(1000, 0, 0);                                    // the once-a-minute purge removes it for good
        long lines = countLines("E\t");
        assertEquals(2, lines);
        clock.set(t0 + 60 * 3_600_000L);
        minute(1000, 0, 0);
        assertEquals(0, history.eventCount());
    }

    @Test
    void finishedSessionsOlderThanThirtyDaysAreDropped() {
        minute(1000, 0, 0);
        history.sessionStarted(0);
        minute(1000, 0, 500);
        history.sessionStopped(0, "Remote");
        long t0 = clock.get();
        clock.set(t0 + 20 * 86_400_000L);
        minute(1000, 0, 0);
        history.sessionStarted(0);
        minute(1000, 0, 500);
        history.sessionStopped(0, "Remote");
        assertEquals(2, history.sessionCount());
        clock.set(t0 + 31 * 86_400_000L);
        assertEquals(1, history.sessionCount());
        assertEquals(1, history.recentSessions(10).size());
        minute(1000, 0, 0);
        assertEquals(1, countLines("T\t"));
        clock.set(t0 + 60 * 86_400_000L);
        minute(1000, 0, 0);
        assertEquals(0, history.sessionCount());
    }

    @Test
    void purgingNeverTouchesTheGraphData() {
        // Two identical runs of 45 days; one also logs events and sessions all along. Their graph data must be identical.
        HistoryService withLog = history;
        AtomicLong otherClock = new AtomicLong(clock.get());
        HistoryService plain = new HistoryService(LA, otherClock::get);
        for (int step = 0; step < 45 * 288; step++) {
            double sun = (step % 288) > 80 && (step % 288) < 220 ? 4000 : 0;
            clock.addAndGet(300_000); otherClock.addAndGet(300_000);
            withLog.recordSample(sun, -500, sun > 0 ? 2000 : 0, ChargingMode.ECO, false);
            plain.recordSample(sun, -500, sun > 0 ? 2000 : 0, ChargingMode.ECO, false);
            if (step % 20 == 0) withLog.addEvent("event " + step);
            if (step % 288 == 100) withLog.sessionStarted(0);
            if (step % 288 == 200) withLog.sessionStopped(0, "Remote");
        }
        assertTrue(withLog.eventCount() < 20);
        assertTrue(withLog.sessionCount() <= 31);
        assertEquals(0, plain.sessionCount());
        assertEquals(plain.allDays().size(), withLog.allDays().size());
        for (var e : plain.allDays().entrySet()) {
            var a = e.getValue(); var b = withLog.allDays().get(e.getKey());
            assertEquals(a.solarWh, b.solarWh, 0.01);
            assertEquals(a.carWh, b.carWh, 0.01);
            assertEquals(a.importWh, b.importWh, 0.01);
            assertEquals(a.exportWh, b.exportWh, 0.01);
        }
        for (int step : new int[] {60, 300, 3600}) {
            var a = plain.series(0, step); var b = withLog.series(0, step);
            assertEquals(a.size(), b.size());
            for (int i = 0; i < a.size(); i++) assertEquals(a.get(i).solar(), b.get(i).solar(), 0.01);
        }
    }

    @Test
    void repeatedLimitChangesBecomeOneLineThatCountsThemAndShowsTheLatest() {
        minute(1000, 0, 0);
        List<String> heard = new ArrayList<>();
        history.setEventListener(heard::add);
        history.addEvent("Changed limit to 9 A: less solar left");
        assertEquals("Changed limit to 9 A: less solar left", history.recentEvents(1).get(0).text());   // one change stays as it is
        for (String s : new String[] {"Changed limit to 10 A: more solar", "Changed limit to 8 A: less solar left", "Changed limit to 13 A: more solar"}) {
            clock.addAndGet(600_000);
            history.addEvent(s);
        }
        assertEquals(1, history.eventCount());
        assertEquals("Changed limit 4 times, now 13 A: more solar", history.recentEvents(5).get(0).text());
        assertEquals(clock.get() / 1000, history.recentEvents(1).get(0).t());       // the time of the latest change
        assertEquals(4, heard.size());                                            // the messages feature still hears every real change
        assertEquals("Changed limit to 13 A: more solar", heard.get(3));
    }

    @Test
    void anythingElseInBetweenOrAGapOfAnHourStartsANewLine() {
        minute(1000, 0, 0);
        history.addEvent("Changed limit to 9 A: more solar");
        clock.addAndGet(60_000); history.addEvent("Changed limit to 10 A: more solar");
        clock.addAndGet(60_000); history.addEvent("Car plugged in");
        clock.addAndGet(60_000); history.addEvent("Changed limit to 11 A: more solar");
        clock.addAndGet(3_600_000); history.addEvent("Changed limit to 12 A: more solar");
        var events = history.recentEvents(10);
        assertEquals(4, events.size());
        assertEquals("Changed limit to 12 A: more solar", events.get(0).text());
        assertEquals("Changed limit to 11 A: more solar", events.get(1).text());
        assertEquals("Car plugged in", events.get(2).text());
        assertEquals("Changed limit 2 times, now 10 A: more solar", events.get(3).text());
    }

    @Test
    void theWallboxAndTheCarLimitLinesNeverMergeAndAMissingReasonIsFine() {
        minute(1000, 0, 0);
        history.addEvent("Changed limit to 9 A");
        clock.addAndGet(60_000); history.addEvent("Car: changed limit to 10 A: more solar");
        clock.addAndGet(60_000); history.addEvent("Car: changed limit to 11 A: more solar");
        clock.addAndGet(60_000); history.addEvent("Car: changed limit to 12 A");
        var events = history.recentEvents(10);
        assertEquals(2, events.size());
        assertEquals("Car: changed limit 3 times, now 12 A", events.get(0).text());
        assertEquals("Changed limit to 9 A", events.get(1).text());
        clock.addAndGet(60_000); history.addEvent("Changed limit to abc A: odd");               // an unreadable line is just a line
        assertEquals(3, history.eventCount());
    }

    @Test
    void aMergedLimitLineSurvivesSavingAndLoading() {
        minute(1000, 0, 0);
        history.addEvent("Changed limit to 9 A: more solar");
        clock.addAndGet(60_000); history.addEvent("Changed limit to 10 A: more solar");
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        HistoryService fresh = new HistoryService(LA, clock::get);
        lines.forEach(fresh::loadLine);
        assertEquals("Changed limit 2 times, now 10 A: more solar", fresh.recentEvents(1).get(0).text());
        clock.addAndGet(60_000); fresh.addEvent("Changed limit to 8 A: less solar left");      // and the run carries on after a restart
        assertEquals("Changed limit 3 times, now 8 A: less solar left", fresh.recentEvents(1).get(0).text());
    }

    @Test
    void savedLimitLinesFromBeforeTheMergeCollapseOnLoadAndAnOverlappingJournalDoesNotDoubleThem() {
        minute(1000, 0, 0);
        long t = clock.get() / 1000;
        HistoryService a = new HistoryService(LA, clock::get);
        for (int i = 0; i < 4; i++) a.loadLine("E\t" + (t + i * 300) + "\tChanged limit to " + (9 + i) + " A: more solar");   // an old file: four plain lines
        a.loadLine("E\t" + (t + 1500) + "\tCar plugged in");
        var events = a.recentEvents(10);
        assertEquals(2, events.size());
        assertEquals("Changed limit 4 times, now 12 A: more solar", events.get(1).text());
        clock.addAndGet(5_000_000);
        HistoryService b = new HistoryService(LA, clock::get);
        b.loadLine("E\t" + t + "\tChanged limit 2 times, now 10 A: more solar");
        b.loadLine("E\t" + (t + 400) + "\tChanged limit 3 times, now 8 A: less solar left");     // the journal's newer snapshot of the same run
        assertEquals(1, b.eventCount());
        assertEquals("Changed limit 3 times, now 8 A: less solar left", b.recentEvents(1).get(0).text());
        b.loadLine("E\t" + (t + 400) + "\tChanged limit 3 times, now 8 A: less solar left");   // the very same line twice
        assertEquals(1, b.eventCount());
    }
}
