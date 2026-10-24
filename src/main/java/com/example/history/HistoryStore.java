package com.example.history;

import com.example.config.AppConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/**
 * Keeps the history in files, so the graphs, the activity log and the year of history survive a restart.
 *
 * <ul>
 *   <li>The main file holds the last two days by the minute, the activity log and the daily totals. It is rewritten every
 *       half hour, and only if something changed.</li>
 *   <li>The long file (same name with {@code .long} added) holds the 5-minute and hourly records. It only ever grows at
 *       the end, so saving costs a few lines and spares the SD card; it is rewritten from memory only now and then, to
 *       drop what has aged out.</li>
 *   <li>The journal (same name with {@code .journal}) is rewritten every minute with only what is new since the last
 *       full save, a few kilobytes. It is replayed on start-up and then folded into a full save. A crash or power cut
 *       therefore loses at most a minute.</li>
 * </ul>
 * Files are written to a temporary file first, so a crash mid-write loses nothing.
 */
public class HistoryStore {

    private static final Logger logger = LoggerFactory.getLogger(HistoryStore.class);
    private static final int COMPACT_WHEN_EXTRA_LINES = 2_000;

    private final Path file;
    private final Path longFile;
    private final Path journalFile;
    private final HistoryService history;
    private int longLinesInFile;

    public HistoryStore(Path file, HistoryService history) {
        this.file = file;
        this.longFile = file.resolveSibling(file.getFileName() + ".long");
        this.journalFile = file.resolveSibling(file.getFileName() + ".journal");
        this.history = history;
    }

    public void load() {
        int lines = 0;
        boolean journalFound = false;
        for (Path path : new Path[] {file, longFile, journalFile}) {
            if (!Files.isRegularFile(path)) continue;
            if (path.equals(journalFile)) journalFound = true;
            try {
                for (String line : Files.readAllLines(path, StandardCharsets.UTF_8)) {
                    history.loadLine(line);
                    lines++;
                    if (path.equals(longFile)) longLinesInFile++;
                }
            } catch (IOException e) {
                logger.warn("Could not read history from {}: {}", path, e.getMessage());
            }
        }
        history.markLongSaved();   // what was loaded is already on disk
        history.markClean();
        if (lines == 0) logger.info("No saved history at {}; starting empty.", file);
        else logger.info("Restored {} history lines from {}.", lines, file);
        if (journalFound) save(true);   // fold the journal into the full save so the next minute's rewrite cannot lose it
    }

    public void start() {
        ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "history-store");
            thread.setDaemon(true);
            return thread;
        });
        scheduler.scheduleWithFixedDelay(() -> save(false), AppConfig.HISTORY_SAVE_INTERVAL_MINUTES,
                AppConfig.HISTORY_SAVE_INTERVAL_MINUTES, TimeUnit.MINUTES);
        scheduler.scheduleWithFixedDelay(this::saveJournal, AppConfig.HISTORY_JOURNAL_INTERVAL_MINUTES,
                AppConfig.HISTORY_JOURNAL_INTERVAL_MINUTES, TimeUnit.MINUTES);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> save(true), "history-store-shutdown"));
    }

    /** @param force save even if nothing changed */
    public synchronized void save(boolean force) {
        try {
            if (force || history.dirty()) saveMain();
            saveLong(force);
        } catch (IOException e) {
            logger.warn("Could not save history to {}: {}", file, e.getMessage());
        }
    }

    /** The cheap every-minute save: only what is new since the last full save (a few KB). */
    public synchronized void saveJournal() {
        if (!history.journalPending()) return;
        try {
            List<String> lines = new ArrayList<>();
            history.forEachJournalLine(lines::add);
            replace(journalFile, lines);
            history.markJournalSaved();
        } catch (IOException e) {
            logger.warn("Could not save the history journal to {}: {}", journalFile, e.getMessage());
        }
    }

    private void saveMain() throws IOException {
        List<String> lines = new ArrayList<>();
        history.forEachLine(lines::add);
        replace(file, lines);
        Files.deleteIfExists(journalFile);   // everything in it is now in the main file
        history.markClean();
    }

    private void saveLong(boolean force) throws IOException {
        if (longLinesInFile > history.longRecordCount() + COMPACT_WHEN_EXTRA_LINES) {
            List<String> lines = new ArrayList<>();
            history.forEachLongLine(lines::add, false);
            replace(longFile, lines);
            longLinesInFile = lines.size();
            history.markLongSaved();
            logger.info("Tidied the long-term history file ({} records).", lines.size());
            return;
        }
        if (!history.longPending() && !force) return;
        List<String> lines = new ArrayList<>();
        history.forEachLongLine(lines::add, true);
        if (lines.isEmpty()) return;
        Files.write(longFile, lines, StandardCharsets.UTF_8, StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        longLinesInFile += lines.size();
        history.markLongSaved();
    }

    /** Writes the whole file to a temporary one and swaps it in. */
    private static void replace(Path target, List<String> lines) throws IOException {
        Path temp = target.resolveSibling(target.getFileName() + ".tmp");
        Files.write(temp, lines, StandardCharsets.UTF_8);
        Files.move(temp, target, StandardCopyOption.REPLACE_EXISTING);
    }
}
