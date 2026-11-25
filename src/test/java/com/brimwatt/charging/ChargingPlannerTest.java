package com.brimwatt.charging;

import static org.junit.jupiter.api.Assertions.*;

import com.brimwatt.charging.ChargingPlanner.Decision;
import java.util.Random;
import org.junit.jupiter.api.Test;

class ChargingPlannerTest {

    private static final long TICK = 10_000;   // poll interval
    private static final double VOLTS = 230.0;

    private long now = 1_000_000;

    /** ECO tuned for 10-second sampling: shorter averages than the one-minute default. */
    private static ChargingPolicy tenSecondPolicy() {
        ChargingPolicy eco = ChargingPolicy.eco();
        return new ChargingPolicy(eco.minAmps(), eco.maxAmps(), eco.startAmps(), eco.startSustainMillis(),
                eco.minOffMillis(), 100, 30, eco.deadbandAmps(), eco.decreaseDeadbandAmps(),
                eco.minIncreaseIntervalMillis(), eco.minDecreaseIntervalMillis());
    }

    private Decision tick(ChargingPlanner planner, double availableAmps) {
        now += TICK;
        return planner.next(now, availableAmps);
    }

    /** Max solar at ten-second sampling: shorter averages, same floor, top-up rule and delays as the real mode. */
    private static ChargingPolicy maxSolar10() {
        ChargingPolicy m = ChargingPolicy.maxSolar();
        return new ChargingPolicy(m.minAmps(), m.maxAmps(), m.startAmps(), m.startSustainMillis(), m.minOffMillis(), 100, 30,
                m.deadbandAmps(), m.decreaseDeadbandAmps(), m.minIncreaseIntervalMillis(), m.minDecreaseIntervalMillis(),
                m.stopSustainMillis(), true);
    }

    /** Feeds the solar output (in amps) with the amount the grid may add on top. */
    private int[] runSolar(ChargingPlanner planner, double solarAmps, double topUpAmps, int ticks) {
        int[] out = new int[ticks];
        for (int i = 0; i < ticks; i++) {
            now += TICK;
            out[i] = planner.next(now, solarAmps, topUpAmps).amps();
        }
        return out;
    }

    private int[] run(ChargingPlanner planner, double availableAmps, int ticks) {
        int[] out = new int[ticks];
        for (int i = 0; i < ticks; i++) out[i] = tick(planner, availableAmps).amps();
        return out;
    }

    private int changes(int[] values) {
        int n = 0;
        for (int i = 1; i < values.length; i++) if (values[i] != values[i - 1]) n++;
        return n;
    }

