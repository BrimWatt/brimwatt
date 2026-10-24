package com.example.charging;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class GoalPlannerTest {

    private static final double V = 240;

    @Test
    void noGoalMeansNothing() {
        assertEquals(0, GoalPlanner.decide(0, 0, 3600, 6, 32, V).forcedAmps());
    }

    @Test
    void plentyOfTimeLeavesItToTheSun() {
        var d = GoalPlanner.decide(10_000, 0, 6 * 3600, 6, 32, V);
        assertEquals(0, d.forcedAmps());
        assertFalse(d.met());
        assertFalse(d.unreachable());
    }

    @Test
    void closeToTheDeadlineAsksForTheLowestCurrentThatArrivesInTime() {
        var d = GoalPlanner.decide(10_000, 0, 5400, 6, 32, V);   // 1.5 h left
        assertEquals(28, d.forcedAmps());                          // 10000 / 1.5 / 240 = 27.8
    }

    @Test
    void neverBelowTheMinimumOrAboveTheMaximum() {
        assertEquals(6, GoalPlanner.decide(300, 0, 800, 6, 32, V).forcedAmps());
        var d = GoalPlanner.decide(50_000, 0, 3600, 6, 32, V);
        assertEquals(32, d.forcedAmps());
        assertTrue(d.unreachable());
    }

    @Test
    void metGoalStopsForcing() {
        var d = GoalPlanner.decide(10_000, 10_500, 600, 6, 32, V);
        assertEquals(0, d.forcedAmps());
        assertTrue(d.met());
    }

    @Test
    void deadlineIsTheNextTimeOfDay() {
        long base = java.time.Instant.parse("2026-10-19T03:00:00Z").getEpochSecond();
        assertEquals(base + 4 * 3600, GoalPlanner.nextDeadline(base, 0, 7 * 60));
        assertEquals(base + 24 * 3600 - 3600, GoalPlanner.nextDeadline(base, 0, 2 * 60));   // 2:00 already passed, so tomorrow
    }
}
