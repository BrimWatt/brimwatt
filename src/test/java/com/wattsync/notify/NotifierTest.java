package com.wattsync.notify;

import static org.junit.jupiter.api.Assertions.*;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.Test;

class NotifierTest {

    private final List<String> sent = new ArrayList<>();
    private final AtomicLong clock = new AtomicLong(1_000_000);
    private final Notifier notifier = new Notifier((url, json) -> {
        synchronized (sent) { sent.add(url + " " + json); }
    }, clock::get);

    private int sentCount() throws InterruptedException {
        Thread.sleep(150);   // delivery happens on its own thread
        synchronized (sent) { return sent.size(); }
    }

    @Test
    void nothingIsSentUntilAnAddressIsSet() throws Exception {
        notifier.onEvent("Charging ended: 12.0 kWh");
        assertEquals(0, sentCount());
        assertEquals("No address set", notifier.sendTest());
    }

    @Test
    void onlyTheImportantEventsAreSent() throws Exception {
        notifier.setUrl("https://example.com/hook/secret");
        notifier.onEvent("Mode set to Eco");
        notifier.onEvent("Grid allowance set to 500 W");
        assertEquals(0, sentCount());
        notifier.onEvent("Charging ended: 12.0 kWh, 80% from the sun");
        notifier.onEvent("Lost contact with the solar meter: charging stopped");
        assertEquals(2, sentCount());
        String ended = sent.stream().filter(m -> m.contains("Charging ended")).findFirst().orElseThrow();   // threads may reorder
        assertTrue(ended.contains("WattSync: Charging ended"));
        assertTrue(ended.contains("\"text\"") && ended.contains("\"content\""));
    }

    @Test
    void theSameMessageIsNotRepeatedWithinTenMinutes() throws Exception {
        notifier.setUrl("https://example.com/hook");
        notifier.onEvent("Lost contact with the solar meter: charging stopped");
        notifier.onEvent("Lost contact with the solar meter: charging stopped");
        clock.addAndGet(9 * 60_000);
        notifier.onEvent("Lost contact with the solar meter: charging stopped");
        assertEquals(1, sentCount());
        clock.addAndGet(2 * 60_000);
        notifier.onEvent("Lost contact with the solar meter: charging stopped");
        assertEquals(2, sentCount());
    }

    @Test
    void addressesAreChecked() {
        assertTrue(Notifier.isValidUrl(""));
        assertTrue(Notifier.isValidUrl("https://hooks.example.com/a/b"));
        assertTrue(Notifier.isValidUrl("http://192.168.1.5:8123/api/webhook/x"));
        for (String bad : new String[] {"ftp://x.com/a", "not a url", "https://", "javascript:alert(1)", "https://x.com/" + "a".repeat(200)}) {
            assertFalse(Notifier.isValidUrl(bad), bad);
        }
        assertFalse(Notifier.isValidUrl(null));
    }

    @Test
    void onlyTheHostIsShownBack() {
        notifier.setUrl("https://hooks.example.com/services/T000/B000/SECRET");
        assertEquals("https://hooks.example.com/…", notifier.maskedHost());
        assertFalse(notifier.maskedHost().contains("SECRET"));
        notifier.setUrl("");
        assertEquals("", notifier.maskedHost());
    }
}
