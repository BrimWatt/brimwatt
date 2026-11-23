package com.wattsync.wallbox;

import com.wattsync.config.AppConfig;
import com.wattsync.model.ChargingMode;
import com.wattsync.state.SystemState;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Turns a desired charging current into OCPP commands: remote start when a vehicle is waiting,
 * a charging profile update while charging, and remote stop when there is nothing to give.
 */
public class WallboxController implements ChargerGateway {

    private static final Logger logger = LoggerFactory.getLogger(WallboxController.class);
    private static final long REMOTE_START_RETRY_MILLIS = 30_000;
    /** Name under which a MeterValues request is remembered, so a refusal can be matched to it. */
    public static final String METER_TRIGGER = "TriggerMessage(MeterValues)";

    private final WallboxSessionRegistry sessions;
    private final SystemState state;
    private volatile String lastCommand = "None yet";
    private volatile long lastCommandAtMillis;
    private volatile com.wattsync.history.HistoryService history;
    private volatile String context = "";

    public WallboxController(WallboxSessionRegistry sessions, SystemState state) {
        this.sessions = sessions;
        this.state = state;
    }

    /** Where to record what was sent (the activity log). Optional. */
    public void setHistory(com.wattsync.history.HistoryService history) { this.history = history; }

    @Override
    public void setCommandContext(String context) { this.context = context == null ? "" : context; }

    @Override
    public boolean hasConnectedChargers() {
        return !sessions.isEmpty();
    }

    @Override
    public void refreshMeasurements() {
        sessions.forEach((id, charger) -> {
            if (charger.status() != Charger.Status.CHARGING || charger.meterTriggerUnsupported() || !charger.isOpen()) return;
            try {
                send(charger, METER_TRIGGER, messageId -> OcppMessages.triggerMessage(messageId, "MeterValues"));
            } catch (Exception e) {
                logger.warn("Could not ask charger {} for meter values: {}", id, e.getMessage());
            }
        });
    }

    @Override
    public boolean drawIsMeasured() {
        boolean[] measured = {false};
        sessions.forEach((id, charger) -> {
            if (charger.status() == Charger.Status.CHARGING
                    && charger.freshMeasuredAmps(AppConfig.MEASUREMENT_MAX_AGE_MILLIS).isPresent()) {
                measured[0] = true;
            }
        });
        return measured[0];
    }

    /** Total current the connected chargers are drawing right now (measured if reported, else the limit we set). */
    @Override
    public double currentDrawAmps() {
        double[] total = {0};
        sessions.forEach((id, charger) -> {
            if (charger.status() != Charger.Status.CHARGING) return;
            total[0] += charger.freshMeasuredAmps(AppConfig.MEASUREMENT_MAX_AGE_MILLIS)
                    .orElse(charger.appliedAmps() == null ? 0 : charger.appliedAmps());
        });
        return total[0];
    }

    /**
     * The limit in effect on a charger that is currently charging, or null if none is. If we never
     * set a limit (for example after a restart) the measured current stands in for it, and failing
     * that the maximum is assumed, so the controller sets a real limit on its next decision.
     */
    @Override
    public Integer currentLimitAmps() {
        Integer[] limit = {null};
        sessions.forEach((id, charger) -> {
            if (charger.status() != Charger.Status.CHARGING) return;
            if (charger.appliedAmps() != null) {
                limit[0] = charger.appliedAmps();
            } else {
                limit[0] = charger.freshMeasuredAmps(AppConfig.MEASUREMENT_MAX_AGE_MILLIS)
                        .stream().mapToInt(a -> (int) Math.ceil(a)).findFirst().orElse(AppConfig.MAX_AMPS);
            }
        });
        return limit[0];
    }

    /** Status of the charger that matters most for display: a charging one first, otherwise any. */
    public Charger.Status primaryStatus() {
        Charger.Status[] result = {null};
        sessions.forEach((id, charger) -> {
            if (result[0] == null || charger.status() == Charger.Status.CHARGING) result[0] = charger.status();
        });
        return result[0];
    }

    @Override
    public boolean carPluggedIn() {
        Charger.Status status = primaryStatus();
        return status == Charger.Status.PREPARING || status == Charger.Status.CHARGING || status == Charger.Status.SUSPENDED_EV
                || status == Charger.Status.SUSPENDED_EVSE || status == Charger.Status.FINISHING;
    }

    /** True if any connected charger is still showing state restored from disk. */
    public boolean anyRestored() {
        boolean[] any = {false};
        sessions.forEach((id, charger) -> any[0] |= charger.isRestored());
        return any[0];
    }

