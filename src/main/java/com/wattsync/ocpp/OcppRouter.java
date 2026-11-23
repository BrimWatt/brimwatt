package com.wattsync.ocpp;

import com.wattsync.wallbox.Charger;
import com.wattsync.wallbox.OcppMessages;
import com.wattsync.wallbox.WallboxController;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;

/** Parses incoming OCPP frames and dispatches them. */
public class OcppRouter {

    private static final Logger logger = LoggerFactory.getLogger(OcppRouter.class);
    private static final ObjectMapper MAPPER = new ObjectMapper();

    private final Map<String, OcppHandler> handlers;

    public OcppRouter(List<OcppHandler> handlers) {
        this.handlers = handlers.stream().collect(Collectors.toMap(OcppHandler::action, Function.identity()));
    }

    public void onMessage(Charger charger, String text) {
        JsonNode frame;
        try {
            frame = MAPPER.readTree(text);
        } catch (Exception e) {
            logger.warn("Charger {} sent invalid JSON: {}", charger.id(), text);
            return;
        }
        if (!frame.isArray() || frame.size() < 3) {
            logger.warn("Charger {} sent a malformed frame: {}", charger.id(), text);
            return;
        }

        int type = frame.get(0).asInt();
        String messageId = frame.get(1).asText();
        switch (type) {
            case OcppMessages.CALL -> onCall(charger, messageId, frame.get(2).asText(),
                    frame.size() > 3 ? frame.get(3) : MAPPER.createObjectNode());
            case OcppMessages.CALL_RESULT -> onCallResult(charger, messageId, frame.get(2));
            case OcppMessages.CALL_ERROR -> onCallError(charger, messageId, text);
            default -> logger.warn("Charger {} sent unknown frame type {}: {}", charger.id(), type, text);
        }
    }

    private void onCall(Charger charger, String messageId, String action, JsonNode payload) {
        if ("Heartbeat".equals(action) || "MeterValues".equals(action)) {
            logger.debug("[OCPP] {} from charger {}: {}", action, charger.id(), payload);
        } else {
            logger.info("[OCPP] {} from charger {}: {}", action, charger.id(), payload);
        }

        OcppHandler handler = handlers.get(action);
        if (handler == null) {
            logger.warn("[OCPP] No handler for action {}; replying NotImplemented.", action);
            charger.send(OcppMessages.callError(messageId, "NotImplemented", action + " is not supported"));
            return;
        }
        try {
            charger.send(OcppMessages.callResult(messageId, action, handler.handle(charger, payload)));
        } catch (Exception e) {
            logger.error("[OCPP] Handler for {} failed: ", action, e);
            charger.send(OcppMessages.callError(messageId, "InternalError", "Failed to process " + action));
        }
    }

    private void onCallError(Charger charger, String messageId, String text) {
        String action = charger.forgetCall(messageId);
        if (WallboxController.METER_TRIGGER.equals(action)) {
            charger.markMeterTriggerUnsupported();
            logger.warn("[OCPP] Charger {} cannot report meter values on request; using the limit we set as its draw.",
                    charger.id());
            return;
        }
        logger.error("[OCPP] Charger {} rejected {}: {}", charger.id(), action, text);
    }

    private void onCallResult(Charger charger, String messageId, JsonNode payload) {
        String action = charger.forgetCall(messageId);
        if (action == null) {
            logger.debug("[OCPP] Ignoring unmatched response {} from charger {}", messageId, charger.id());
            return;
        }
        if (WallboxController.METER_TRIGGER.equals(action)) {
            if (!"Accepted".equals(payload.path("status").asText())) {
                charger.markMeterTriggerUnsupported();
                logger.warn("[OCPP] Charger {} declined a meter values request ({}); using the limit we set as its draw.",
                        charger.id(), payload.path("status").asText(payload.toString()));
            } else {
                logger.debug("[OCPP] Charger {} accepted a meter values request.", charger.id());
            }
            return;
        }
        logger.info("[OCPP] Charger {} answered {}: {}", charger.id(), action, payload.path("status").asText(payload.toString()));
    }
}
