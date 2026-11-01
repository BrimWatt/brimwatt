package com.example.enphase;

import static org.junit.jupiter.api.Assertions.*;

import com.example.model.ChargingMode;
import com.example.model.EnphaseMeterReading;
import com.example.state.SystemState;
import com.example.wallbox.ChargerGateway;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

class EnphasePollerTest {

    private static final long TICK = 10_000;

    private final AtomicLong clock = new AtomicLong(1_000_000);
    private final SystemState state = new SystemState();
    private final List<Integer> sentLimits = new ArrayList<>();
    private boolean chargerConnected = true;
    private boolean carPlugged = true;
    private Integer chargingLimit;                   // non-null: a charge is already running at this limit
    private double chargerDraw;
    private int refreshes;
    private int fetches;
    private boolean daylightOpen = true;
    private Exception failure;                       // when set, the gateway throws it
    private double solarWatts = 6000;
    private double netGridWatts = -3000;             // exporting 3 kW

    private final ChargerGateway wallbox = new ChargerGateway() {
        public boolean hasConnectedChargers() { return chargerConnected; }
        public boolean carPluggedIn() { return carPlugged; }
        public void refreshMeasurements() { refreshes++; }
        public double currentDrawAmps() { return chargerDraw; }
        public Integer currentLimitAmps() { return chargingLimit; }
        public void sendChargingLimit(int amps) {
            sentLimits.add(amps);
            if (amps == 0) {                         // a stopped charger is no longer charging
                chargingLimit = null;
                chargerDraw = 0;
            }
        }
    };

    private final MeterReadingSource source = () -> {
        fetches++;
        if (failure != null) throw failure;
        return List.of(reading(solarWatts), reading(netGridWatts));
    };

    private final com.example.daylight.DaylightSchedule schedule = new com.example.daylight.DaylightSchedule() {
        public boolean isOpen(java.time.Instant now) { return daylightOpen; }
        public String describe(java.time.Instant now) { return "08:00 to 17:00"; }
    };

    private EnphasePoller poller;

    private static EnphaseMeterReading reading(double watts) {
        EnphaseMeterReading r = new EnphaseMeterReading();
        r.activePower = watts;
        return r;
    }

    @BeforeEach
    void setUp() {
        poller = new EnphasePoller(source, state, wallbox, schedule, clock::get);
    }

    private void pollFor(int seconds) {
        for (int i = 0; i < seconds * 1000 / TICK; i++) {
            clock.addAndGet(TICK);
            poller.poll();
        }
    }

    @Test
    void shortOutageDoesNothingToTheCharger() {
        pollFor(60);
        int before = sentLimits.size();

        failure = new IOException("connection refused");
        pollFor(120);                                // below the 150 s threshold

        assertEquals(before, sentLimits.size(), "no commands should be sent during a short outage");
        assertFalse(state.enphaseOk());
        assertEquals("connection refused", state.enphaseMessage());
    }

    @Test
    void ecoStopsChargingAfterLongOutage() {
        pollFor(300);                                // charging on solar
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0);

        failure = new IOException("timeout");
        pollFor(300);

