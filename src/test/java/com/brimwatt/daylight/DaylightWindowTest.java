package com.brimwatt.daylight;

import static org.junit.jupiter.api.Assertions.*;

import java.time.Duration;
import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneId;
import org.junit.jupiter.api.Test;

class DaylightWindowTest {

    private static final ZoneId LA = ZoneId.of("America/Los_Angeles");

    private DaylightWindow lathrop(int afterSunriseMinutes, int beforeSunsetMinutes) {
        return new DaylightWindow(37.8412, -121.2691, LA,
                Duration.ofMinutes(afterSunriseMinutes), Duration.ofMinutes(beforeSunsetMinutes));
    }

    @Test
    void windowIsSunriseToSunsetShrunkByTheMargins() {
        LocalDate day = LocalDate.of(2025, 6, 21);
        var sun = SunCalculator.calculate(day, 37.8412, -121.2691, LA);
        DaylightWindow.Span span = lathrop(60, 60).spanFor(day).orElseThrow();

        assertEquals(sun.sunrise().plus(Duration.ofHours(1)), span.start());
        assertEquals(sun.sunset().minus(Duration.ofHours(1)), span.end());
    }

    @Test
    void marginsCanBeChangedLater() {
        LocalDate day = LocalDate.of(2025, 6, 21);
        var sun = SunCalculator.calculate(day, 37.8412, -121.2691, LA);
        DaylightWindow window = lathrop(60, 60);
        window.setMargins(Duration.ofMinutes(30), Duration.ofMinutes(60));
        DaylightWindow.Span span = window.spanFor(day).orElseThrow();
        assertEquals(sun.sunrise().plus(Duration.ofMinutes(30)), span.start());
        assertEquals(sun.sunset().minus(Duration.ofHours(1)), span.end());
    }

    @Test
    void opensAndClosesAtTheEdges() {
        DaylightWindow window = lathrop(60, 60);
        DaylightWindow.Span span = window.spanFor(LocalDate.of(2025, 6, 21)).orElseThrow();

        assertFalse(window.isOpen(span.start().minusSeconds(1)));
        assertTrue(window.isOpen(span.start()));
        assertTrue(window.isOpen(span.end().minusSeconds(1)));
        assertFalse(window.isOpen(span.end()));
    }

    @Test
    void closedAtNightAndOpenAtMidday() {
        DaylightWindow window = lathrop(60, 60);
        Instant midnight = LocalDate.of(2025, 6, 21).atTime(2, 0).atZone(LA).toInstant();
        Instant midday = LocalDate.of(2025, 6, 21).atTime(13, 0).atZone(LA).toInstant();
        Instant lateEvening = LocalDate.of(2025, 6, 21).atTime(20, 45).atZone(LA).toInstant();

        assertFalse(window.isOpen(midnight));
        assertTrue(window.isOpen(midday));
        assertFalse(window.isOpen(lateEvening));
    }

    @Test
    void winterWindowIsShorterThanSummerWindow() {
        DaylightWindow window = lathrop(60, 60);
        var summer = window.spanFor(LocalDate.of(2025, 6, 21)).orElseThrow();
        var winter = window.spanFor(LocalDate.of(2025, 12, 21)).orElseThrow();
        assertTrue(Duration.between(winter.start(), winter.end()).compareTo(Duration.between(summer.start(), summer.end())) < 0);
    }

    @Test
    void worksAcrossBothDaylightSavingChanges() {
        DaylightWindow window = lathrop(60, 60);
        for (LocalDate day : new LocalDate[] {LocalDate.of(2025, 3, 9), LocalDate.of(2025, 11, 2)}) {
            DaylightWindow.Span span = window.spanFor(day).orElseThrow();
            assertTrue(span.end().isAfter(span.start()));
            assertEquals(day, span.start().atZone(LA).toLocalDate());
            assertEquals(day, span.end().atZone(LA).toLocalDate());
        }
    }

    @Test
    void marginsLargerThanTheDayMeanItNeverOpens() {
        DaylightWindow window = lathrop(60 * 8, 60 * 8);
        assertTrue(window.spanFor(LocalDate.of(2025, 12, 21)).isEmpty());
        assertFalse(window.isOpen(LocalDate.of(2025, 12, 21).atTime(12, 0).atZone(LA).toInstant()));
        assertEquals("no daylight window today", window.describe(LocalDate.of(2025, 12, 21).atTime(12, 0).atZone(LA).toInstant()));
    }

    @Test
    void polarDayIsOpenAllDayAndPolarNightNever() {
        ZoneId oslo = ZoneId.of("Europe/Oslo");
        DaylightWindow arctic = new DaylightWindow(69.65, 18.96, oslo, Duration.ofHours(1), Duration.ofHours(1));
        assertTrue(arctic.isOpen(LocalDate.of(2024, 6, 21).atTime(2, 0).atZone(oslo).toInstant()));
        assertFalse(arctic.isOpen(LocalDate.of(2024, 12, 21).atTime(12, 0).atZone(oslo).toInstant()));
    }

    @Test
    void describesTheWindowAsClockTimes() {
        String text = lathrop(60, 60).describe(LocalDate.of(2025, 6, 21).atTime(12, 0).atZone(LA).toInstant());
        assertTrue(text.matches("\\d\\d:\\d\\d to \\d\\d:\\d\\d"), text);
        assertEquals("06:43 to 19:30", text);
    }
}
