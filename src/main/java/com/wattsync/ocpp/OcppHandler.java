package com.wattsync.ocpp;

import com.wattsync.wallbox.Charger;
import com.fasterxml.jackson.databind.JsonNode;

import java.util.Map;

/** Handles one action the charger initiates and returns the reply payload. */
public interface OcppHandler {

    String action();

    Map<String, Object> handle(Charger charger, JsonNode payload);
}
