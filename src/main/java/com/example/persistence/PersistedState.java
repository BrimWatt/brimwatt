package com.example.persistence;

import com.example.model.ChargingMode;
import com.example.wallbox.ChargerSnapshot;

import java.util.Map;

/** Everything saved to the state file. */
public record PersistedState(ChargingMode mode, int fastModeAmps, Map<String, ChargerSnapshot> chargers,
                             Integer gridAllowanceWatts, Boolean daylightWindowEnabled, Boolean paused,
                             Integer rateCents, Integer daylightStartMinutes, Integer daylightEndMinutes,
                             Integer maxAmps, Integer peakRateCents, Integer peakStartHour, Integer peakEndHour,
                             Integer goalKwh, Integer goalMinutes,
                             String notifyUrl) {}
