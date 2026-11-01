package com.example.charging;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class GoalTrackerTest {

    private static final int OFFSET = -7 * 3600;                 // a fixed offset keeps the arithmetic plain
    private static final int SEVEN_AM = 7 * 60;
    private static final long MIDNIGHT = 20000L * 86400 + 7 * 3600;   // local midnight for OFFSET on day 20000

    private long at(int hour, int minute) { return MIDNIGHT + hour * 3600L + minute * 60L; }

    @Test
    void theFirstLookOnlySetsTheBaselineAndJudgesNothing() {
        GoalTracker t = new GoalTracker();
        assertEquals(-1, t.due(at(23, 0), OFFSET, SEVEN_AM, 20));
    }

    @Test
    void aDeadlineIsJudgedOnceRightAfterItPasses() {
        GoalTracker t = new GoalTracker();
        t.due(at(23, 0), OFFSET, SEVEN_AM, 20);
        assertEquals(-1, t.due(at(30, 0) - 60, OFFSET, SEVEN_AM, 20));        // one minute before 6 am next day... still ahead of 7 am
        long deadline = at(24 + 7, 0);                                         // 7 am the next morning
        assertEquals(-1, t.due(deadline - 60, OFFSET, SEVEN_AM, 20));
        assertEquals(deadline, t.due(deadline + 30, OFFSET, SEVEN_AM, 20));
        assertEquals(-1, t.due(deadline + 90, OFFSET, SEVEN_AM, 20));          // not again
    }

    @Test
    void aDeadlineNoticedMuchLaterIsNotJudged() {
        GoalTracker t = new GoalTracker();
        t.due(at(23, 0), OFFSET, SEVEN_AM, 20);
        assertEquals(-1, t.due(at(24 + 12, 0), OFFSET, SEVEN_AM, 20));        // 5 hours after the deadline (the app was off)
        assertEquals(at(48 + 7, 0), t.due(at(48 + 7, 1), OFFSET, SEVEN_AM, 20));   // the next one is judged normally
    }

    @Test
    void changingTheGoalOrItsTimeStartsOverWithoutJudging() {
        GoalTracker t = new GoalTracker();
        t.due(at(23, 0), OFFSET, SEVEN_AM, 20);
        assertEquals(-1, t.due(at(24 + 7, 0) + 30, OFFSET, 6 * 60, 20));       // time changed to 6 am: baseline again
        assertEquals(-1, t.due(at(24 + 7, 0) + 60, OFFSET, 6 * 60, 25));       // kWh changed: baseline again
    }

    @Test
    void turningTheGoalOffForgetsEverything() {
        GoalTracker t = new GoalTracker();
        t.due(at(23, 0), OFFSET, SEVEN_AM, 20);
        assertEquals(-1, t.due(at(24 + 7, 0) + 30, OFFSET, SEVEN_AM, 0));
        assertEquals(-1, t.due(at(24 + 7, 0) + 60, OFFSET, SEVEN_AM, 20));     // back on: baseline, the passed deadline is not judged
    }
}
