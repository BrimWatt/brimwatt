package com.wattsync.daylight;

import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneId;

/**
 * Sunrise and sunset from a date and a position, using the NOAA solar calculation (accurate to
 * about a minute away from the poles). Needs no network access.
 */
public final class SunCalculator {

    public enum Kind {
        /** The sun rises and sets on this date. */
        NORMAL,
        /** Polar day: the sun stays up all day. */
        ALWAYS_UP,
        /** Polar night: the sun never rises. */
        ALWAYS_DOWN
    }

    public record SunTimes(Kind kind, Instant sunrise, Instant solarNoon, Instant sunset) {}

    /** Zenith for sunrise and sunset: 90 degrees plus atmospheric refraction and the sun's radius. */
    private static final double SUNRISE_ZENITH_DEGREES = 90.833;

    private SunCalculator() {}

    /**
     * @param date      the local calendar date in {@code zone}
     * @param latitude  degrees, north positive
     * @param longitude degrees, east positive (so the western US is negative)
     */
    public static SunTimes calculate(LocalDate date, double latitude, double longitude, ZoneId zone) {
        // Julian date of local solar noon, roughly, and the Julian century used by the formulas.
        double julianDay = date.toEpochDay() + 2440587.5 + 0.5 - longitude / 360.0;
        double t = (julianDay - 2451545.0) / 36525.0;

        double meanLongitude = mod360(280.46646 + t * (36000.76983 + t * 0.0003032));
        double meanAnomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
        double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);

        double m = Math.toRadians(meanAnomaly);
        double equationOfCenter = Math.sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t))
                + Math.sin(2 * m) * (0.019993 - 0.000101 * t)
                + Math.sin(3 * m) * 0.000289;
        double trueLongitude = meanLongitude + equationOfCenter;
        double omega = 125.04 - 1934.136 * t;
        double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * Math.sin(Math.toRadians(omega));

        double meanObliquity = 23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60;
        double obliquity = meanObliquity + 0.00256 * Math.cos(Math.toRadians(omega));
        double declination = Math.asin(Math.sin(Math.toRadians(obliquity)) * Math.sin(Math.toRadians(apparentLongitude)));

        double y = Math.pow(Math.tan(Math.toRadians(obliquity / 2)), 2);
        double l0 = Math.toRadians(meanLongitude);
        double equationOfTimeMinutes = 4 * Math.toDegrees(
                y * Math.sin(2 * l0)
                        - 2 * eccentricity * Math.sin(m)
                        + 4 * eccentricity * y * Math.sin(m) * Math.cos(2 * l0)
                        - 0.5 * y * y * Math.sin(4 * l0)
                        - 1.25 * eccentricity * eccentricity * Math.sin(2 * m));

        double solarNoonMinutesUtc = 720 - 4 * longitude - equationOfTimeMinutes;
        Instant noon = Instant.ofEpochSecond(date.toEpochDay() * 86400L)
                .plusMillis(Math.round(solarNoonMinutesUtc * 60_000));
        noon = onLocalDate(noon, date, zone);

        double lat = Math.toRadians(latitude);
        double cosHourAngle = Math.cos(Math.toRadians(SUNRISE_ZENITH_DEGREES)) / (Math.cos(lat) * Math.cos(declination))
                - Math.tan(lat) * Math.tan(declination);
        if (cosHourAngle > 1) return new SunTimes(Kind.ALWAYS_DOWN, null, noon, null);
        if (cosHourAngle < -1) return new SunTimes(Kind.ALWAYS_UP, null, noon, null);

        double hourAngleDegrees = Math.toDegrees(Math.acos(cosHourAngle));
        long halfDayMillis = Math.round(hourAngleDegrees * 4 * 60_000);   // 4 minutes of time per degree
        return new SunTimes(Kind.NORMAL, noon.minusMillis(halfDayMillis), noon, noon.plusMillis(halfDayMillis));
    }

    /** Solar noon computed from the UTC day can fall on the neighbouring local date far from Greenwich. */
    private static Instant onLocalDate(Instant noon, LocalDate date, ZoneId zone) {
        LocalDate actual = noon.atZone(zone).toLocalDate();
        if (actual.isBefore(date)) return noon.plusSeconds(86400);
        if (actual.isAfter(date)) return noon.minusSeconds(86400);
        return noon;
    }

    private static double mod360(double degrees) {
        double r = degrees % 360;
        return r < 0 ? r + 360 : r;
    }
}
