package com.wattsync.daylight;

import com.wattsync.daylight.SunCalculator.Kind;
import com.wattsync.daylight.SunCalculator.SunTimes;

import java.time.Duration;
import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.Optional;

/** The part of the day between a little after sunrise and a little before sunset. */
public class DaylightWindow implements DaylightSchedule {

    public record Span(Instant start, Instant end) {}

    private static final DateTimeFormatter CLOCK = DateTimeFormatter.ofPattern("HH:mm");

    private final double latitude;
    private final double longitude;
    private final ZoneId zone;
    private volatile Duration startAfterSunrise;
    private volatile Duration endBeforeSunset;

    public DaylightWindow(double latitude, double longitude, ZoneId zone,
                          Duration startAfterSunrise, Duration endBeforeSunset) {
        this.latitude = latitude;
        this.longitude = longitude;
        this.zone = zone;
        this.startAfterSunrise = startAfterSunrise;
        this.endBeforeSunset = endBeforeSunset;
    }

    @Override
    public void setMargins(Duration startAfterSunrise, Duration endBeforeSunset) {
        this.startAfterSunrise = startAfterSunrise;
        this.endBeforeSunset = endBeforeSunset;
    }

    /** Empty if the window never opens on this date (polar night, or a day too short for the margins). */
    public Optional<Span> spanFor(LocalDate date) {
        SunTimes sun = SunCalculator.calculate(date, latitude, longitude, zone);
        if (sun.kind() == Kind.ALWAYS_DOWN) return Optional.empty();
        if (sun.kind() == Kind.ALWAYS_UP) {
            return Optional.of(new Span(date.atStartOfDay(zone).toInstant(), date.plusDays(1).atStartOfDay(zone).toInstant()));
        }
        Instant start = sun.sunrise().plus(startAfterSunrise);
        Instant end = sun.sunset().minus(endBeforeSunset);
        return end.isAfter(start) ? Optional.of(new Span(start, end)) : Optional.empty();
    }

    @Override
    public boolean isOpen(Instant now) {
        return spanFor(now.atZone(zone).toLocalDate())
                .map(span -> !now.isBefore(span.start()) && now.isBefore(span.end()))
                .orElse(false);
    }

    @Override
    public Optional<long[]> windowFor(Instant now) {
        return spanFor(now.atZone(zone).toLocalDate())
                .map(span -> new long[] {span.start().getEpochSecond(), span.end().getEpochSecond()});
    }

    @Override
    public String describe(Instant now) {
        return spanFor(now.atZone(zone).toLocalDate())
                .map(span -> span.start().atZone(zone).format(CLOCK) + " to " + span.end().atZone(zone).format(CLOCK))
                .orElse("no daylight window today");
    }

    public ZoneId zone() { return zone; }
}
