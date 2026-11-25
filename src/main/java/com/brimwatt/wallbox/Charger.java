package com.brimwatt.wallbox;

import io.javalin.websocket.WsContext;

import java.util.Map;
import java.util.OptionalDouble;
import java.util.concurrent.ConcurrentHashMap;

/** A connected charger: its socket plus what we know about its connector and transaction. */
public class Charger {

    /** Connector states from OCPP 1.6 StatusNotification. */
    public enum Status {
        AVAILABLE("Available"), PREPARING("Preparing"), CHARGING("Charging"),
        SUSPENDED_EV("SuspendedEV"), SUSPENDED_EVSE("SuspendedEVSE"), FINISHING("Finishing"),
        RESERVED("Reserved"), UNAVAILABLE("Unavailable"), FAULTED("Faulted"), UNKNOWN("Unknown");

        private final String ocppName;

        Status(String ocppName) { this.ocppName = ocppName; }

        public static Status parse(String value) {
            for (Status s : values()) {
                if (s.ocppName.equalsIgnoreCase(value)) return s;
            }
            return UNKNOWN;
        }

        public static Status fromName(String name) {
            try {
                return valueOf(name);
            } catch (IllegalArgumentException | NullPointerException e) {
                return UNKNOWN;
            }
        }

        /** States in which a remote start can begin a charge. */
        public boolean canStartCharging() {
            return this == PREPARING || this == SUSPENDED_EV || this == SUSPENDED_EVSE || this == FINISHING;
        }
    }

    private final String id;
    private final WsContext ctx;
    private final Map<String, String> pendingCalls = new ConcurrentHashMap<>();

    private volatile Status status = Status.UNKNOWN;
    private volatile boolean restored;
    private volatile Integer transactionId;
    private volatile Integer appliedAmps;
    private volatile long lastRemoteStartMillis;
    private volatile boolean meterTriggerUnsupported;
    private volatile double measuredAmps;
    private volatile long measuredAtMillis;

    public Charger(String id, WsContext ctx) {
        this.id = id;
        this.ctx = ctx;
    }

    public String id() { return id; }
    public WsContext ctx() { return ctx; }

    public Status status() { return status; }

    /** Sets a status reported live by the charger, which also confirms anything restored from disk. */
    public void setStatus(Status status) {
        this.status = status;
        this.restored = false;
    }

    /** True while the state comes from the saved file and the charger has not confirmed it yet. */
    public boolean isRestored() { return restored; }

    public ChargerSnapshot snapshot(long nowMillis) {
        return new ChargerSnapshot(status.name(), transactionId, appliedAmps, nowMillis);
    }

    /** Adopts the last known state after a restart or reconnect, until the charger reports for itself. */
    public void restoreFrom(ChargerSnapshot snapshot) {
        this.status = Status.fromName(snapshot.status());
        this.transactionId = snapshot.transactionId();
        this.appliedAmps = snapshot.appliedAmps();
        this.restored = true;
    }

    public Integer transactionId() { return transactionId; }
    public void setTransactionId(Integer transactionId) { this.transactionId = transactionId; }

    /** Last current limit we asked the charger to use, or null if none is in effect. */
    public Integer appliedAmps() { return appliedAmps; }
    public void setAppliedAmps(Integer appliedAmps) { this.appliedAmps = appliedAmps; }

    /** The charger's own reading of the current it is delivering, from MeterValues. */
    public void setMeasuredAmps(double amps) {
        this.measuredAmps = amps;
        this.measuredAtMillis = System.currentTimeMillis();
    }

    public OptionalDouble freshMeasuredAmps(long maxAgeMillis) {
        if (measuredAtMillis == 0 || System.currentTimeMillis() - measuredAtMillis > maxAgeMillis) {
            return OptionalDouble.empty();
        }
        return OptionalDouble.of(measuredAmps);
    }

    /** Set once a charger has refused or failed a request for meter values, so we stop asking. */
    public boolean meterTriggerUnsupported() { return meterTriggerUnsupported; }
    public void markMeterTriggerUnsupported() { this.meterTriggerUnsupported = true; }

    public long lastRemoteStartMillis() { return lastRemoteStartMillis; }
    public void markRemoteStart() { lastRemoteStartMillis = System.currentTimeMillis(); }

    public boolean isOpen() { return ctx.session.isOpen(); }

    public void send(String frame) { ctx.send(frame); }

    public void rememberCall(String messageId, String action) { pendingCalls.put(messageId, action); }

    public String forgetCall(String messageId) { return pendingCalls.remove(messageId); }
}
