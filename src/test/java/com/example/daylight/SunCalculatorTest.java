package com.example.daylight;

import static org.junit.jupiter.api.Assertions.*;

import com.example.daylight.SunCalculator.Kind;
import com.example.daylight.SunCalculator.SunTimes;
import java.time.Duration;
import java.time.LocalDate;
import java.time.LocalTime;
import java.time.ZoneId;
import org.junit.jupiter.api.Test;

class SunCalculatorTest {

    private static final long TOLERANCE_MINUTES = 3;

    private void assertTime(String expected, java.time.Instant actual, ZoneId zone, String what) {
        LocalTime want = LocalTime.parse(expected);
        LocalTime got = actual.atZone(zone).toLocalTime();
        long diff = Math.abs(Duration.between(want, got).toMinutes());
        assertTrue(diff <= TOLERANCE_MINUTES, what + ": expected about " + expected + " but was " + got);
    }

    @Test
    void londonSummerSolstice() {
        ZoneId zone = ZoneId.of("Europe/London");
        SunTimes sun = SunCalculator.calculate(LocalDate.of(2024, 6, 21), 51.4779, -0.0015, zone);
        assertEquals(Kind.NORMAL, sun.kind());
        assertTime("04:43", sun.sunrise(), zone, "sunrise");
        assertTime("21:21", sun.sunset(), zone, "sunset");
    }

    @Test
    void newYorkWinterSolstice() {
        ZoneId zone = ZoneId.of("America/New_York");
        SunTimes sun = SunCalculator.calculate(LocalDate.of(2024, 12, 21), 40.7128, -74.0060, zone);
        assertTime("07:17", sun.sunrise(), zone, "sunrise");
        assertTime("16:32", sun.sunset(), zone, "sunset");
    }

    @Test
    void lathropMatchesPublishedTimesAroundTheSummerSolstice() {
        // timeanddate.com lists 5:42 am and 8:29 pm for a northern California site on June 16, 2025; the 21st is a minute later at sunset.
        ZoneId zone = ZoneId.of("America/Los_Angeles");
        SunTimes sun = SunCalculator.calculate(LocalDate.of(2025, 6, 21), 37.8412, -121.2691, zone);
        assertTime("05:43", sun.sunrise(), zone, "sunrise");
        assertTime("20:30", sun.sunset(), zone, "sunset");
    }

    @Test
    void lathropWinterDaysAreShorterThanSummerDaysByAboutFiveHours() {
        ZoneId zone = ZoneId.of("America/Los_Angeles");
        SunTimes winter = SunCalculator.calculate(LocalDate.of(2025, 12, 21), 37.8412, -121.2691, zone);
        SunTimes summer = SunCalculator.calculate(LocalDate.of(2025, 6, 21), 37.8412, -121.2691, zone);
        long winterMinutes = Duration.between(winter.sunrise(), winter.sunset()).toMinutes();
        long summerMinutes = Duration.between(summer.sunrise(), summer.sunset()).toMinutes();
        assertTrue(winterMinutes > 9 * 60 && winterMinutes < 10 * 60, "winter day was " + winterMinutes);
        assertTrue(summerMinutes > 14 * 60 + 30 && summerMinutes < 15 * 60 + 30, "summer day was " + summerMinutes);
    }

    @Test
    void sydneyEastOfGreenwichKeepsTheLocalDate() {
        ZoneId zone = ZoneId.of("Australia/Sydney");
        SunTimes sun = SunCalculator.calculate(LocalDate.of(2024, 12, 21), -33.8688, 151.2093, zone);
        assertEquals(LocalDate.of(2024, 12, 21), sun.sunrise().atZone(zone).toLocalDate());
        assertEquals(LocalDate.of(2024, 12, 21), sun.sunset().atZone(zone).toLocalDate());
        assertTime("05:41", sun.sunrise(), zone, "sunrise");
        assertTime("20:05", sun.sunset(), zone, "sunset");
    }

    @Test
    void equatorAtTheEquinoxHasAboutTwelveHoursOfDay() {
        SunTimes sun = SunCalculator.calculate(LocalDate.of(2024, 3, 20), 0.0, 0.0, ZoneId.of("UTC"));
        long minutes = Duration.between(sun.sunrise(), sun.sunset()).toMinutes();
        assertTrue(minutes >= 12 * 60 + 3 && minutes <= 12 * 60 + 12, "day length was " + minutes);
    }

    @Test
    void polarDayAndNight() {
        ZoneId zone = ZoneId.of("Europe/Oslo");
        assertEquals(Kind.ALWAYS_UP, SunCalculator.calculate(LocalDate.of(2024, 6, 21), 69.65, 18.96, zone).kind());
        assertEquals(Kind.ALWAYS_DOWN, SunCalculator.calculate(LocalDate.of(2024, 12, 21), 69.65, 18.96, zone).kind());
    }

    @Test
    void solarNoonSitsBetweenSunriseAndSunset() {
        ZoneId zone = ZoneId.of("America/Los_Angeles");
        for (int month = 1; month <= 12; month++) {
            SunTimes sun = SunCalculator.calculate(LocalDate.of(2025, month, 15), 37.8412, -121.2691, zone);
            assertTrue(sun.sunrise().isBefore(sun.solarNoon()) && sun.solarNoon().isBefore(sun.sunset()), "month " + month);
        }
    }
}
