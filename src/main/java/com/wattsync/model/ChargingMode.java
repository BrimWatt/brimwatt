package com.wattsync.model;

import com.fasterxml.jackson.annotation.JsonCreator;

import java.util.Optional;

/** The charging modes. CHEAPEST ("Cheapest hours") charges at the maximum current outside the expensive hours and needs no solar meter. MAX_SOLAR is shown as "Max solar"; it used to be called BALANCED, and that name is still accepted. */
public enum ChargingMode {
    ECO, MAX_SOLAR, FAST, CHEAPEST;

    public static Optional<ChargingMode> parse(String value) {
        if (value == null) return Optional.empty();
        String name = value.trim().toUpperCase().replace(' ', '_');
        if (name.equals("CHEAP") || name.equals("CHEAPEST_HOURS")) return Optional.of(CHEAPEST);   // "Cheapest hours" in words
        if (name.equals("BALANCED") || name.equals("MAXSOLAR")) return Optional.of(MAX_SOLAR);   // the old name, and a friendly spelling
        try {
            return Optional.of(valueOf(name));
        } catch (IllegalArgumentException e) {
            return Optional.empty();
        }
    }

    /** Reads a mode from a saved state file, so files written before the rename (BALANCED) still load. */
    @JsonCreator
    static ChargingMode fromJson(String value) {
        return parse(value).orElseThrow(() -> new IllegalArgumentException("Unknown charging mode: " + value));
    }
}
