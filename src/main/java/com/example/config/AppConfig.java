package com.example.config;

/** Static application settings. Secrets come from the environment, never from source. */
public final class AppConfig {

    public static final int HTTP_PORT = 8080;

    public static final String ENPHASE_HOST = "https://envoy.local";
    public static final String ENPHASE_TOKEN_ENV = "ENPHASE_TOKEN";

    public static final long POLL_INITIAL_DELAY_SECONDS = 2;
    public static final long POLL_INTERVAL_SECONDS = 60;

    public static final String VOLTAGE_ENV = "HOUSEHOLD_VOLTAGE";
    public static final double DEFAULT_VOLTAGE = 230.0;

    public static final int MIN_AMPS = 6;
    public static final int MAX_AMPS = 32;
    public static final int MAX_FAST_AMPS = 40;
    public static final int DEFAULT_FAST_AMPS = 32;

    // ---- Daylight window ----
    // Solar-following modes (ECO, MAX_SOLAR) only run between a little after sunrise and a little
    // before sunset. Defaults are for Lathrop, CA; override with the SITE_* variables.
    public static final String SITE_LATITUDE_ENV = "SITE_LATITUDE";
    public static final String SITE_LONGITUDE_ENV = "SITE_LONGITUDE";
    public static final String SITE_TIMEZONE_ENV = "SITE_TIMEZONE";
    public static final double DEFAULT_SITE_LATITUDE = 37.8412;
    public static final double DEFAULT_SITE_LONGITUDE = -121.2691;
    public static final String DEFAULT_SITE_TIMEZONE = "America/Los_Angeles";
    public static final int DAYLIGHT_START_AFTER_SUNRISE_MINUTES = 30;   // the array faces south-east: mornings are strong
    public static final int DAYLIGHT_END_BEFORE_SUNSET_MINUTES = 60;
    public static final int MAX_DAYLIGHT_MARGIN_MINUTES = 180;   // the margins are adjustable from 0 to this

    // ---- History and savings ----
    public static final String HISTORY_FILE_ENV = "HISTORY_FILE";
    public static final String DEFAULT_HISTORY_FILE = "solar-charger-history.tsv";
    public static final int HISTORY_SAVE_INTERVAL_MINUTES = 30;   // the full save; the journal (below) covers the time in between
    public static final int HISTORY_JOURNAL_INTERVAL_MINUTES = 1;
    /** What a kWh costs, in cents; the savings shown are the sun's share of the car's energy at this price. */
    public static final int DEFAULT_RATE_CENTS = 30;
    public static final int MAX_RATE_CENTS = 200;
    public static final int MAX_GOAL_KWH = 100;   // the largest departure goal, in kWh

    // ---- Saved state ----
    public static final String STATE_FILE_ENV = "STATE_FILE";
    public static final String DEFAULT_STATE_FILE = "solar-charger-state.json";
    public static final int STATE_SAVE_INTERVAL_SECONDS = 5;
    /** Even when nothing changed, refresh a charger's saved timestamp this often. */
    public static final long STATE_REFRESH_MILLIS = 5 * 60_000L;
    /** A saved charger state older than this is ignored, as the car may have left meanwhile. */
    public static final long STATE_MAX_AGE_MILLIS = 60 * 60_000L;

    /** After a charger connects, ask it for its real status this many seconds later, and again later if still unsure. */
    public static final int CHARGER_RESYNC_FIRST_SECONDS = 5;
    public static final int CHARGER_RESYNC_SECOND_SECONDS = 35;

    // ---- Enphase outage handling ----
    /** After this long without a successful reading, stop charging (ECO and MAX_SOLAR). */
    public static final int ENPHASE_FAILSAFE_AFTER_SECONDS = 150;
    /** While the gateway stays unreachable, repeat the error log at most this often. */
    public static final int ENPHASE_ERROR_LOG_INTERVAL_SECONDS = 60;

    // ---- ECO / MAX_SOLAR smoothing and hysteresis ----
    // Two moving averages of the available current: the slow one governs increases, the fast one
    // governs decreases, so we back off quickly when importing but ramp up cautiously. They are
    // defined by time constants, so behaviour does not depend on the poll interval.
    public static final double SLOW_AVERAGE_SECONDS = 180;
    public static final double FAST_AVERAGE_SECONDS = 60;
    /** A new limit is only sent if it differs from the current one by at least this many amps. */
    public static final int DEADBAND_AMPS = 2;
    /**
     * Stepping down reacts to a smaller gap than stepping up. Above the minimum current the only thing keeping grid
     * import within the allowance is this step-down, so it is kept tight; increases stay cautious.
     */
    public static final int DECREASE_DEADBAND_AMPS = 1;
    /** Minimum time between two limit changes: slow for increases, short for decreases. */
    public static final int MIN_INCREASE_INTERVAL_SECONDS = 60;
    public static final int MIN_DECREASE_INTERVAL_SECONDS = 30;

