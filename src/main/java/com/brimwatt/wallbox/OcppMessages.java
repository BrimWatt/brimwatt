package com.brimwatt.wallbox;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.security.SecureRandom;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/**
 * Builds OCPP 1.6J frames. Replies to the charger use the layout
 * {@code [3, id, action, payload]}, matching the previous Brimwatt project.
 */
public final class OcppMessages {

    public static final int CALL = 2;
    public static final int CALL_RESULT = 3;
    public static final int CALL_ERROR = 4;

    private static final ObjectMapper MAPPER = new ObjectMapper();
    private static final SecureRandom RANDOM = new SecureRandom();
    private static final DateTimeFormatter TIME_FORMAT =
            DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'");

    private static final int CONNECTOR_ID = 1;
    private static final int STACK_LEVEL = 1;

    private OcppMessages() {}

    public static String currentTime() {
        return ZonedDateTime.now(ZoneOffset.UTC).format(TIME_FORMAT);
    }

    // ---- replies to the charger ----

    public static String callResult(String messageId, String action, Map<String, Object> payload) {
        return toJson(List.of(CALL_RESULT, messageId, action, payload));
    }

    public static String callError(String messageId, String code, String description) {
        return toJson(List.of(CALL_ERROR, messageId, code, description, Map.of()));
    }

    // ---- commands to the charger ----

    public static String setChargingProfile(String messageId, int transactionId, int amps, String unit) {
        Map<String, Object> profile = chargingProfile(0, amps, unit);
        profile.put("transactionId", transactionId);
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("connectorId", CONNECTOR_ID);
        payload.put("csChargingProfiles", profile);
        return toJson(List.of(CALL, messageId, "SetChargingProfile", payload));
    }

    public static String remoteStartTransaction(String messageId, int amps, String unit) {
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("idTag", UUID.randomUUID().toString());
        payload.put("connectorId", CONNECTOR_ID);
        payload.put("chargingProfile", chargingProfile(RANDOM.nextInt(Integer.MAX_VALUE), amps, unit));
        return toJson(List.of(CALL, messageId, "RemoteStartTransaction", payload));
    }

    public static String remoteStopTransaction(String messageId, int transactionId) {
        return toJson(List.of(CALL, messageId, "RemoteStopTransaction", Map.of("transactionId", transactionId)));
    }

    /** Asks the charger to send a message it would normally send on its own (e.g. StatusNotification). */
    public static String triggerMessage(String messageId, String requestedMessage) {
        return toJson(List.of(CALL, messageId, "TriggerMessage",
                Map.of("requestedMessage", requestedMessage, "connectorId", CONNECTOR_ID)));
    }

    public static String newMessageId() {
        return UUID.randomUUID().toString();
    }

    private static Map<String, Object> chargingProfile(int profileId, int amps, String unit) {
        Map<String, Object> period = new LinkedHashMap<>();
        period.put("limit", amps);
        period.put("startPeriod", 0);

        Map<String, Object> schedule = new LinkedHashMap<>();
        schedule.put("chargingRateUnit", unit);
        schedule.put("minChargingRate", amps);
        schedule.put("chargingSchedulePeriod", List.of(period));

        Map<String, Object> profile = new LinkedHashMap<>();
        profile.put("chargingProfileId", profileId);
        profile.put("stackLevel", STACK_LEVEL);
        profile.put("chargingProfileKind", "Absolute");
        profile.put("chargingProfilePurpose", "TxProfile");
        profile.put("chargingSchedule", schedule);
        return profile;
    }

    private static String toJson(Object frame) {
        try {
            return MAPPER.writeValueAsString(frame);
        } catch (JsonProcessingException e) {
            throw new IllegalStateException("Failed to serialize OCPP frame", e);
        }
    }
}
