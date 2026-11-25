package com.brimwatt.ocpp;

import static org.junit.jupiter.api.Assertions.*;

import com.brimwatt.wallbox.Charger;
import com.brimwatt.wallbox.ChargerSnapshot;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.List;
import org.junit.jupiter.api.Test;

/** How the app re-learns what the charger is doing after a restart. */
class RecoveryHandlersTest {

    private static final ObjectMapper MAPPER = new ObjectMapper();

    private final List<OcppHandler> handlers = CoreHandlers.all(charger -> {});

    private void receive(Charger charger, String action, String json) throws Exception {
        JsonNode payload = MAPPER.readTree(json);
        handlers.stream().filter(h -> h.action().equals(action)).findFirst().orElseThrow().handle(charger, payload);
    }

    private Charger restoredCharging() {
        Charger charger = new Charger("c1", null);
        charger.restoreFrom(new ChargerSnapshot("CHARGING", 42, 14, 0));
        return charger;
    }

    @Test
    void meterValuesRevealTransactionAndChargingWhenNothingWasKnown() throws Exception {
        Charger charger = new Charger("c1", null);
        receive(charger, "MeterValues", """
                {"connectorId":1,"transactionId":777,
                 "meterValue":[{"sampledValue":[{"measurand":"Current.Import","value":"11.8","unit":"A"}]}]}""");

        assertEquals(777, charger.transactionId());
        assertEquals(Charger.Status.CHARGING, charger.status());
        assertEquals(11.8, charger.freshMeasuredAmps(60_000).orElseThrow(), 0.001);
    }

    @Test
    void meterValuesDoNotOverwriteAKnownTransaction() throws Exception {
        Charger charger = restoredCharging();
        receive(charger, "MeterValues", "{\"connectorId\":1,\"transactionId\":999,\"meterValue\":[]}");
        assertEquals(42, charger.transactionId());
    }

    @Test
    void liveChargingStatusConfirmsTheRestoredState() throws Exception {
        Charger charger = restoredCharging();
        receive(charger, "StatusNotification", "{\"connectorId\":1,\"status\":\"Charging\",\"errorCode\":\"NoError\"}");

        assertFalse(charger.isRestored());
        assertEquals(42, charger.transactionId());
        assertEquals(14, charger.appliedAmps());
    }

    @Test
    void liveIdleStatusDiscardsAStaleRestoredTransaction() throws Exception {
        for (String status : List.of("Available", "Preparing", "Finishing")) {
            Charger charger = restoredCharging();
            receive(charger, "StatusNotification",
                    "{\"connectorId\":1,\"status\":\"" + status + "\",\"errorCode\":\"NoError\"}");

            assertNull(charger.transactionId(), status);
            assertNull(charger.appliedAmps(), status);
            assertFalse(charger.isRestored());
        }
    }

    @Test
    void connectorZeroStatusDoesNotTouchRestoredState() throws Exception {
        Charger charger = restoredCharging();
        receive(charger, "StatusNotification", "{\"connectorId\":0,\"status\":\"Available\",\"errorCode\":\"NoError\"}");

        assertTrue(charger.isRestored());
        assertEquals(Charger.Status.CHARGING, charger.status());
        assertEquals(42, charger.transactionId());
    }
}