    /**
     * ECO counts this much grid power as free: the controller aims to keep grid import at or below
     * it rather than at zero. This lets ECO start on a smaller real surplus and ride through small
     * imports, at the cost of drawing up to this much from the grid while charging.
     */
    public static final int ECO_GRID_ALLOWANCE_WATTS = 1000;
    /** Upper limit accepted from the dashboard. */
    public static final int MAX_GRID_ALLOWANCE_WATTS = 1000;

    /** ECO starts only when the smoothed surplus stays at or above this for the sustain time. */
    public static final int ECO_START_AMPS = 7;
    public static final int ECO_START_SUSTAIN_SECONDS = 180;   // spare solar must hold this long before a charge starts
    public static final int ECO_MIN_OFF_SECONDS = 120;
    public static final int STOP_SUSTAIN_SECONDS = 300;   // a shortfall must last this long before a charge stops (it runs at the minimum meanwhile)

    /** A charger's own current reading is trusted for this long after its last MeterValues. */
    public static final long MEASUREMENT_MAX_AGE_MILLIS = 120_000;

    /** Unit used in OCPP charging schedules: "A" (amps) or "W" (watts). */
    public static final String CHARGING_RATE_UNIT = "A";

    /** Heartbeat interval we ask chargers to use (sent in the BootNotification reply). */
    public static final int HEARTBEAT_INTERVAL_SECONDS = 60;

    /**
     * How long a charger connection may stay silent before the server closes it.
     * Kept at several times the heartbeat interval so one or two missed heartbeats are tolerated.
     */
    public static final java.time.Duration WEBSOCKET_IDLE_TIMEOUT = java.time.Duration.ofMinutes(5);

    public static final String DEFAULT_CHARGER_ID = "DEFAULT_BOX";

    private AppConfig() {}

    /** Where the state file lives; override with the STATE_FILE variable. Relative paths use the working directory. */
    public static java.nio.file.Path stateFile() {
        String value = System.getenv(STATE_FILE_ENV);
        return java.nio.file.Path.of(value == null || value.isBlank() ? DEFAULT_STATE_FILE : value.trim());
    }

    public static java.nio.file.Path historyFile() {
        String value = System.getenv(HISTORY_FILE_ENV);
        return java.nio.file.Path.of(value == null || value.isBlank() ? DEFAULT_HISTORY_FILE : value.trim());
    }

    // Where the owner said the panels are (saved from the dashboard); wins over the environment variables and the built-in default.
    private static volatile Double savedLatitude, savedLongitude;
    private static volatile String savedZone;

    /** Called once at start-up with the saved site (the zone is ignored if Java does not know it). */
    public static void useSite(Double latitude, Double longitude, String zone) {
        savedLatitude = latitude;
        savedLongitude = longitude;
        try {
            if (zone != null) java.time.ZoneId.of(zone);
            savedZone = zone;
        } catch (java.time.DateTimeException e) {
            savedZone = null;
        }
    }

    public static double siteLatitude() {
        Double saved = savedLatitude;
        if (saved != null) return saved;
        return envNumber(SITE_LATITUDE_ENV, DEFAULT_SITE_LATITUDE, -90, 90);
    }

    public static double siteLongitude() {
        Double saved = savedLongitude;
        if (saved != null) return saved;
        return envNumber(SITE_LONGITUDE_ENV, DEFAULT_SITE_LONGITUDE, -180, 180);
    }

    public static java.time.ZoneId siteZone() {
        String saved = savedZone;
        if (saved != null) return java.time.ZoneId.of(saved);
        String value = System.getenv(SITE_TIMEZONE_ENV);
        String name = value == null || value.isBlank() ? DEFAULT_SITE_TIMEZONE : value.trim();
        try {
            return java.time.ZoneId.of(name);
        } catch (java.time.DateTimeException e) {
            throw new IllegalStateException("Environment variable " + SITE_TIMEZONE_ENV + " is not a timezone: " + name);
        }
    }

    private static double envNumber(String name, double fallback, double min, double max) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) return fallback;
        try {
            double number = Double.parseDouble(value.trim());
            if (number < min || number > max) throw new NumberFormatException();
            return number;
        } catch (NumberFormatException e) {
            throw new IllegalStateException("Environment variable " + name + " must be a number from " + min + " to " + max + ": " + value);
        }
    }

    /** Mains voltage used to convert watts to amps; override with the HOUSEHOLD_VOLTAGE variable. */
    public static double voltage() {
        String value = System.getenv(VOLTAGE_ENV);
        if (value == null || value.isBlank()) return DEFAULT_VOLTAGE;
        try {
            return Double.parseDouble(value.trim());
        } catch (NumberFormatException e) {
            throw new IllegalStateException("Environment variable " + VOLTAGE_ENV + " is not a number: " + value);
        }
    }

    public static String enphaseToken() {
        String token = System.getenv(ENPHASE_TOKEN_ENV);
        if (token == null || token.isBlank()) {
            throw new IllegalStateException("Environment variable " + ENPHASE_TOKEN_ENV + " is not set.");
        }
        return token;
    }
}
