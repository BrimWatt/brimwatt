package com.wattsync.ocpp;

import com.wattsync.config.AppConfig;
import com.wattsync.wallbox.Charger;
import com.wattsync.wallbox.OcppMessages;
import com.fasterxml.jackson.databind.JsonNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;

/** The OCPP 1.6 core actions a charger initiates. */
public final class CoreHandlers {

    private static final Logger logger = LoggerFactory.getLogger(CoreHandlers.class);

    private CoreHandlers() {}

    /** @param onChargerChanged called after the charger's status or transaction changes */
    public static List<OcppHandler> all(Consumer<Charger> onChargerChanged) {
        return all(onChargerChanged, null);
    }

    /** @param history if given, plugging in and unplugging the car are added to the activity log */
    public static List<OcppHandler> all(Consumer<Charger> onChargerChanged,
                                        com.wattsync.history.HistoryService history) {
        // Seed from the clock so transaction ids are not reused after a restart.
        AtomicInteger nextTransactionId = new AtomicInteger((int) (System.currentTimeMillis() / 1000));

        return List.of(
                handler("BootNotification", (c, p) -> Map.of(
                        "status", "Accepted",
                        "currentTime", OcppMessages.currentTime(),
                        "interval", AppConfig.HEARTBEAT_INTERVAL_SECONDS)),

                handler("Heartbeat", (c, p) -> Map.of("currentTime", OcppMessages.currentTime())),

                handler("StatusNotification", (c, p) -> {
                    // Connector 0 describes the whole charge point, not the plug.
                    if (p.path("connectorId").asInt(1) > 0) {
                        Charger.Status status = Charger.Status.parse(p.path("status").asText());
                        boolean wasRestored = c.isRestored();
                        Charger.Status before = c.status();
                        c.setStatus(status);
                        if (history != null) {
                            if (status == Charger.Status.PREPARING && before == Charger.Status.AVAILABLE) {
                                history.addEvent("Car plugged in");
                                history.notePluggedIn();
                            }
                            if (status == Charger.Status.AVAILABLE && before != Charger.Status.AVAILABLE
                                    && before != Charger.Status.UNKNOWN) {
                                history.addEvent("Car unplugged");
                                history.notePluggedOut();
                            }
                        }
                        // No transaction exists in these states, so a restored one must be stale.
                        if (wasRestored && (status == Charger.Status.AVAILABLE
                                || status == Charger.Status.PREPARING || status == Charger.Status.FINISHING)) {
                            c.setTransactionId(null);
                            c.setAppliedAmps(null);
                        }
                        logger.info("Charger {} status: {}", c.id(), status);
                        onChargerChanged.accept(c);
                    }
                    return Map.of();
                }),

                handler("Authorize", (c, p) -> Map.of("idTagInfo", Map.of("status", "Accepted"))),

                handler("StartTransaction", (c, p) -> {
                    int transactionId = nextTransactionId.incrementAndGet();
                    c.setTransactionId(transactionId);
                    logger.info("Charger {} started transaction {}", c.id(), transactionId);
                    if (history != null) history.sessionStarted(p.path("meterStart").asDouble(-1), IdTagHash.of(p.path("idTag").asText("")));   // only a fingerprint is kept
                    onChargerChanged.accept(c);
                    return Map.of("transactionId", transactionId, "idTagInfo", Map.of("status", "Accepted"));
                }),

                handler("StopTransaction", (c, p) -> {
                    logger.info("Charger {} stopped transaction {}", c.id(), c.transactionId());
                    if (history != null) history.sessionStopped(p.path("meterStop").asDouble(-1), p.path("reason").asText(""));
                    c.setTransactionId(null);
                    c.setAppliedAmps(null);
                    return Map.of("idTagInfo", Map.of("status", "Accepted"));
                }),

                handler("MeterValues", (c, p) -> {
                    recordMeasuredCurrent(c, p);
                    return Map.of();
                })
        );
    }

    /** Remembers the current the charger reports delivering (Current.Import, or Power.Active.Import / voltage). */
    private static void recordMeasuredCurrent(Charger charger, JsonNode payload) {
        double amps = -1;
        double watts = -1;
        for (JsonNode meterValue : payload.path("meterValue")) {
            for (JsonNode sample : meterValue.path("sampledValue")) {
                String measurand = sample.path("measurand").asText("");
                double value;
                try {
                    value = Double.parseDouble(sample.path("value").asText());
                } catch (NumberFormatException e) {
                    continue;
                }
                String unit = sample.path("unit").asText("");
                if (measurand.equals("Current.Import")) {
                    amps = Math.max(amps, value);
                } else if (measurand.equals("Power.Active.Import")) {
                    watts = Math.max(watts, "kW".equals(unit) ? value * 1000 : value);
                }
            }
        }
        // A transaction id here lets us control a charge that started before we (re)connected.
        if (payload.hasNonNull("transactionId") && charger.transactionId() == null) {
            charger.setTransactionId(payload.get("transactionId").asInt());
            logger.info("Charger {} reports transaction {}", charger.id(), charger.transactionId());
        }
        if (amps >= 0) {
            charger.setMeasuredAmps(amps);
            if (amps >= 1 && charger.status() == Charger.Status.UNKNOWN) {
                charger.setStatus(Charger.Status.CHARGING);   // it is delivering current, so it is charging
            }
        } else if (watts >= 0) {
            charger.setMeasuredAmps(watts / AppConfig.voltage());
        }
    }

    private static OcppHandler handler(String action, java.util.function.BiFunction<Charger, JsonNode, Map<String, Object>> fn) {
        return new OcppHandler() {
            public String action() { return action; }
            public Map<String, Object> handle(Charger charger, JsonNode payload) { return fn.apply(charger, payload); }
        };
    }
}
