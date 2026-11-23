package com.wattsync.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

/** One meter reading from the Envoy gateway: the active power in watts as the gateway reports it. Fields we do not use are ignored. */
@JsonIgnoreProperties(ignoreUnknown = true)
public class EnphaseMeterReading {
    public double activePower;
}
