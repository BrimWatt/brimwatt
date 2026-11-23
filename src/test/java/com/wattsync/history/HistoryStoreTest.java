package com.wattsync.history;

import static org.junit.jupiter.api.Assertions.*;

import com.wattsync.model.ChargingMode;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.ZoneId;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class HistoryStoreTest {

    private static final ZoneId LA = ZoneId.of("America/Los_Angeles");

    @TempDir Path dir;
    private final AtomicLong clock = new AtomicLong(1_760_000_000_000L);
    private Path file;
    private HistoryService history;
    private HistoryStore store;

    @BeforeEach
    void setUp() {
        file = dir.resolve("history.tsv");
        history = new HistoryService(LA, clock::get);
        store = new HistoryStore(file, history);
    }

    private void minutes(int n) {
        for (int i = 0; i < n; i++) {
            clock.addAndGet(60_000);
            history.recordSample(2000, 0, 1000, ChargingMode.ECO, false);
        }
    }

    private Path longFile() { return dir.resolve("history.tsv.long"); }

    private Path journalFile() { return dir.resolve("history.tsv.journal"); }

    @Test
    void aCrashAfterTheFullSaveLosesAtMostTheLastMinute() {
        minutes(30);
        store.save(true);                         // the full save
        assertFalse(Files.exists(journalFile()));

        minutes(5);
        history.sessionStarted(100);
        history.addEvent("Started charging");
        store.saveJournal();                      // written a minute at a time in between
        assertTrue(Files.exists(journalFile()));
        minutes(1);                               // this last minute never reaches the disk

        HistoryService fresh = new HistoryService(LA, clock::get);
        new HistoryStore(file, fresh).load();      // what the app finds after the crash
        assertEquals(history.minuteSampleCount() - 1, fresh.minuteSampleCount());
        assertEquals(1, fresh.recentEvents(5).size());
        assertNotNull(fresh.openSession());
        assertFalse(Files.exists(journalFile()), "load folds the journal into a full save");

        HistoryService again = new HistoryService(LA, clock::get);
        new HistoryStore(file, again).load();      // and a second restart still has it all
        assertEquals(fresh.minuteSampleCount(), again.minuteSampleCount());
        assertEquals(1, again.recentEvents(5).size());
    }

    @Test
    void aSessionThatEndedAfterTheFullSaveIsNotLeftOpen() {
        history.sessionStarted(100);
        minutes(3);
        store.save(true);
        minutes(2);
        history.sessionStopped(900, "Remote");
        store.saveJournal();

        HistoryService fresh = new HistoryService(LA, clock::get);
        new HistoryStore(file, fresh).load();
        assertNull(fresh.openSession());
        assertEquals(1, fresh.recentSessions(5).size());
    }

    @Test
    void replayingTheJournalTwiceDoesNotDuplicateAnything() {
        minutes(10);
        store.save(true);
        minutes(3);
        history.addEvent("Car plugged in");
        store.saveJournal();
        List<String> lines = new java.util.ArrayList<>();
        history.forEachJournalLine(lines::add);

        HistoryService fresh = new HistoryService(LA, clock::get);
        new HistoryStore(file, fresh).load();
        lines.forEach(fresh::loadLine);
        assertEquals(history.minuteSampleCount(), fresh.minuteSampleCount());
        assertEquals(1, fresh.recentEvents(5).size());
    }

    @Test
    void everythingComesBackAfterARestart() {
        minutes(400);
        history.addEvent("Car plugged in");
        store.save(true);

        HistoryService fresh = new HistoryService(LA, clock::get);
        new HistoryStore(file, fresh).load();
        assertEquals(history.minuteSampleCount(), fresh.minuteSampleCount());
        assertEquals(history.longRecordCount(), fresh.longRecordCount());
        assertEquals(1, fresh.recentEvents(5).size());
        assertEquals(history.totalsForDay(history.localDay(clock.get() / 1000)).solarWh,
                fresh.totalsForDay(fresh.localDay(clock.get() / 1000)).solarWh, 0.1);
        assertFalse(fresh.dirty());
        assertFalse(fresh.longPending());
    }

    @Test
    void theLongFileOnlyGrowsAtTheEnd() throws IOException {
        minutes(120);
        store.save(false);
        long sizeAfterFirst = Files.size(longFile());
        String firstBytes = Files.readString(longFile());

        minutes(60);
        store.save(false);
        assertTrue(Files.size(longFile()) > sizeAfterFirst);
        assertTrue(Files.readString(longFile()).startsWith(firstBytes), "earlier lines are never rewritten");
    }

    @Test
    void nothingIsWrittenWhenNothingChanged() throws IOException {
        minutes(120);
        store.save(false);
        long main = Files.getLastModifiedTime(file).toMillis();
        long size = Files.size(longFile());
        store.save(false);
        assertEquals(size, Files.size(longFile()));
        assertEquals(main, Files.getLastModifiedTime(file).toMillis());
    }

    @Test
    void aMissingOrDamagedFileJustMeansStartingEmpty() throws IOException {
        store.load();
        assertEquals(0, history.minuteSampleCount());
        Files.writeString(file, "garbage\nS\tx\n");
        new HistoryStore(file, history).load();
        assertEquals(0, history.minuteSampleCount());
    }
}
