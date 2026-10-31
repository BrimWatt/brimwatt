package com.example.charging;

import static org.junit.jupiter.api.Assertions.*;

import java.time.ZoneId;
import java.time.ZonedDateTime;
import org.junit.jupiter.api.Test;

class CheapHoursTest {

    private static final ZoneId LA = ZoneId.of("America/Los_Angeles");

    private static ZonedDateTime at(int month, int day, int hour, int minute) {
        return ZonedDateTime.of(2026, month, day, hour, minute, 0, 0, LA);   // 4 Nov 2026 is a Wednesday, 7 Nov a Saturday
    }

    @Test
    void theWindowIsExpensiveAndEverythingElseIsCheap() {
        assertFalse(CheapHours.expensive(at(11, 4, 15, 59), 16, 21, true));
        assertTrue(CheapHours.expensive(at(11, 4, 16, 0), 16, 21, true));
        assertTrue(CheapHours.expensive(at(11, 4, 20, 59), 16, 21, true));
        assertFalse(CheapHours.expensive(at(11, 4, 21, 0), 16, 21, true));
    }

    @Test
    void aWindowThatWrapsPastMidnightWorks() {
        assertTrue(CheapHours.expensive(at(11, 4, 23, 0), 22, 6, true));
        assertTrue(CheapHours.expensive(at(11, 4, 5, 59), 22, 6, true));
        assertFalse(CheapHours.expensive(at(11, 4, 6, 0), 22, 6, true));
    }

    @Test
    void weekendsAreCheapAllDayWhenTheWindowIsWeekdaysOnly() {
        assertTrue(CheapHours.expensive(at(11, 4, 17, 0), 16, 21, false));    // Wednesday
        assertFalse(CheapHours.expensive(at(11, 7, 17, 0), 16, 21, false));   // Saturday
        assertTrue(CheapHours.expensive(at(11, 7, 17, 0), 16, 21, true));
    }

    @Test
    void secondsToChangeCountsToTheNextPriceChange() {
        long now = at(11, 4, 15, 30).toInstant().toEpochMilli();
        assertEquals(30 * 60, CheapHours.secondsToChange(now, LA, 16, 21, true));            // peak starts at 4 pm
        long inside = at(11, 4, 18, 0).toInstant().toEpochMilli();
        assertEquals(3 * 3600, CheapHours.secondsToChange(inside, LA, 16, 21, true));        // peak ends at 9 pm
    }

    @Test
    void onFridayEveningWeekdaysOnlyTheNextChangeIsMondayAfternoon() {
        long friNight = at(11, 6, 22, 0).toInstant().toEpochMilli();   // Friday 10 pm, cheap
        long expected = at(11, 9, 16, 0).toEpochSecond() - friNight / 1000;
        assertEquals(expected, CheapHours.secondsToChange(friNight, LA, 16, 21, false));
    }
}
