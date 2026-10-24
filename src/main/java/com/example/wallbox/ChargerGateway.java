package com.example.wallbox;

/** What the polling loop needs from the charger side. */
public interface ChargerGateway {

    boolean hasConnectedChargers();

    /** Total current the chargers are drawing right now. */
    double currentDrawAmps();

    /** The limit in effect on a charger that is currently charging, or null if none is. */
    Integer currentLimitAmps();

    /** Asks charging chargers to report their real current now; the answer arrives a moment later. */
    default void refreshMeasurements() {}

    /** True if the draw reported by {@link #currentDrawAmps()} comes from the charger, not from our own limit. */
    default boolean drawIsMeasured() { return false; }

    /** The reason shown in the activity log for the commands sent next ("enough solar"); empty clears it. */
    default void setCommandContext(String context) {}

    /** True if a car is plugged in (preparing, charging, paused or finishing); the departure goal needs one. */
    default boolean carPluggedIn() { return false; }

    /** Applies a current limit to every connected charger; 0 means stop charging. */
    void sendChargingLimit(int amps);
}
