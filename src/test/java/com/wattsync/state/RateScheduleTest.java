package com.wattsync.state;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class RateScheduleTest {

    @Test
    void withoutAPeakPriceEveryHourCostsTheNormalPrice() {
        for (int hour = 0; hour < 24; hour++) assertEquals(30, RateSchedule.centsAt(30, 0, 16, 21, hour));
    }

    @Test
    void thePeakPriceAppliesFromTheStartHourUpToButNotIncludingTheEndHour() {
        assertEquals(30, RateSchedule.centsAt(30, 52, 16, 21, 15));
        assertEquals(52, RateSchedule.centsAt(30, 52, 16, 21, 16));
        assertEquals(52, RateSchedule.centsAt(30, 52, 16, 21, 20));
        assertEquals(30, RateSchedule.centsAt(30, 52, 16, 21, 21));
    }

    @Test
    void aPeakThatRunsPastMidnightWrapsAround() {
        assertEquals(52, RateSchedule.centsAt(30, 52, 22, 6, 23));
        assertEquals(52, RateSchedule.centsAt(30, 52, 22, 6, 3));
        assertEquals(30, RateSchedule.centsAt(30, 52, 22, 6, 12));
        assertEquals(52, RateSchedule.centsAt(30, 52, 0, 24, 13));   // all day
    }
}