    @Test
    void adoptsAChargeItDidNotStartInsteadOfStoppingIt() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        planner.adoptRunning(now, 14);
        int amps = tick(planner, 14).amps();
        assertEquals(14, amps, "surplus supports 14 A, so the running charge must continue");
    }

    @Test
    void adoptedChargeIsReducedImmediatelyWhenNoSurplus() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        planner.adoptRunning(now, 14);
        assertTrue(tick(planner, 0).amps() <= 6, "no surplus: drop to the minimum at once");
    }

    @Test
    void adoptRunningIgnoresNullAndDoesNotDisturbARunningPlan() {
        ChargingPlanner idle = new ChargingPlanner(tenSecondPolicy());
        idle.adoptRunning(now, null);
        assertEquals(0, tick(idle, 12).amps(), "still waiting to confirm surplus");

        ChargingPlanner adopted = new ChargingPlanner(tenSecondPolicy());
        adopted.adoptRunning(now, 30);               // a charge appears that we did not start
        adopted.adoptRunning(now, 8);                // second call must not override
        assertTrue(tick(adopted, 30).amps() >= 25);
    }

    @Test
    void resetWhileChargingLetsItBeReducedAtOnce() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        planner.reset(now, 14);                      // e.g. switched from FAST while charging
        assertTrue(tick(planner, 0).amps() <= 6, "no surplus: must come down on the first decision");
    }

    @Test
    void doesNotStartUntilSurplusHasLasted() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        int[] first = run(planner, 12, 6);          // 60 s
        for (int amps : first) assertEquals(0, amps);

        int[] later = run(planner, 12, 14);         // past the three minutes it has to last
        assertEquals(12, later[later.length - 1]);
    }

    /** The ten-second test policy with the real five-minute stop delay. */
    private static ChargingPolicy withStopDelay() {
        ChargingPolicy p = tenSecondPolicy();
        return new ChargingPolicy(p.minAmps(), p.maxAmps(), p.startAmps(), p.startSustainMillis(), p.minOffMillis(),
                p.slowAverageSeconds(), p.fastAverageSeconds(), p.deadbandAmps(), p.decreaseDeadbandAmps(),
                p.minIncreaseIntervalMillis(), p.minDecreaseIntervalMillis(), 300_000);
    }

    @Test
    void aDipRunsAtTheMinimumAndOnlyStopsOnceItHasLastedFiveMinutes() {
        ChargingPlanner planner = new ChargingPlanner(withStopDelay());
        run(planner, 15, 40);
        Decision last = null;
        int stoppedAtTick = -1;
        for (int i = 1; i <= 60 && stoppedAtTick < 0; i++) {
            last = tick(planner, 1);
            if (last.amps() == 0) stoppedAtTick = i;
            else assertTrue(last.amps() >= 6, "never below the charger's minimum while waiting");
        }
        assertTrue(stoppedAtTick > 30, "not before five minutes of shortfall, was tick " + stoppedAtTick);
        assertTrue(stoppedAtTick <= 50, "but soon after, was tick " + stoppedAtTick);
        assertTrue(last.reason().startsWith("stop:"), last.reason());
    }

    @Test
    void theSunComingBackDuringTheDipRestartsTheFiveMinutes() {
        ChargingPlanner planner = new ChargingPlanner(withStopDelay());
        run(planner, 15, 40);
        run(planner, 1, 25);                         // four minutes short
        for (int amps : run(planner, 15, 12)) assertTrue(amps >= 6);   // recovers: the clock resets
        for (int amps : run(planner, 1, 25)) assertTrue(amps >= 6, "another four minutes is again not enough");
    }

    @Test
    void briefSpikeNeverStartsCharging() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        run(planner, 0, 5);
        run(planner, 20, 3);                         // 30 s spike
        for (int amps : run(planner, 0, 30)) assertEquals(0, amps);
    }

    @Test
    void ecoStopsAsSoonAsAvailableFallsBelowTheMinimum() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        run(planner, 15, 30);                        // charging
        Decision stop = null;
        for (int i = 0; i < 18 && stop == null; i++) {
            Decision d = tick(planner, 2);           // less than the 6 A minimum, allowance included
            if (d.amps() == 0) stop = d;
        }
        assertNotNull(stop, "must stop instead of holding 6 A on the grid");
        assertTrue(stop.reason().startsWith("stop:"), stop.reason());
    }

    @Test
    void ecoNeverHoldsTheMinimumWhileAvailableIsBelowIt() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        run(planner, 15, 30);
        int[] weak = run(planner, 3, 60);            // 10 minutes of weak surplus
        boolean stopped = false;
        for (int amps : weak) {
            if (amps == 0) stopped = true;
            if (stopped) assertEquals(0, amps, "must stay off once stopped");
        }
        assertTrue(stopped);
    }

    @Test
    void ecoKeepsChargingWhileAvailableCoversTheMinimum() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        run(planner, 15, 30);
        for (int amps : run(planner, 7, 60)) assertTrue(amps >= 6, "available covers 6 A: " + amps);
    }

    @Test
    void maxSolarTopsUpFromTheGridToReachTheMinimum() {
        ChargingPlanner planner = new ChargingPlanner(maxSolar10());
        // 4.2 A of solar and room for 2.5 A from the grid: 6 A is reachable, so it charges at 6 A.
        int[] amps = runSolar(planner, 4.2, 2.5, 40);
        assertEquals(0, amps[0], "not before the start delay");
        assertEquals(6, amps[amps.length - 1]);
    }

    @Test
    void maxSolarDoesNotStartWhenTheGapIsBiggerThanTheAllowance() {
        ChargingPlanner planner = new ChargingPlanner(maxSolar10());
        for (int amps : runSolar(planner, 2.0, 2.5, 60)) assertEquals(0, amps, "4 A short of 6 A, only 2.5 A may come from the grid");
        for (int amps : runSolar(planner, 4.6, 0, 60)) assertEquals(0, amps, "no allowance: no top-up at all");
    }

    @Test
    void maxSolarRoundsUpToTheNextWholeAmpWhenTheGapFitsAndDownWhenItDoesNot() {
        ChargingPlanner planner = new ChargingPlanner(maxSolar10());
        assertEquals(9, runSolar(planner, 8.3, 2.5, 60)[59], "0.7 A from the grid reaches 9 A instead of exporting");
        ChargingPlanner noTopUp = new ChargingPlanner(maxSolar10());
        assertEquals(8, runSolar(noTopUp, 8.6, 0, 60)[59], "with no allowance the car stays on what the sun covers");
    }

    @Test
    void maxSolarFollowsTheSunUpAndStopsAfterTheDelayWhenItIsGone() {
        ChargingPlanner planner = new ChargingPlanner(maxSolar10());
        assertTrue(runSolar(planner, 17, 2.5, 60)[59] >= 17, "follows a strong sun");
        int[] dip = runSolar(planner, 0.5, 2.5, 20);
        for (int amps : dip) assertTrue(amps >= 6, "rides out a short dip at the minimum");
        int[] gone = runSolar(planner, 0.5, 2.5, 60);
        assertEquals(0, gone[gone.length - 1], "stops once the dip has lasted the stop delay");
    }

    @Test
    void doesNotRestartDuringMinimumOffTime() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        run(planner, 15, 30);
        run(planner, 1, 60);                         // stops
        int[] sun = run(planner, 15, 12);            // 2 min of sun right after
        for (int amps : sun) assertEquals(0, amps);
    }

    @Test
    void backsOffQuicklyButRampsUpSlowly() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        run(planner, 20, 40);
        assertEquals(20, tick(planner, 20).amps());

        int[] drop = run(planner, 8, 3);             // 30 s after the drop
        assertTrue(drop[2] <= 16, "should already be falling within 30s, was " + drop[2]);
        int[] more = run(planner, 8, 9);              // 2 min after the drop
        assertTrue(more[3] <= 12, "should be well down within 70s, was " + more[3]);
        assertTrue(more[8] <= 9, "should be near 8A within 2 min, was " + more[8]);

        int[] rise = run(planner, 20, 3);            // 30 s after sun returns
        assertTrue(rise[2] <= 12, "should still be low 30s after recovery, was " + rise[2]);
        int settled = run(planner, 20, 60)[59];
        assertTrue(settled >= 19, "should settle within the deadband of 20A, was " + settled);
    }

    @Test
    void steadySunUnderLoadFeedbackDoesNotOscillate() {
        // 5 kW solar, 500 W house load. Charger draw reduces export, so surplus alone would shrink.
        double solar = 5000, house = 500;
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        int applied = 0;
        int[] history = new int[120];
        for (int i = 0; i < history.length; i++) {
            double drawAmps = applied;
            double export = solar - house - drawAmps * VOLTS;
            double available = Math.max(0, drawAmps + export / VOLTS);
            applied = tick(planner, available).amps();
            history[i] = applied;
        }
        assertEquals(19, history[history.length - 1]);
        // Once started, the limit should stay put.
        int[] tail = java.util.Arrays.copyOfRange(history, 40, history.length);
        assertEquals(0, changes(tail));
    }

    @Test
    void noisyReadingsCauseFewLimitChanges() {
        Random random = new Random(42);
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        run(planner, 12, 30);
        int[] history = new int[360];                // one hour of readings, 8..16 A white noise
        int[] naive = new int[360];                  // what unsmoothed floor(available) would do
        for (int i = 0; i < history.length; i++) {
            double reading = 12 + (random.nextDouble() - 0.5) * 8;
            history[i] = tick(planner, reading).amps();
            naive[i] = (int) Math.floor(reading);
        }
        // Stepping down at a 1 A gap (to keep import near the allowance) makes a few more changes than a 2 A
        // dead zone would, but it is still many times fewer than following every reading.
        assertTrue(changes(history) * 6 <= changes(naive),
                "planner changed " + changes(history) + " times, naive " + changes(naive));
        for (int amps : history) assertTrue(amps >= 6);
    }

    @Test
    void resetWhileChargingKeepsCurrentLimitUntilReadingsJustifyChange() {
        ChargingPlanner planner = new ChargingPlanner(tenSecondPolicy());
        planner.reset(now, 32);                      // arriving from FAST at 32A
        assertEquals(32, tick(planner, 32).amps());
        int last = 32;
        for (int i = 0; i < 6; i++) last = tick(planner, 10).amps();
        assertTrue(last < 32, "should come down once readings stay low, was " + last);
    }

    @Test
    void countsDownTheStartConfirmationAndHasNoTimerWithoutSurplus() {
        ChargingPlanner none = new ChargingPlanner(ChargingPolicy.eco());
        now += 60_000;
        assertEquals(-1, none.next(now, 0).waitSeconds(), "no surplus: nothing to count");
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        now += 60_000;
        Decision first = planner.next(now, 15);
        assertEquals(180, first.waitSeconds(), "the 3 minute confirmation has just begun");
        now += 60_000;
        assertEquals(120, planner.next(now, 15).waitSeconds());
    }

    @Test
    void countsDownThePauseAfterAStopAndTheDipBeforeIt() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        for (int i = 0; i < 6; i++) { now += 60_000; planner.next(now, 15); }   // charging
        int previous = -1;
        Decision stop = null;
        for (int i = 0; i < 12 && stop == null; i++) {
            now += 60_000;
            Decision d = planner.next(now, 1);
            if (d.amps() == 0) { stop = d; break; }
            if (d.reason().startsWith("dip")) {
                if (previous > 0) assertEquals(previous - 60, d.waitSeconds(), "the stop timer runs down a minute per reading");
                else assertEquals(300, d.waitSeconds(), "the 5 minute dip has just begun");
                previous = d.waitSeconds();
            }
        }
        assertNotNull(stop, "it stops in the end");
        now += 60_000;
        Decision after = planner.next(now, 0.5);
        assertEquals(60, after.waitSeconds(), "one minute of the 2 minute pause is left");
    }
}
