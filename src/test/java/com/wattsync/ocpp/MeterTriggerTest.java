package com.wattsync.ocpp;

import static org.junit.jupiter.api.Assertions.*;

import com.wattsync.wallbox.Charger;
import com.wattsync.wallbox.WallboxController;
import java.util.List;
import org.junit.jupiter.api.Test;

/** A charger that cannot report meter values on request must not be asked again every minute. */
class MeterTriggerTest {

    private final OcppRouter router = new OcppRouter(List.of());

    private Charger chargerAwaitingMeterAnswer() {
        Charger charger = new Charger("c1", null);
        charger.rememberCall("m1", WallboxController.METER_TRIGGER);
        return charger;
    }

    @Test
    void acceptedRequestKeepsAskingEnabled() {
        Charger charger = chargerAwaitingMeterAnswer();
        router.onMessage(charger, "[3,\"m1\",{\"status\":\"Accepted\"}]");
        assertFalse(charger.meterTriggerUnsupported());
    }

    @Test
    void notImplementedAnswerStopsFurtherRequests() {
        Charger charger = chargerAwaitingMeterAnswer();
        router.onMessage(charger, "[3,\"m1\",{\"status\":\"NotImplemented\"}]");
        assertTrue(charger.meterTriggerUnsupported());
    }

    @Test
    void rejectedAnswerStopsFurtherRequests() {
        Charger charger = chargerAwaitingMeterAnswer();
        router.onMessage(charger, "[3,\"m1\",{\"status\":\"Rejected\"}]");
        assertTrue(charger.meterTriggerUnsupported());
    }

    @Test
    void callErrorStopsFurtherRequests() {
        Charger charger = chargerAwaitingMeterAnswer();
        router.onMessage(charger, "[4,\"m1\",\"NotSupported\",\"\",{}]");
        assertTrue(charger.meterTriggerUnsupported());
    }

    @Test
    void otherAnswersDoNotAffectIt() {
        Charger charger = new Charger("c1", null);
        charger.rememberCall("s1", "SetChargingProfile");
        router.onMessage(charger, "[3,\"s1\",{\"status\":\"Rejected\"}]");
        assertFalse(charger.meterTriggerUnsupported());
    }
}
