package com.example.history;

import static org.junit.jupiter.api.Assertions.*;

import com.example.model.ChargingMode;
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
        old.loadLine("T\t100\t200\t-1.0\t-1.0\t300.0\t250.0\tRemote");                                 // a session without it
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
}
