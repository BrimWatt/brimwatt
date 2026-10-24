package com.example.wallbox;

/** What we last knew about a charger, saved so it survives a restart of this app. */
public record ChargerSnapshot(String status, Integer transactionId, Integer appliedAmps, long savedAtMillis) {

    /** True if everything except the timestamp matches. */
    public boolean sameStateAs(ChargerSnapshot other) {
        return other != null
                && java.util.Objects.equals(status, other.status)
                && java.util.Objects.equals(transactionId, other.transactionId)
                && java.util.Objects.equals(appliedAmps, other.appliedAmps);
    }
}
