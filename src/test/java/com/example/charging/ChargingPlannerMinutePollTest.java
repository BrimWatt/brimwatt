package com.example.charging;

import static org.junit.jupiter.api.Assertions.*;

import java.util.Arrays;
import java.util.Random;
import org.junit.jupiter.api.Test;

/** The same behaviour must hold when readings arrive once a minute instead of every 10 seconds. */
class ChargingPlannerMinutePollTest {

    private static final long TICK = 60_000;
    private static final double VOLTS = 230.0;

    private long now = 1_000_000;

    private int tick(ChargingPlanner planner, double availableAmps) {
        now += TICK;
        return planner.next(now, availableAmps).amps();
    }

    private int[] run(ChargingPlanner planner, double availableAmps, int ticks) {
        int[] out = new int[ticks];
        for (int i = 0; i < ticks; i++) out[i] = tick(planner, availableAmps);
        return out;
    }

    private int changes(int[] values) {
        int n = 0;
        for (int i = 1; i < values.length; i++) if (values[i] != values[i - 1]) n++;
        return n;
    }

    @Test
    void stepsDownOneAmpWhenImportGoesPastTheAllowance() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        run(planner, 10, 8);                         // charging steadily at 10 A
        assertEquals(10, tick(planner, 10));

        // 9.7 A available: the car draws 10 A and the grid import is a little above the allowance.
        int[] after = run(planner, 9.2, 4);
        assertEquals(9, after[after.length - 1], "a 1 A shortfall is enough to step down: " + Arrays.toString(after));
    }

    @Test
    void increasesStillNeedAFullTwoAmps() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        run(planner, 10, 8);
        for (int amps : run(planner, 11.6, 10)) assertEquals(10, amps, "1 A more is not worth a change");
        int[] up = run(planner, 13.5, 8);
        assertTrue(up[up.length - 1] >= 12, "2 A or more goes up: " + Arrays.toString(up));
    }

    @Test
    void startsOnlyAfterThreeMinutesOfSurplus() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        int[] amps = run(planner, 12, 5);
        assertEquals(0, amps[0]);
        assertEquals(0, amps[1]);
        assertEquals(0, amps[2]);
        assertEquals(12, amps[3]);
    }

    @Test
    void singleSpikeNeverStarts() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        run(planner, 0, 3);
        assertEquals(0, tick(planner, 25));
        for (int amps : run(planner, 0, 10)) assertEquals(0, amps);
    }

    @Test
    void ridesOutAShortfallAtTheMinimumThenStopsAfterFiveMinutes() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        run(planner, 15, 10);
        int[] dip = run(planner, 1, 6);
        for (int amps : dip) assertTrue(amps >= 6, "keeps charging at the minimum meanwhile: " + Arrays.toString(dip));
        assertEquals(6, dip[dip.length - 1]);
        int[] more = run(planner, 1, 3);
        assertEquals(0, more[more.length - 1], "the shortfall lasted over five minutes: " + Arrays.toString(more));
        for (int i = 0; i < more.length; i++) if (more[i] == 0) for (int j = i; j < more.length; j++) assertEquals(0, more[j]);
    }

    @Test
    void aShortfallUnderFiveMinutesNeverStopsTheCharge() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        run(planner, 15, 10);
        run(planner, 1, 4);                          // about three minutes below the minimum
        for (int amps : run(planner, 15, 10)) assertTrue(amps >= 6, "still charging after the sun returns");
    }

    @Test
    void steadySunUnderLoadFeedbackDoesNotOscillate() {
        double solar = 5000, house = 500;
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        int applied = 0;
        int[] history = new int[60];
        for (int i = 0; i < history.length; i++) {
            double export = solar - house - applied * VOLTS;
            applied = tick(planner, Math.max(0, applied + export / VOLTS));
            history[i] = applied;
        }
        assertEquals(19, history[history.length - 1]);
        assertEquals(0, changes(Arrays.copyOfRange(history, 10, history.length)));
    }

    @Test
    void noisyReadingsCauseFarFewerChangesThanNaive() {
        Random random = new Random(7);
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        run(planner, 12, 6);
        int[] history = new int[120];                // two hours of one-minute readings
        int[] naive = new int[120];
        for (int i = 0; i < history.length; i++) {
            double reading = 12 + (random.nextDouble() - 0.5) * 8;
            history[i] = tick(planner, reading);
            naive[i] = (int) Math.floor(reading);
        }
        assertTrue(changes(history) * 2 <= changes(naive),
                "planner changed " + changes(history) + " times, naive " + changes(naive));
        for (int amps : history) assertTrue(amps >= 6);
    }

    @Test
    void importIsCorrectedWithinOnePoll() {
        ChargingPlanner planner = new ChargingPlanner(ChargingPolicy.eco());
        run(planner, 20, 8);
        int next = tick(planner, 8);
        assertTrue(next <= 14, "should already be coming down on the next reading, was " + next);
        assertTrue(tick(planner, 8) <= 11, "and be close to 8A a minute later");
    }
}
