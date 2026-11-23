package com.wattsync.charging;

import java.time.DayOfWeek;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.time.temporal.ChronoUnit;

/**
 * "Cheapest hours": the expensive hours are the peak window from Settings (start hour to end hour, wrapping past midnight);
 * everything else is cheap. With {@code weekends} off the window only applies Monday to Friday, so Saturday and Sunday are cheap
 * all day. Pure functions, mirrored by {@code solar::CheapHours} on the ESP32.
 */
public final class CheapHours {

    private CheapHours() {}

    public static boolean expensive(ZonedDateTime t, int startHour, int endHour, boolean weekends) {
        DayOfWeek day = t.getDayOfWeek();
        if (!weekends && (day == DayOfWeek.SATURDAY || day == DayOfWeek.SUNDAY)) return false;
        int hour = t.getHour();
        return startHour < endHour ? hour >= startHour && hour < endHour : hour >= startHour || hour < endHour;
    }

    public static boolean expensive(long epochMillis, ZoneId zone, int startHour, int endHour, boolean weekends) {
        return expensive(Instant.ofEpochMilli(epochMillis).atZone(zone), startHour, endHour, weekends);
    }

    /** Seconds until the price period changes (cheap to expensive or back), or -1 if it never does within a week. */
    public static long secondsToChange(long epochMillis, ZoneId zone, int startHour, int endHour, boolean weekends) {
        ZonedDateTime now = Instant.ofEpochMilli(epochMillis).atZone(zone);
        boolean current = expensive(now, startHour, endHour, weekends);
        ZonedDateTime step = now.truncatedTo(ChronoUnit.HOURS);
        for (int i = 0; i < 24 * 8; i++) {
            step = step.plusHours(1);
            if (expensive(step, startHour, endHour, weekends) != current) {
                return Math.max(0, step.toEpochSecond() - epochMillis / 1000);
            }
        }
        return -1;
    }
}
