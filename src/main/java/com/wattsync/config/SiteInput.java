package com.wattsync.config;

import java.time.DateTimeException;
import java.time.ZoneId;

/**
 * Checks a location sent from the dashboard (latitude, longitude, time zone name, a display name) and makes the one to keep:
 * latitude and longitude to two decimals (about a kilometre), a time zone Java knows, and a name of up to 60 printable characters.
 * The messages are the same words the board uses.
 */
public final class SiteInput {

    private SiteInput() {}

    public record Site(double latitude, double longitude, String timezone, String name) {}

    /** Either a site, or the message to show. */
    public record Result(Site site, String error) {
        public boolean ok() { return site != null; }
    }

    public static Result parse(String lat, String lon, String tz, String name) {
        Double la = decimal(lat);
        if (la == null || la < -90 || la > 90) return fail("Latitude must be a number from -90 to 90.");
        Double lo = decimal(lon);
        if (lo == null || lo < -180 || lo > 180) return fail("Longitude must be a number from -180 to 180.");
        String zone = tz == null ? "" : tz.trim();
        if (!zone.contains("/")) return fail("That time zone is not supported on this controller yet.");
        try {
            ZoneId.of(zone);
        } catch (DateTimeException e) {
            return fail("That time zone is not supported on this controller yet.");
        }
        String label = name == null ? "" : name;
        if (label.length() > 60) return fail("The place name must be 60 characters or fewer.");
        for (int i = 0; i < label.length(); i++) {
            char c = label.charAt(i);
            if (c < 32 || c == 127) return fail("The place name has a character that is not allowed.");
        }
        return new Result(new Site(Math.round(la * 100) / 100.0, Math.round(lo * 100) / 100.0, zone, label), null);
    }

    /** A whole decimal number; null for anything else (empty, text after the number, not finite). */
    static Double decimal(String text) {
        if (text == null || text.isBlank()) return null;
        try {
            double v = Double.parseDouble(text.trim());
            return Double.isFinite(v) ? v : null;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private static Result fail(String message) { return new Result(null, message); }
}
