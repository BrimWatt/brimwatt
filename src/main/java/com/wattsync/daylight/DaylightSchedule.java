package com.wattsync.daylight;

import java.time.Instant;

/** Tells whether solar-following modes should be active right now. */
public interface DaylightSchedule {

    DaylightSchedule ALWAYS_OPEN = new DaylightSchedule() {
        public boolean isOpen(Instant now) { return true; }
        public String describe(Instant now) { return "always open"; }
    };

    boolean isOpen(Instant now);

    /** Today's window as {start, end} in epoch seconds, for drawing it on a chart; empty if there is none. */
    default java.util.Optional<long[]> windowFor(Instant now) { return java.util.Optional.empty(); }

    /** Changes how long after sunrise the window opens and how long before sunset it closes. */
    default void setMargins(java.time.Duration startAfterSunrise, java.time.Duration endBeforeSunset) {}

    /** Today's window as text for logs and the dashboard, e.g. "08:12 to 16:55". */
    String describe(Instant now);
}