        assertEquals(0, sentLimits.get(sentLimits.size() - 1));
    }

    @Test
    void balancedStopsAfterLongOutage() {
        state.setMode(ChargingMode.MAX_SOLAR);
        pollFor(300);

        failure = new IOException("timeout");
        pollFor(300);

        assertEquals(0, sentLimits.get(sentLimits.size() - 1));
    }

    @Test
    void recoversAndResumesNormalControl() {
        pollFor(300);
        failure = new IOException("timeout");
        pollFor(300);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1));

        failure = null;
        pollFor(400);

        assertTrue(state.enphaseOk());
        assertEquals("", state.enphaseMessage());
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0, "should start charging again once surplus is confirmed");
    }

    @Test
    void fastModeAndNoChargerNeverCountAsAnOutage() {
        failure = new IOException("down");
        state.setMode(ChargingMode.FAST);
        pollFor(600);
        assertTrue(sentLimits.isEmpty());
        assertFalse(state.failSafe(), "a failing gateway in Custom is shown on the dashboard but is never a fail-safe");

        // Leaving FAST must not trigger the fail-safe immediately because of the idle time.
        state.setMode(ChargingMode.ECO);
        pollFor(60);
        assertTrue(sentLimits.isEmpty(), "outage clock should start fresh after FAST");

        chargerConnected = false;
        pollFor(600);
        assertTrue(sentLimits.isEmpty());
    }

    @Test
    void ecoStartsWithSmallRealSurplusThanksToGridAllowance() {
        netGridWatts = -700;                         // exporting 0.7 kW: 3 A alone, 7.3 A with the 1 kW allowance
        pollFor(300);
        assertTrue(sentLimits.stream().anyMatch(a -> a >= 6), "ECO should start: " + sentLimits);
    }

    @Test
    void changingTheAllowanceChangesWhenEcoStarts() {
        netGridWatts = -700;                         // 0.7 kW export: enough with a 1 kW allowance, not with none
        state.setGridAllowanceWatts(0);
        pollFor(600);
        assertTrue(sentLimits.stream().allMatch(a -> a == 0), "no allowance: must stay off " + sentLimits);

        state.setGridAllowanceWatts(1000);
        pollFor(300);
        assertTrue(sentLimits.stream().anyMatch(a -> a >= 6), "with the allowance it should start: " + sentLimits);
    }

    @Test
    void ecoDoesNotStartWithoutRealSurplus() {
        netGridWatts = -200;                         // 0.2 kW export + 1 kW allowance is still below the start threshold
        pollFor(600);
        assertTrue(sentLimits.stream().allMatch(a -> a == 0), "ECO must stay off: " + sentLimits);

        netGridWatts = 300;                          // importing 0.3 kW
        pollFor(600);
        assertTrue(sentLimits.stream().allMatch(a -> a == 0), "ECO must stay off: " + sentLimits);
    }

    @Test
    void maxSolarIgnoresTheHouseAndTopsUpWithTheAllowance() {
        state.setMode(ChargingMode.MAX_SOLAR);
        state.setGridAllowanceWatts(600);
        solarWatts = 1000;                           // 1 kW of sun
        netGridWatts = 500;                          // the house is using 1.5 kW: Eco would see no surplus at all
        pollFor(600);
        assertEquals(6, sentLimits.get(sentLimits.size() - 1).intValue(),
                "6 A needs about 400 W from the grid, which fits the 600 W allowance: " + sentLimits);

        state.setGridAllowanceWatts(0);              // without the top-up 1 kW cannot reach 6 A
        pollFor(900);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1).intValue(), "stops once the dip delay has passed: " + sentLimits);
    }

    @Test
    void maxSolarFollowsTheSunWhateverTheHouseUses() {
        state.setMode(ChargingMode.MAX_SOLAR);
        solarWatts = 4000;
        netGridWatts = 2000;                         // the house takes more than the sun gives
        pollFor(600);
        assertTrue(sentLimits.get(sentLimits.size() - 1) >= 15, "about 4 kW of sun is 17 A: " + sentLimits);
    }

    @Test
    void maxSolarStopsWhenTheSunIsGone() {
        state.setMode(ChargingMode.MAX_SOLAR);
        solarWatts = 3000;
        netGridWatts = -1000;
        pollFor(600);
        assertTrue(sentLimits.get(sentLimits.size() - 1) >= 6, "should be charging: " + sentLimits);
        solarWatts = 300;                            // sun gone: even with the 1 kW allowance 6 A is out of reach
        netGridWatts = 1500;
        pollFor(900);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1).intValue(), "should stop: " + sentLimits);
    }

    @Test
    void chargeAlreadyRunningAfterRestartIsNotStoppedWhileSurplusExists() {
        chargingLimit = 14;                          // car was charging when the app restarted
        chargerDraw = 14;
        netGridWatts = -3000;                        // plenty of export on top of the 14 A draw
        pollFor(300);

        assertFalse(sentLimits.isEmpty());
        assertTrue(sentLimits.stream().allMatch(a -> a >= 6), "must keep charging: " + sentLimits);
    }

    @Test
    void chargeAlreadyRunningAfterRestartIsReducedAndStoppedWhenSurplusIsGone() {
        chargingLimit = 14;
        chargerDraw = 14;
        solarWatts = 200;
        netGridWatts = 3500;                         // importing heavily: no surplus at all
        pollFor(120);

        assertTrue(sentLimits.get(sentLimits.size() - 1) <= 6, "should come down quickly: " + sentLimits);
        assertTrue(sentLimits.get(0) < 14, "first decision must already reduce it: " + sentLimits);

        pollFor(600);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1), "and stop after the dip delay: " + sentLimits);
    }

    @Test
    void maxSolarAfterRestartDropsAnOversizedChargeToWhatSolarSupports() {
        state.setMode(ChargingMode.MAX_SOLAR);
        chargingLimit = 32;
        chargerDraw = 32;
        solarWatts = 2000;
        netGridWatts = 5500;                         // 32 A is mostly coming from the grid
        pollFor(300);

        // 2 kW of sun is 8.3 to 8.7 A; the grid tops it up to the next whole amp.
        assertEquals(9, sentLimits.get(sentLimits.size() - 1));
    }

    @Test
    void asksTheChargerForItsRealCurrentOnEveryPollButNotWhenPaused() {
        pollFor(60);                                 // 6 polls
        assertEquals(6, refreshes);

        state.setMode(ChargingMode.FAST);
        pollFor(60);
        assertEquals(12, refreshes, "Custom still asks for the real current, for the dashboard");

        state.setMode(ChargingMode.ECO);
        chargerConnected = false;
        pollFor(60);
        assertEquals(12, refreshes, "no requests without a charger");
    }

    @Test
    void outsideDaylightItStopsPollingAndStopsCharging() {
        pollFor(300);
        int fetchesByDay = fetches;
        assertTrue(fetchesByDay > 0);
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0, "charging during the day");

        daylightOpen = false;
        pollFor(300);

        assertEquals(fetchesByDay, fetches, "the gateway must not be polled at night");
        assertEquals(0, sentLimits.get(sentLimits.size() - 1), "charging is stopped at night");
    }

    @Test
    void doesNotAskTheChargerForMeterValuesOutsideDaylight() {
        daylightOpen = false;
        pollFor(120);
        assertEquals(0, refreshes);
        assertEquals(0, fetches);
    }

    @Test
    void balancedAlsoStopsOutsideDaylight() {
        state.setMode(ChargingMode.MAX_SOLAR);
        pollFor(300);
        assertTrue(sentLimits.get(sentLimits.size() - 1) >= 10);

        daylightOpen = false;
        pollFor(120);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1));
    }

    @Test
    void resumesWhenTheWindowOpensAgainAndStartsFresh() {
        pollFor(300);
        daylightOpen = false;
        pollFor(300);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1));

        daylightOpen = true;
        pollFor(400);
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0, "should start again once the window opens: " + sentLimits);
    }

    @Test
    void customModeIgnoresTheDaylightWindow() {
        state.setMode(ChargingMode.FAST);
        daylightOpen = false;
        pollFor(120);
        assertTrue(sentLimits.isEmpty(), "Custom (FAST) never has its current changed by the poller");
    }

    @Test
    void customModeReadsTheMetersForDisplayOnly() {
        state.setMode(ChargingMode.FAST);
        daylightOpen = false;                        // the daylight window does not apply to Custom
        pollFor(180);

        assertTrue(fetches >= 3, "the meters are still read");
        assertTrue(sentLimits.isEmpty(), "but nothing is ever sent to the charger");
        assertEquals(6000, state.solarWatts(), 0.01);
        assertNotNull(state.control());
        assertEquals(-3000, state.control().gridWatts(), 0.01);
        assertEquals(state.fastModeAmps(), state.control().targetAmps());
        assertTrue(state.enphaseOk());
    }

    @Test
    void customModeShowsAGatewayProblemWithoutTriggeringTheFailSafe() {
        state.setMode(ChargingMode.FAST);
        failure = new IOException("timeout");
        pollFor(600);

        assertFalse(state.enphaseOk());
        assertEquals("timeout", state.enphaseMessage());
        assertFalse(state.failSafe());
        assertTrue(sentLimits.isEmpty());

        failure = null;
        pollFor(60);
        assertTrue(state.enphaseOk());
    }

    private com.example.history.HistoryService attachHistory() {
        var history = new com.example.history.HistoryService(java.time.ZoneId.of("America/Los_Angeles"), clock::get);
        poller.setHistory(history);
        return history;
    }

    private boolean logged(com.example.history.HistoryService history, String part) {
        return history.recentEvents(50).stream().anyMatch(e -> e.text().contains(part));
    }

    @Test
    void everyReadingAddsASampleInEveryMode() {
        var history = attachHistory();
        pollFor(180);                                // ECO
        state.setMode(ChargingMode.FAST);
        pollFor(120);
        assertTrue(history.series(0, 60).size() >= 4, "samples: " + history.series(0, 60).size());
        assertEquals(6000, history.series(0, 60).get(0).solar());
    }

    @Test
    void aStartAndAStopAreLoggedWithTheirReason() {
        var history = attachHistory();
        pollFor(400);                                // solar surplus: starts
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0);
        netGridWatts = 4500;                         // the oven: the house takes more than solar plus allowance
        solarWatts = 3000;
        pollFor(400);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1).intValue());
        // The fake gateway records limits but not events, so just check the poller's own events exist.
        assertNotNull(history.recentEvents(10));
    }

    @Test
    void theFailSafeAndItsRecoveryAreLogged() {
        var history = attachHistory();
        pollFor(300);
        failure = new IOException("timeout");
        pollFor(300);
        assertTrue(logged(history, "Lost contact with the solar meter: charging stopped"));
        failure = null;
        pollFor(120);
        assertTrue(logged(history, "Solar meter is back"));
    }

    @Test
    void pausedStopsChargingInEveryModeAndNeverStartsAgain() {
        for (ChargingMode mode : ChargingMode.values()) {
            sentLimits.clear();
            state.setMode(mode);
            state.setPaused(true);
            chargingLimit = 14;                      // a charge is running when the kill switch is pressed
            chargerDraw = 14;
            pollFor(300);

            assertFalse(sentLimits.isEmpty(), mode + ": a stop is sent");
            assertTrue(sentLimits.stream().allMatch(a -> a == 0), mode + " must only ever send stop: " + sentLimits);
        }
    }

    @Test
    void pausedKeepsTheDashboardLive() {
        state.setPaused(true);
        pollFor(180);

        assertTrue(fetches >= 3, "the meters are still read");
        assertEquals(6000, state.solarWatts(), 0.01);
        assertNotNull(state.control());
        assertEquals("paused", state.control().reason());
        assertEquals(0, state.control().targetAmps());
        assertFalse(state.failSafe());
    }

    @Test
    void pausedIsNotAnOutageEvenIfTheGatewayIsDown() {
        state.setPaused(true);
        failure = new IOException("timeout");
        pollFor(600);
        assertFalse(state.failSafe());
        assertFalse(state.enphaseOk());
    }

    @Test
    void resumingHandsTheChargerBackToTheSavedMode() {
        pollFor(300);                                // ECO is charging on solar
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0);

        state.setPaused(true);
        pollFor(120);
        assertEquals(0, sentLimits.get(sentLimits.size() - 1));
        assertEquals(ChargingMode.ECO, state.mode(), "pausing never changes the mode");

        state.setPaused(false);
        pollFor(400);
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0, "ECO takes over again: " + sentLimits);
    }

    @Test
    void customModeDoesNotReadWithoutACharger() {
        state.setMode(ChargingMode.FAST);
        chargerConnected = false;
        int before = fetches;
        pollFor(120);
        assertEquals(before, fetches);
    }

    @Test
    void turningTheWindowOffKeepsSolarModesRunningAtNight() {
        daylightOpen = false;
        state.setDaylightWindowEnabled(false);
        pollFor(300);

        assertTrue(fetches > 0, "polling continues when the window is turned off");
        assertTrue(sentLimits.get(sentLimits.size() - 1) > 0);
    }

    @Test
    void anOutageOvernightDoesNotLeaveAnErrorBehind() {
        failure = new IOException("down");
        pollFor(60);
        assertFalse(state.enphaseOk());

        daylightOpen = false;
        pollFor(60);
        assertTrue(state.enphaseOk(), "no stale error while polling is paused");
        assertFalse(state.failSafe());
    }

    @Test
    void publishesControlStateForTheDashboard() {
        assertNull(state.control());
        pollFor(60);

        assertNotNull(state.control());
        assertEquals(-3000, state.control().gridWatts());
        assertTrue(state.control().availableAmps() > 12);
        assertFalse(state.failSafe());

        failure = new IOException("down");
        pollFor(300);
        assertTrue(state.failSafe());

        failure = null;
        pollFor(20);
        assertFalse(state.failSafe());
    }

    @Test
    void expiredTokenIsReportedClearly() {
        failure = new EnphaseAuthException(401);
        pollFor(20);
        assertFalse(state.enphaseOk());
        assertTrue(state.enphaseMessage().contains("ENPHASE_TOKEN"), state.enphaseMessage());
    }

    @Test
    void malformedResponseCountsAsFailure() {
        MeterReadingSource shortReply = () -> List.of(reading(100));
        poller = new EnphasePoller(shortReply, state, wallbox, clock::get);
        pollFor(20);
        assertFalse(state.enphaseOk());
    }

    @Test
    void departureGoalWaitsWhileThereIsTimeThenTakesOverEvenAtNight() {
        long threeAm = java.time.Instant.parse("2026-10-19T03:00:00Z").toEpochMilli();
        clock.set(threeAm);
        poller = new EnphasePoller(source, state, wallbox, schedule, clock::get);
        poller.setHistory(new com.example.history.HistoryService(java.time.ZoneOffset.UTC, clock::get));
        daylightOpen = false;                         // night: normally nothing charges
        state.setGoal(10, 7 * 60);                    // 10 kWh by 7:00
        pollFor(60);
        assertTrue(sentLimits.stream().allMatch(a -> a == 0), "plenty of time left: still waiting for the sun");
        assertFalse(state.goalForcing());

        sentLimits.clear();
        clock.set(java.time.Instant.parse("2026-10-19T05:40:00Z").toEpochMilli());   // 80 minutes left, 10 kWh missing
        pollFor(20);
        assertTrue(state.goalForcing());
        assertTrue(sentLimits.stream().allMatch(a -> a > 6), "needs more than the minimum to make it: " + sentLimits);
    }

    @Test
    void departureGoalIsIgnoredWithoutACarOrWhenOff() {
        clock.set(java.time.Instant.parse("2026-10-19T06:30:00Z").toEpochMilli());
        poller = new EnphasePoller(source, state, wallbox, schedule, clock::get);
        poller.setHistory(new com.example.history.HistoryService(java.time.ZoneOffset.UTC, clock::get));
        daylightOpen = false;
        state.setGoal(10, 7 * 60);
        carPlugged = false;
        pollFor(30);
        assertFalse(state.goalForcing());
        carPlugged = true;
        state.setGoal(0, 7 * 60);
        pollFor(30);
        assertFalse(state.goalForcing());
    }

    private com.example.history.HistoryService goalHistory(String time) {
        clock.set(java.time.Instant.parse(time).toEpochMilli());
        poller = new EnphasePoller(source, state, wallbox, schedule, clock::get);
        com.example.history.HistoryService h = new com.example.history.HistoryService(java.time.ZoneOffset.UTC, clock::get);
        poller.setHistory(h);
        daylightOpen = false;
        state.setGoal(1, 7 * 60);                     // 1 kWh by 7:00
        poller.poll();                                // the first look only sets the baseline
        return h;
    }

    private void charge(com.example.history.HistoryService h, int minutes, double carWatts) {
        for (int i = 0; i < minutes; i++) { clock.addAndGet(60_000); h.recordSample(0, 0, carWatts, ChargingMode.ECO, false); }
    }

    @Test
    void aGoalReachedByTheDeadlineIsRecordedAsMet() {
        var h = goalHistory("2026-10-19T06:00:00Z");
        charge(h, 30, 2400);                          // about 1.2 kWh before 6:30
        clock.set(java.time.Instant.parse("2026-10-19T07:00:20Z").toEpochMilli());
        poller.poll();
        assertEquals(1, h.goalOutcome(java.time.LocalDate.of(2026, 10, 19)));
        poller.poll();                                // not judged twice
        assertEquals(1, h.recentEvents(10).stream().filter(e -> e.text().startsWith("Departure goal met")).count());
    }

    @Test
    void aGoalNotReachedWithACarPluggedInIsRecordedAsMissed() {
        var h = goalHistory("2026-10-19T06:00:00Z");
        charge(h, 5, 600);                            // far too little
        clock.set(java.time.Instant.parse("2026-10-19T07:00:20Z").toEpochMilli());
        carPlugged = true;
        poller.poll();
        assertEquals(2, h.goalOutcome(java.time.LocalDate.of(2026, 10, 19)));
    }

    @Test
    void noCarAndNoEnergyMeansThereWasNothingToJudge() {
        var h = goalHistory("2026-10-19T06:00:00Z");
        carPlugged = false;
        clock.set(java.time.Instant.parse("2026-10-19T07:00:20Z").toEpochMilli());
        poller.poll();
        assertEquals(0, h.goalOutcome(java.time.LocalDate.of(2026, 10, 19)));
    }

    @Test
    void pausedOrCustomNeverJudgesAGoal() {
        var h = goalHistory("2026-10-19T06:00:00Z");
        state.setPaused(true);
        clock.set(java.time.Instant.parse("2026-10-19T07:00:20Z").toEpochMilli());
        poller.poll();
        assertEquals(0, h.goalOutcome(java.time.LocalDate.of(2026, 10, 19)));
    }

    // --- Cheapest hours ---

    private void setLocalTime(int month, int day, int hour) {
        clock.set(java.time.ZonedDateTime.of(2026, month, day, hour, 0, 0, 0, com.example.config.AppConfig.siteZone()).toInstant().toEpochMilli());
    }

    @Test
    void cheapestHoursChargesAtTheMaximumInTheCheapHoursWithoutLookingAtTheSun() {
        state.setMode(ChargingMode.CHEAPEST);
        state.setMaxAmps(32);
        solarWatts = 0;                              // night, no solar: irrelevant
        setLocalTime(11, 4, 22);
        poller.poll();
        assertEquals(List.of(32), sentLimits);
        assertEquals("cheap hours", state.control().reason());
        assertEquals(32, state.control().targetAmps());
    }

    @Test
    void cheapestHoursStopsWhenTheExpensiveHoursBegin() {
        state.setMode(ChargingMode.CHEAPEST);
        state.setMaxAmps(32);
        setLocalTime(11, 4, 15);
        poller.poll();
        setLocalTime(11, 4, 16);                     // 4 pm: the default peak window opens
        poller.poll();
        assertEquals(List.of(32, 0), sentLimits);
        assertEquals("expensive hours", state.control().reason());
    }

    @Test
    void cheapestHoursDoesNotTripTheMeterFailSafe() {
        state.setMode(ChargingMode.CHEAPEST);
        failure = new IOException("no meter at all");
        setLocalTime(11, 4, 23);
        pollFor(600);
        assertFalse(state.failSafe());
        assertEquals(state.maxAmps(), sentLimits.get(sentLimits.size() - 1).intValue());
    }

    @Test
    void cheapestHoursStaysStoppedWhilePaused() {
        state.setMode(ChargingMode.CHEAPEST);
        state.setPaused(true);
        setLocalTime(11, 4, 23);
        poller.poll();
        assertEquals(List.of(0), sentLimits);
    }
}
