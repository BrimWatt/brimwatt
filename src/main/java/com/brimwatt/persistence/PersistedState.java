package com.brimwatt.persistence;

import com.brimwatt.model.ChargingMode;
import com.brimwatt.wallbox.ChargerSnapshot;

import java.util.Map;

/** Everything saved to the state file. */
public record PersistedState(ChargingMode mode, int fastModeAmps, Map<String, ChargerSnapshot> chargers,
                             Integer gridAllowanceWatts, Boolean daylightWindowEnabled, Boolean paused,
                             Integer rateCents, Integer daylightStartMinutes, Integer daylightEndMinutes,
                             Integer maxAmps, Integer peakRateCents, Integer peakStartHour, Integer peakEndHour,
                             Integer goalKwh, Integer goalMinutes,
                             String notifyUrl, Boolean peakWeekends,
                             Double siteLat, Double siteLon, String siteTz, String siteName) {}
