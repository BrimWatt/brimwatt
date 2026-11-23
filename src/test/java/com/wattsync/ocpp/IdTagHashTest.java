package com.wattsync.ocpp;

import static org.junit.jupiter.api.Assertions.*;

import com.wattsync.history.HistoryService;
import com.wattsync.wallbox.Charger;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

/** Only a short one-way fingerprint of the charger's ID tag is ever kept. The same vectors are checked in the firmware's host tests. */
class IdTagHashTest {

    private static final ObjectMapper MAPPER = new ObjectMapper();

    @Test
    void theFingerprintMatchesTheSharedVectors() {
        assertEquals("35cca576", IdTagHash.of("tag-1"));
        assertEquals("634a1613", IdTagHash.of("abc"));
    }

    @Test
    void noTagMeansNoFingerprintAndTheSameTagAlwaysGivesTheSame() {
        assertEquals("", IdTagHash.of(""));
        assertEquals("", IdTagHash.of("   "));
        assertEquals("", IdTagHash.of(null));
        assertEquals(IdTagHash.of("tag-1"), IdTagHash.of("tag-1"));
        assertNotEquals(IdTagHash.of("tag-1"), IdTagHash.of("tag-2"));
    }

    @Test
    void aStartedSessionKeepsTheFingerprintAndTheRawTagNeverReachesTheRecords() throws Exception {
        long[] now = {1_760_000_000_000L};
        HistoryService history = new HistoryService(ZoneId.of("America/Los_Angeles"), () -> now[0]);
        var handlers = CoreHandlers.all(c -> {}, history);
        Charger charger = new Charger("c1", null);
        String raw = "secret-card-123";
        handlers.stream().filter(h -> h.action().equals("StartTransaction")).findFirst().orElseThrow()
                .handle(charger, MAPPER.readTree("{\"idTag\":\"" + raw + "\",\"meterStart\":100}"));
        assertEquals(IdTagHash.of(raw), history.openSession().tag);
        assertNotEquals("", history.openSession().tag);

        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        history.forEachJournalLine(lines::add);
        assertTrue(lines.stream().noneMatch(l -> l.contains(raw)), "the raw tag must not be saved");
        assertTrue(history.recentEvents(50).stream().noneMatch(e -> e.text().contains(raw)));
        assertTrue(lines.stream().anyMatch(l -> l.startsWith("O\t") && l.endsWith("\t" + IdTagHash.of(raw))));

        HistoryService fresh = new HistoryService(ZoneId.of("America/Los_Angeles"), () -> now[0]);
        lines.forEach(fresh::loadLine);
        assertEquals(IdTagHash.of(raw), fresh.openSession().tag);
    }

    @Test
    void aDamagedOrOldTagColumnIsIgnored() {
        long[] now = {1_760_000_000_000L};
        HistoryService h = new HistoryService(ZoneId.of("America/Los_Angeles"), () -> now[0]);
        h.loadLine("T\t1759999000\t1759999100\t-1.0\t-1.0\t300.0\t250.0\tRemote\t0.00\t0\t0\t0\tNOT-HEX-AT-ALL!");
        assertEquals("", h.recentSessions(1).get(0).tag);
        h.loadLine("T\t1759999200\t1759999300\t-1.0\t-1.0\t300.0\t250.0\tRemote\t0.00\t0\t0\t0");            // a file from before the column existed
        assertEquals("", h.recentSessions(2).get(0).tag);
    }
}