    /**
     * Asks a charger to report its current status and meter values, so we learn what it is really
     * doing after we (or the connection) restarted. Chargers that don't support this answer with an
     * error, which is only logged; the charger's own regular messages fill the gap eventually.
     */
    public void requestStatus(Charger charger) {
        if (!charger.isOpen()) return;
        try {
            send(charger, "TriggerMessage(StatusNotification)",
                    id -> OcppMessages.triggerMessage(id, "StatusNotification"));
            send(charger, METER_TRIGGER, id -> OcppMessages.triggerMessage(id, "MeterValues"));
            logger.info("[SYNC] Asked charger {} for its current status and meter values.", charger.id());
        } catch (Exception e) {
            logger.warn("[SYNC] Could not ask charger {} for its status: {}", charger.id(), e.getMessage());
        }
    }

    /** Plain-language description of the last command sent to a charger, e.g. "Started charging at 10 A". */
    public String lastCommand() { return lastCommand; }

    /** When the last command was sent, in milliseconds since 1970, or 0 if none yet. */
    public long lastCommandAtMillis() { return lastCommandAtMillis; }

    private void recordCommand(String description) {
        lastCommand = description;
        lastCommandAtMillis = System.currentTimeMillis();
        com.wattsync.history.HistoryService h = history;
        if (h != null) h.addEvent(context.isEmpty() ? description : description + ": " + context);
    }

    /** Applies the same current limit to every connected charger. */
    @Override
    public void sendChargingLimit(int amps) {
        sessions.forEach((chargerId, charger) -> apply(charger, amps));
    }

    /**
     * Called when a charger reports a new status. In FAST mode nothing polls, so the fixed
     * limit is applied here as soon as the charger is ready.
     */
    public void onChargerChanged(Charger charger) {
        if (state.paused()) {
            context = "paused by you";
            apply(charger, 0);   // the kill switch beats every mode, including a car that starts by itself
            context = "";
        } else if (state.mode() == ChargingMode.FAST) {
            context = "custom current";
            apply(charger, state.fastModeAmps());
            context = "";
        } else if (state.mode() == ChargingMode.CHEAPEST) {
            int amps = com.wattsync.charging.CheapHours.expensive(System.currentTimeMillis(), AppConfig.siteZone(), state.peakStartHour(),
                    state.peakEndHour(), state.peakWeekends()) ? 0 : state.maxAmps();
            context = amps > 0 ? "cheap hours" : "expensive hours";
            apply(charger, amps);
            context = "";
        }
    }

    private void apply(Charger charger, int amps) {
        if (!charger.isOpen()) return;
        try {
            if (amps <= 0) {
                stop(charger);
            } else if (charger.status() == Charger.Status.CHARGING) {
                updateLimit(charger, amps);
            } else if (charger.status().canStartCharging()) {
                start(charger, amps);
            } else {
                logger.debug("Charger {} is {}; nothing to apply.", charger.id(), charger.status());
            }
        } catch (Exception e) {
            logger.error("[DISPATCH EXCEPTION] Failed to send to charger {}: {}", charger.id(), e.getMessage());
        }
    }

    private void start(Charger charger, int amps) {
        long sinceLastStart = System.currentTimeMillis() - charger.lastRemoteStartMillis();
        if (sinceLastStart < REMOTE_START_RETRY_MILLIS) return;

        send(charger, "RemoteStartTransaction",
                id -> OcppMessages.remoteStartTransaction(id, amps, AppConfig.CHARGING_RATE_UNIT));
        charger.markRemoteStart();
        charger.setAppliedAmps(amps);
        recordCommand("Started charging at " + amps + " A");
        logger.info("[DISPATCH] RemoteStartTransaction ({}A) -> charger {}", amps, charger.id());
    }

    private void updateLimit(Charger charger, int amps) {
        Integer transactionId = charger.transactionId();
        if (transactionId == null) {
            logger.warn("Charger {} is charging but no transaction is known; cannot set a limit.", charger.id());
            return;
        }
        if (Integer.valueOf(amps).equals(charger.appliedAmps())) return;

        send(charger, "SetChargingProfile",
                id -> OcppMessages.setChargingProfile(id, transactionId, amps, AppConfig.CHARGING_RATE_UNIT));
        charger.setAppliedAmps(amps);
        recordCommand("Changed limit to " + amps + " A");
        logger.info("[DISPATCH] SetChargingProfile ({}A, txn {}) -> charger {}", amps, transactionId, charger.id());
    }

    private void stop(Charger charger) {
        Integer transactionId = charger.transactionId();
        if (charger.status() != Charger.Status.CHARGING) return;
        if (transactionId == null) {
            logger.warn("Charger {} is charging but no transaction is known; cannot stop it. "
                    + "Waiting for it to report one.", charger.id());
            return;
        }

        send(charger, "RemoteStopTransaction", id -> OcppMessages.remoteStopTransaction(id, transactionId));
        recordCommand("Stopped charging");
        logger.info("[DISPATCH] RemoteStopTransaction (txn {}) -> charger {}", transactionId, charger.id());
    }

    private void send(Charger charger, String action, java.util.function.Function<String, String> frame) {
        String messageId = OcppMessages.newMessageId();
        charger.rememberCall(messageId, action);
        charger.send(frame.apply(messageId));
    }
}
