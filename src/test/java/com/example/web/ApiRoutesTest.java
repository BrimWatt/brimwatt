package com.example.web;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class ApiRoutesTest {

    @Test
    void priceAndPeakHoursAreCheckedAsWholeNumbersInTheirRanges() {
        assertEquals(0, ApiRoutes.parseInRange("0", 0, 200).getAsInt());
        assertEquals(24, ApiRoutes.parseInRange(" 24 ", 1, 24).getAsInt());
        for (String bad : new String[] {"-1", "201", "abc", "1.5", "", null}) assertTrue(ApiRoutes.parseInRange(bad, 0, 200).isEmpty(), "rejects: " + bad);
        assertTrue(ApiRoutes.parseInRange("0", 1, 24).isEmpty(), "peakEnd cannot be 0");
    }

    @Test
    void customCurrentIsLimitedByTheMaximumCurrentSetting() {
        assertEquals(16, ApiRoutes.parseFastAmps("16", 16).getAsInt());
        assertTrue(ApiRoutes.parseFastAmps("17", 16).isEmpty(), "above the installation's maximum");
        assertEquals(40, ApiRoutes.parseFastAmps("40", 40).getAsInt());
    }

    @Test
    void changingRequestsNeedTheSafeHeaderButReadingDoesNot() {
        assertTrue(ApiRoutes.isAllowed("GET", null));
        assertTrue(ApiRoutes.isAllowed("get", null));
        assertTrue(ApiRoutes.isAllowed("POST", "1"));
        assertFalse(ApiRoutes.isAllowed("POST", null), "a cross-site form post carries no custom header");
        assertFalse(ApiRoutes.isAllowed("post", ""));
        assertFalse(ApiRoutes.isAllowed("POST", "0"));
    }

    @Test
    void theCarRoutesExistOnThePiButSayTheyNeedTheEsp32() {
        assertEquals(11, ApiRoutes.CAR_ROUTES.length);
        for (String route : ApiRoutes.CAR_ROUTES) assertTrue(route.startsWith("/api/car/"), route);
        assertTrue(ApiRoutes.CAR_UNSUPPORTED.contains("ESP32"));
    }

    @Test
    void theOldBalancedNameStillMeansMaxSolar() {
        assertEquals(com.example.model.ChargingMode.MAX_SOLAR, com.example.model.ChargingMode.parse("BALANCED").orElseThrow());
        assertEquals(com.example.model.ChargingMode.MAX_SOLAR, com.example.model.ChargingMode.parse("balanced").orElseThrow());
        assertEquals(com.example.model.ChargingMode.MAX_SOLAR, com.example.model.ChargingMode.parse("MAX_SOLAR").orElseThrow());
        assertEquals(com.example.model.ChargingMode.MAX_SOLAR, com.example.model.ChargingMode.parse("max solar").orElseThrow());
        assertEquals(com.example.model.ChargingMode.MAX_SOLAR, com.example.model.ChargingMode.parse("maxsolar").orElseThrow());
        assertTrue(com.example.model.ChargingMode.parse("turbo").isEmpty());
    }

    @Test
    void pagingOffsetsAreWholeNumbersFromZeroTo1000() {
        assertEquals(0, ApiRoutes.parseOffset(null).getAsInt());
        assertEquals(1000, ApiRoutes.parseOffset(" 1000 ").getAsInt());
        for (String bad : new String[] {"-1", "1001", "abc", "1.5", ""}) {
            assertTrue(ApiRoutes.parseOffset(bad).isEmpty(), "should reject: " + bad);
        }
    }

    @Test
    void daylightMarginsAreWholeMinutesFromZeroTo180() {
        assertEquals(0, ApiRoutes.parseMargin("0").getAsInt());
        assertEquals(180, ApiRoutes.parseMargin(" 180 ").getAsInt());
        for (String bad : new String[] {"-1", "181", "abc", "1.5", "", "99999999999", null}) {
            assertTrue(ApiRoutes.parseMargin(bad).isEmpty(), "should reject: " + bad);
        }
    }

    @Test
    void acceptsWholeWattsInRange() {
        assertEquals(0, ApiRoutes.parseAllowance("0").getAsInt());
        assertEquals(1000, ApiRoutes.parseAllowance("1000").getAsInt());
        assertEquals(1000, ApiRoutes.parseAllowance(" 1000 ").getAsInt());
    }

    @Test
    void fastCurrentMustBeWholeAmpsFromSixToForty() {
        assertEquals(6, ApiRoutes.parseFastAmps("6", 40).getAsInt());
        assertEquals(40, ApiRoutes.parseFastAmps("40", 40).getAsInt());
        for (String bad : new String[] {null, "", "5", "41", "abc", "12.5", "-6"}) {
            assertTrue(ApiRoutes.parseFastAmps(bad, 40).isEmpty(), "should reject: " + bad);
        }
    }

    @Test
    void rejectsEverythingElse() {
        for (String bad : new String[] {null, "", "abc", "-1", "1001", "5000", "12.5", "1e3", "99999999999"}) {
            assertTrue(ApiRoutes.parseAllowance(bad).isEmpty(), "should reject: " + bad);
        }
    }
}
