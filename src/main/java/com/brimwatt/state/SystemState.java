package com.brimwatt.state;

import com.brimwatt.config.AppConfig;
import com.brimwatt.model.ChargingMode;

/** Shared mutable state read by the web layer and the polling loop. */
public class SystemState {

    private volatile ChargingMode mode = ChargingMode.ECO;
    private volatile double solarWatts = 0.0;
    private volatile int fastModeAmps = AppConfig.DEFAULT_FAST_AMPS;
    private volatile int maxAmps = AppConfig.MAX_FAST_AMPS;
    private volatile int gridAllowanceWatts = AppConfig.ECO_GRID_ALLOWANCE_WATTS;
    private volatile boolean daylightWindowEnabled = true;
    private volatile int daylightStartMinutes = AppConfig.DAYLIGHT_START_AFTER_SUNRISE_MINUTES;
    private volatile int daylightEndMinutes = AppConfig.DAYLIGHT_END_BEFORE_SUNSET_MINUTES;
    private volatile boolean paused;
    private volatile int rateCents = AppConfig.DEFAULT_RATE_CENTS;
    private volatile String notifyUrl = "";
    private volatile int goalKwh = 0;
    private volatile int goalMinutes = 420;   // 7:00 am
    private volatile double goalDoneWh = 0;
    private volatile boolean goalForcing = false;
    private volatile int goalForcedAmps = 0;
    private volatile int peakRateCents = 0;
    private volatile int peakStartHour = 16;
    private volatile int peakEndHour = 21;
    private volatile Double siteLatitude, siteLongitude;   // where the owner said the panels are; null until saved
    private volatile String siteTimezone, siteName = "";
    private volatile boolean peakWeekends = true;   // false: the expensive hours only apply Monday to Friday

    /** Result of the latest control decision, shown on the dashboard. */
    public record Control(double availableAmps, double smoothedAmps, int targetAmps, String reason,
                          double gridWatts, long readingAtMillis, int waitSeconds) {
        /** No timer is running (-1), as in Custom, paused or a departure goal. */
        public Control(double availableAmps, double smoothedAmps, int targetAmps, String reason,
                       double gridWatts, long readingAtMillis) {
            this(availableAmps, smoothedAmps, targetAmps, reason, gridWatts, readingAtMillis, -1);
        }
    }

    private volatile Control control;
    private volatile boolean failSafe;
    private volatile boolean enphaseOk = true;
    private volatile String enphaseMessage = "";

    public ChargingMode mode() { return mode; }
    public void setMode(ChargingMode mode) { this.mode = mode; }

    public double solarWatts() { return solarWatts; }
    public void setSolarWatts(double solarWatts) { this.solarWatts = solarWatts; }

    /** Grid power ECO treats as free; adjustable from the dashboard. */
    public int gridAllowanceWatts() { return gridAllowanceWatts; }
    public void setGridAllowanceWatts(int watts) { this.gridAllowanceWatts = watts; }

    /** Whether ECO and MAX_SOLAR pause outside the daylight window. */
    public boolean daylightWindowEnabled() { return daylightWindowEnabled; }
    public void setDaylightWindowEnabled(boolean enabled) { this.daylightWindowEnabled = enabled; }

    /** Minutes after sunrise that the window opens, and before sunset that it closes. */
    public int daylightStartMinutes() { return daylightStartMinutes; }
    public int daylightEndMinutes() { return daylightEndMinutes; }
    public void setDaylightMargins(int startMinutes, int endMinutes) {
        this.daylightStartMinutes = startMinutes;
        this.daylightEndMinutes = endMinutes;
    }

    /**
     * The kill switch: while paused nothing is ever charged, whatever the mode. The mode itself is untouched, so
     * resuming simply hands the charger back to it.
     */
    public boolean paused() { return paused; }
    public void setPaused(boolean paused) { this.paused = paused; }

    /** What a kWh costs, in cents; the savings are the sun's share of the car's energy valued at this price. */
    /** The peak price in cents per kWh, or 0 for no peak pricing; and the hours it applies (end is exclusive, 1 to 24). */
    public int peakRateCents() { return peakRateCents; }
    public int peakStartHour() { return peakStartHour; }
    public int peakEndHour() { return peakEndHour; }
    public void setPeakRate(int cents, int startHour, int endHour) {
        this.peakRateCents = cents; this.peakStartHour = startHour; this.peakEndHour = endHour;
    }

    /** Whether the expensive hours also apply on Saturday and Sunday. */
    public boolean siteSet() { return siteTimezone != null && siteLatitude != null && siteLongitude != null; }
    public Double siteLatitude() { return siteLatitude; }
    public Double siteLongitude() { return siteLongitude; }
    public String siteTimezone() { return siteTimezone; }
    public String siteName() { return siteName; }
    public void setSite(double latitude, double longitude, String timezone, String name) {
        this.siteLatitude = latitude; this.siteLongitude = longitude; this.siteTimezone = timezone; this.siteName = name == null ? "" : name;
    }
    public boolean peakWeekends() { return peakWeekends; }
    public void setPeakWeekends(boolean weekends) { this.peakWeekends = weekends; }

    /** The departure goal: at least {@code goalKwh} kWh (0 = no goal) by {@code goalMinutes} after local midnight. */
    /** The web address notifications are posted to, or empty for none. */
    public String notifyUrl() { return notifyUrl; }
    public void setNotifyUrl(String url) { this.notifyUrl = url == null ? "" : url; }

    public int goalKwh() { return goalKwh; }
    public int goalMinutes() { return goalMinutes; }
    public void setGoal(int kwh, int minutes) { this.goalKwh = kwh; this.goalMinutes = minutes; }
    /** What the goal looks like right now, for the dashboard: energy delivered toward it, and whether it has taken over. */
    public double goalDoneWh() { return goalDoneWh; }
    public boolean goalForcing() { return goalForcing; }
    public int goalForcedAmps() { return goalForcedAmps; }
    public void setGoalProgress(double doneWh, boolean forcing, int forcedAmps) {
        this.goalDoneWh = doneWh; this.goalForcing = forcing; this.goalForcedAmps = forcedAmps;
    }

    public int rateCents() { return rateCents; }
    public void setRateCents(int cents) { this.rateCents = cents; }

    /** Null until the first successful reading. */
    public Control control() { return control; }
    public void setControl(Control control) { this.control = control; }

    public boolean failSafe() { return failSafe; }
    public void setFailSafe(boolean failSafe) { this.failSafe = failSafe; }

    public boolean enphaseOk() { return enphaseOk; }
    public String enphaseMessage() { return enphaseMessage; }
    public void setEnphaseStatus(boolean ok, String message) {
        this.enphaseMessage = message;
        this.enphaseOk = ok;
    }

    /** The most current the charger is ever asked for, in any mode (ECO and Max solar are also limited to 32 A). */
    public int maxAmps() { return maxAmps; }
    public void setMaxAmps(int maxAmps) { this.maxAmps = maxAmps; }

    public int fastModeAmps() { return fastModeAmps; }
    public void setFastModeAmps(int fastModeAmps) { this.fastModeAmps = fastModeAmps; }
}
