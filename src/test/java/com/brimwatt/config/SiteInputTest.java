package com.brimwatt.config;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class SiteInputTest {

    @Test
    void aGoodLocationIsKeptToTwoDecimals() {
        var r = SiteInput.parse("34.0901", "-118.4065", "America/Los_Angeles", "Beverly Hills, California");
        assertTrue(r.ok());
        assertEquals(34.09, r.site().latitude(), 1e-9);
        assertEquals(-118.41, r.site().longitude(), 1e-9);
        assertEquals("America/Los_Angeles", r.site().timezone());
        assertEquals("Beverly Hills, California", r.site().name());
    }

    @Test
    void theEdgesAreAllowedAndBeyondThemAreNot() {
        assertTrue(SiteInput.parse("90", "180", "America/Denver", "").ok());
        assertTrue(SiteInput.parse("-90", "-180", "America/Denver", "").ok());
        assertTrue(SiteInput.parse("90.01", "0", "America/Denver", "").error().contains("Latitude"));
        assertTrue(SiteInput.parse("0", "-180.5", "America/Denver", "").error().contains("Longitude"));
        for (String bad : new String[] {"abc", "", " ", "nan", "Infinity", "12abc", null}) {
            assertFalse(SiteInput.parse(bad, "0", "America/Denver", "").ok(), "latitude: " + bad);
            assertFalse(SiteInput.parse("0", bad, "America/Denver", "").ok(), "longitude: " + bad);
        }
    }

    @Test
    void theTimeZoneMustBeARealRegionName() {
        assertTrue(SiteInput.parse("1", "2", "Europe/Paris", "").ok());   // the Pi knows every zone Java knows
        for (String bad : new String[] {"", "EST", "Mars/Olympus", "america los angeles", null}) {
            var r = SiteInput.parse("1", "2", bad, "");
            assertFalse(r.ok(), "zone: " + bad);
            assertTrue(r.error().contains("time zone"));
        }
    }

    @Test
    void namesAreShortAndPrintable() {
        assertTrue(SiteInput.parse("1", "2", "America/Denver", "a".repeat(60)).ok());
        assertTrue(SiteInput.parse("1", "2", "America/Denver", "a".repeat(61)).error().contains("60 characters"));
        assertTrue(SiteInput.parse("1", "2", "America/Denver", "bad\tname").error().contains("character"));
        assertTrue(SiteInput.parse("1", "2", "America/Denver", null).ok());
    }

    @Test
    void aSavedSiteWinsOverTheEnvironmentAndAnUnknownSavedZoneIsIgnored() {
        try {
            AppConfig.useSite(34.09, -118.41, "America/Chicago");
            assertEquals(34.09, AppConfig.siteLatitude(), 1e-9);
            assertEquals("America/Chicago", AppConfig.siteZone().getId());
            AppConfig.useSite(34.09, -118.41, "Not/AZone");
            assertEquals(AppConfig.DEFAULT_SITE_TIMEZONE, AppConfig.siteZone().getId());
        } finally {
            AppConfig.useSite(null, null, null);
        }
        assertEquals(AppConfig.DEFAULT_SITE_LATITUDE, AppConfig.siteLatitude(), 1e-9);
    }
}
