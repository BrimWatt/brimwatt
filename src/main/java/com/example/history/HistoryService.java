package com.example.history;

import com.example.model.ChargingMode;

import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneId;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.TreeMap;
import java.util.function.Consumer;
import java.util.function.LongSupplier;

/**
 * What happened and how much power flowed, kept for the dashboard's graphs at lower detail the older it gets: a sample
 * every minute for 2 days, every 5 minutes for 30 days and every hour for a year, plus daily energy totals for a year
 * (from which the savings are worked out) and a short activity log.
 *
 * <p>Energy is integrated from the once-a-minute readings, so it is an estimate, typically within a few percent. The
 * part of the car's energy that came from the sun is whatever solar was left after the house, up to what the car
 * drew. Same behaviour as the ESP32 firmware's HistoryService.
 */
public class HistoryService {

    public static final int MAX_SAMPLES = 2_880;      // 48 hours at one a minute
    public static final int MAX_FIVE_MINUTE = 8_640;  // 30 days at one every 5 minutes
    public static final int MAX_HOURLY = 8_760;       // a year at one an hour
    public static final int MAX_EVENTS = 50;
    public static final int MAX_DAYS = 400;
    public static final int MAX_SESSIONS = 400;
    /** Old lines are dropped by age as well as by count: activity keeps one day, finished sessions keep 30 days. */
    public static final long EVENT_MAX_AGE_SECONDS = 24 * 3600L;
    /** Limit changes less than an hour apart, with nothing else logged between them, become one line. */
    private static final long LIMIT_MERGE_SECONDS = 3600;
    private static final java.util.regex.Pattern LIMIT_LINE = java.util.regex.Pattern.compile(
        "^(Changed limit|Car: changed limit)(?: to (\\d{1,5})| (\\d{1,5}) times, now (\\d{1,5})) A(?:: (.*))?$", java.util.regex.Pattern.DOTALL);
    public static final long SESSION_MAX_AGE_SECONDS = 30 * 24 * 3600L;
    public static final int FLAG_CHARGING = 1;
    public static final int FLAG_PAUSED = 2;

    private static final long MAX_GAP_SECONDS = 300;      // longer gaps (no readings) are not counted as energy
    private static final long EVENT_REPEAT_SECONDS = 120;

    /** One minute of power, in watts. {@code grid} is positive importing, negative exporting. */
    public record Sample(long t, int solar, int home, int grid, int car, int mode, int flags) {}

    public record Event(long t, String text) {}

    /**
     * One charging session, from the wallbox's StartTransaction to its StopTransaction. The energy is the wallbox's own
     * meter difference when it reported both readings, otherwise our estimate; the part from the sun is always our
     * estimate (the solar left after the house, up to what the car drew).
     */
    public static class Session {
        public long start;
        public long end;                  // 0 while the session is still running
        public double meterStartWh = -1;  // the wallbox's meter at the start, or -1 if it did not say
        public double meterWh = -1;       // the wallbox's measured energy for the session, or -1 if unknown
        public double estimatedWh;        // our integration of what the car drew
        public double sunWh;              // the part of that which the sun covered
        public String reason = "";        // why it ended, as the wallbox reported it
        public double savedCents;         // what the sun's share was worth at the prices of the hours it was charged
        public int mode = -1;             // mode code (0 Eco, 1 Max solar, 2 Custom, 3 Cheapest hours) at the first reading of the session, -1 if unknown
        public boolean mixed;             // the mode changed during the session
        public long pluggedAt;            // when the car was plugged in before this session started, 0 if we did not see it
        public String tag = "";           // a short one-way fingerprint of the ID tag the charger sent (never the tag itself), "" if none

        public double energyWh() { return meterWh >= 0 ? meterWh : estimatedWh; }

        Session copy() {
            Session c = new Session();
            c.start = start; c.end = end; c.meterStartWh = meterStartWh; c.meterWh = meterWh;
            c.estimatedWh = estimatedWh; c.sunWh = sunWh; c.reason = reason; c.savedCents = savedCents;
            c.mode = mode; c.mixed = mixed; c.pluggedAt = pluggedAt; c.tag = tag;
            return c;
        }
    }

    /** Whether a departure goal was reached by its deadline, for the local day the deadline fell on. */
    public static class GoalResult {
        public boolean met;
        public double targetWh, doneWh;
    }

    /** Energy for one local day, in watt-hours. */
    public static class DayTotals {
        public double solarWh, carWh, carSolarWh, importWh, exportWh;
        public double savedCents;   // the sun's share of the car's energy, valued at the price of each hour (0 if not recorded)
    }

    private final ZoneId zone;
    private java.util.function.LongToDoubleFunction priceAt = t -> 30.0;   // cents per kWh at an epoch second
    private final LongSupplier clockMillis;
    private final Deque<Sample> samples = new ArrayDeque<>();
    private final Deque<Sample> fiveMin = new ArrayDeque<>();
    private final Deque<Sample> hourly = new ArrayDeque<>();
    private final Accum acc5 = new Accum(), acc60 = new Accum();
    private int pendingFiveMin, pendingHourly;
    private final Deque<Event> events = new ArrayDeque<>();
    private final Deque<Session> sessions = new ArrayDeque<>();
    private Session open;
    private final TreeMap<LocalDate, DayTotals> days = new TreeMap<>();
    private final TreeMap<LocalDate, GoalResult> goals = new TreeMap<>();   // one per day a goal deadline was judged
    private long pluggedAt;   // when the car was last seen being plugged in (0 = not plugged, or not seen)
    private long lastSampleAt;
    private boolean dirty;
    private boolean journalDirty;
    private long mainSavedAt;   // epoch seconds of the last full save; the journal holds what happened after it

    public HistoryService(ZoneId zone) {
        this(zone, System::currentTimeMillis);
    }

    public HistoryService(ZoneId zone, LongSupplier clockMillis) {
        this.zone = zone;
        this.clockMillis = clockMillis;
    }

    /** A running average for the bucket being filled. */
    private static final class Accum {
        double solar, home, grid, car;
        int n, flags, mode;
        long bucket = -1;

        void add(Sample s) {
            solar += s.solar(); home += s.home(); grid += s.grid(); car += s.car();
            flags |= s.flags();
            mode = s.mode();
            n++;
        }

        Sample average(long middle) {
            return new Sample(middle, clamp(solar / n), clamp(home / n), clamp(grid / n), clamp(car / n), mode, flags);
        }

        void reset() { solar = home = grid = car = 0; n = flags = mode = 0; }
    }

    private long nowSeconds() { return clockMillis.getAsLong() / 1000; }

    public ZoneId zone() { return zone; }

    /** Where the price of electricity at a moment comes from (it can change with the hour), for the money saved. */
    public synchronized void setPriceProvider(java.util.function.LongToDoubleFunction priceAt) { this.priceAt = priceAt; }

    public LocalDate localDay(long epochSeconds) {
        return Instant.ofEpochSecond(epochSeconds).atZone(zone).toLocalDate();
    }

    /** Midnight (epoch seconds) at the start of the local day containing the given time. */
    public long startOfLocalDay(long epochSeconds) {
        return localDay(epochSeconds).atStartOfDay(zone).toEpochSecond();
    }

    /** Records the latest readings; {@code carWatts} is what the charger draws (measured, or assumed from its limit). */
    /** Drops events older than a day and sessions older than 30 days (nothing while the clock is not set). */
    private void pruneOld(long now) {
        if (now <= 0) return;
        boolean changed = false;
        while (!events.isEmpty() && events.peekFirst().t() < now - EVENT_MAX_AGE_SECONDS) { events.pollFirst(); changed = true; }
        while (!sessions.isEmpty() && sessions.peekFirst().end < now - SESSION_MAX_AGE_SECONDS) { sessions.pollFirst(); changed = true; }
        if (changed) dirty = true;
    }

    /** The oldest time still shown, or 0 while the clock is not set. */
    private long eventCutoff() { long now = nowSeconds(); return now <= 0 ? 0 : now - EVENT_MAX_AGE_SECONDS; }
    private long sessionCutoff() { long now = nowSeconds(); return now <= 0 ? 0 : now - SESSION_MAX_AGE_SECONDS; }

    public synchronized void recordSample(double solarWatts, double gridWatts, double carWatts, ChargingMode mode,
                                          boolean paused) {
        long now = nowSeconds();
        if (now <= 0) return;
        pruneOld(now);   // once a minute is plenty: old lines also leave while nothing new happens
        solarWatts = Math.max(0, solarWatts);
        carWatts = Math.max(0, carWatts);
        double homeWatts = Math.max(0, solarWatts + gridWatts - carWatts);

        int modeCode = mode == ChargingMode.ECO ? 0 : mode == ChargingMode.MAX_SOLAR ? 1 : mode == ChargingMode.CHEAPEST ? 3 : 2;
        int flags = (carWatts >= 100 ? FLAG_CHARGING : 0) | (paused ? FLAG_PAUSED : 0);

        // Energy since the previous reading, counted into the local day it ended in.
        if (lastSampleAt > 0 && now > lastSampleAt && now - lastSampleAt <= MAX_GAP_SECONDS) {
            double hours = (now - lastSampleAt) / 3600.0;
            double carFromSun = Math.min(carWatts, Math.max(0, solarWatts - homeWatts));
            DayTotals day = days.computeIfAbsent(localDay(now), d -> new DayTotals());
            day.solarWh += solarWatts * hours;
            day.carWh += carWatts * hours;
            day.carSolarWh += carFromSun * hours;
            double savedNow = carFromSun * hours / 1000.0 * priceAt.applyAsDouble(now);
            day.savedCents += savedNow;
            day.importWh += Math.max(0, gridWatts) * hours;
            day.exportWh += Math.max(0, -gridWatts) * hours;
            if (open != null) {
                open.estimatedWh += carWatts * hours;
                open.sunWh += carFromSun * hours;
                open.savedCents += savedNow;
                if (open.mode < 0) open.mode = modeCode;
                else if (open.mode != modeCode) open.mixed = true;
                dirty = journalDirty = true;
            }
            while (days.size() > MAX_DAYS) days.pollFirstEntry();
        }
        lastSampleAt = now;

        Sample sample = new Sample(now, clamp(solarWatts), clamp(homeWatts), clamp(gridWatts), clamp(carWatts),
                modeCode, flags);
        samples.addLast(sample);
        while (samples.size() > MAX_SAMPLES) samples.pollFirst();
        feedFiveMinute(sample);
        dirty = journalDirty = true;
    }

    private void feedFiveMinute(Sample s) {
        long bucket = s.t() / 300;
        if (acc5.n > 0 && acc5.bucket != bucket) flushFiveMinute();
        acc5.bucket = bucket;
        acc5.add(s);
    }

    private void flushFiveMinute() {
        if (acc5.n == 0) return;
        Sample record = acc5.average(acc5.bucket * 300 + 150);
        acc5.reset();
        fiveMin.addLast(record);
        while (fiveMin.size() > MAX_FIVE_MINUTE) fiveMin.pollFirst();
        pendingFiveMin = Math.min(pendingFiveMin + 1, MAX_FIVE_MINUTE);
        long bucket = record.t() / 3600;
        if (acc60.n > 0 && acc60.bucket != bucket) flushHourly();
        acc60.bucket = bucket;
        acc60.add(record);
    }

    private void flushHourly() {
        if (acc60.n == 0) return;
        hourly.addLast(acc60.average(acc60.bucket * 3600 + 1800));
        while (hourly.size() > MAX_HOURLY) hourly.pollFirst();
        pendingHourly = Math.min(pendingHourly + 1, MAX_HOURLY);
        acc60.reset();
    }

    /** The wallbox started a session; {@code meterStartWh} is its meter reading, or -1 if it gave none. */
    public void sessionStarted(double meterStartWh) { sessionStarted(meterStartWh, ""); }

    /** As above; {@code tagHash} is the fingerprint from {@link com.example.ocpp.IdTagHash}, never the raw ID tag. */
    public synchronized void sessionStarted(double meterStartWh, String tagHash) {
        long now = nowSeconds();
        if (now <= 0) return;
        if (open != null) {   // we never saw the previous one end: close it where it stands
            open.end = now;
            open.reason = "unknown";
            sessions.addLast(open);
            while (sessions.size() > MAX_SESSIONS) sessions.pollFirst();
        }
        open = new Session();
        open.start = now;
        open.tag = tagHash == null ? "" : tagHash;
        open.pluggedAt = pluggedAt > 0 && pluggedAt <= now && now - pluggedAt < 3 * 86400 ? pluggedAt : 0;
        open.meterStartWh = meterStartWh;
        dirty = journalDirty = true;
    }

    /** The car was just plugged in (the charger went from available to preparing). */
    public synchronized void notePluggedIn() { pluggedAt = nowSeconds(); }

    /** The car was unplugged. */
    public synchronized void notePluggedOut() { pluggedAt = 0; }

    /** Records whether the departure goal was reached at the deadline that has just passed. */
    public synchronized void recordGoalResult(long deadlineEpochSeconds, boolean met, double targetWh, double doneWh) {
        GoalResult r = new GoalResult();
        r.met = met; r.targetWh = targetWh; r.doneWh = doneWh;
        goals.put(localDay(deadlineEpochSeconds), r);
        while (goals.size() > MAX_DAYS) goals.pollFirstEntry();
        dirty = journalDirty = true;
        addEvent(String.format(java.util.Locale.ROOT, met ? "Departure goal met: %.1f of %.1f kWh" : "Departure goal missed: %.1f of %.1f kWh",
                doneWh / 1000, targetWh / 1000));
    }

    /** 0 if no goal was judged that day, 1 met, 2 missed. */
    public synchronized int goalOutcome(LocalDate day) {
        GoalResult r = goals.get(day);
        return r == null ? 0 : r.met ? 1 : 2;
    }

    /** The wallbox ended the session. Ignored if we never saw it start (the app was not running). */
    public synchronized void sessionStopped(double meterStopWh, String reason) {
        long now = nowSeconds();
        if (open == null || now <= 0) return;
        Session done = open;
        open = null;
        done.end = now;
        if (done.meterStartWh >= 0 && meterStopWh >= done.meterStartWh) done.meterWh = meterStopWh - done.meterStartWh;
        done.reason = reason == null ? "" : reason.replaceAll("[\\t\\r\\n]", " ");
        sessions.addLast(done);
        while (sessions.size() > MAX_SESSIONS) sessions.pollFirst();
        pruneOld(now);
        dirty = journalDirty = true;

        double kwh = done.energyWh() / 1000;
        if (done.estimatedWh > 1) {
            long pct = Math.round(Math.min(1.0, done.sunWh / done.estimatedWh) * 100);
            addEvent(String.format(java.util.Locale.ROOT, "Charging ended: %.1f kWh, %d%% from the sun", kwh, pct));
        } else {
            addEvent(String.format(java.util.Locale.ROOT, "Charging ended: %.1f kWh", kwh));
        }
    }

    /** The session running now, or null. */
    public synchronized Session openSession() { return open == null ? null : open.copy(); }

    /** Finished sessions, newest first. */
    public synchronized List<Session> recentSessions(int limit) { return recentSessions(limit, 0); }

    /** One page of finished sessions, newest first, after skipping {@code offset}. */
    public synchronized List<Session> recentSessions(int limit, int offset) {
        List<Session> out = new ArrayList<>();
        var it = sessions.descendingIterator();
        int skipped = 0;
        long cutoff = sessionCutoff();
        while (it.hasNext() && out.size() < limit) {
            Session x = it.next();
            if (x.end < cutoff) continue;   // older than 30 days: gone from the list even before it is purged
            if (skipped++ < offset) continue;
            out.add(x.copy());
        }
        return out;
    }

    public synchronized int sessionCount() {
        long cutoff = sessionCutoff();
        int n = 0;
        for (Session x : sessions) if (x.end >= cutoff) n++;
        return n;
    }

    /** Adds a line to the activity log. The same text within two minutes is ignored. */
    private volatile java.util.function.Consumer<String> eventListener;

    /** Told about every new activity line (not the repeats that are dropped); must not block. */
    public void setEventListener(java.util.function.Consumer<String> listener) { this.eventListener = listener; }

    public synchronized void addEvent(String text) {
        long now = nowSeconds();
        if (now <= 0) return;
        // Only an identical line right after the previous one is dropped (a flapping status). The same line after a
        // different one is real, such as switching Eco, Custom, Eco: dropping it would leave the log contradicting the state.
        pruneOld(now);
        Event last = events.peekLast();
        if (last != null && now - last.t() < EVENT_REPEAT_SECONDS && last.text().equals(text)) return;
        // A run of limit changes (the planner moves the current a few times an hour) is one line that counts them and shows the latest.
        if (last != null && now - last.t() < LIMIT_MERGE_SECONDS) {
            var previous = LIMIT_LINE.matcher(last.text());
            var next = LIMIT_LINE.matcher(text);
            if (previous.matches() && next.matches() && previous.group(1).equals(next.group(1))) {
                int count = previous.group(3) != null ? Integer.parseInt(previous.group(3)) : 1;
                int amps = Integer.parseInt(next.group(2) != null ? next.group(2) : next.group(4));
                String reason = next.group(5);
                String merged = next.group(1) + " " + (count + 1) + " times, now " + amps + " A" + (reason == null || reason.isEmpty() ? "" : ": " + reason);
                events.pollLast();
                events.addLast(new Event(now, merged));
                java.util.function.Consumer<String> listener = eventListener;
                if (listener != null) listener.accept(text);   // the notifier still sees every real change
                dirty = journalDirty = true;
                return;
            }
        }
        events.addLast(new Event(now, text));
        java.util.function.Consumer<String> listener = eventListener;
        if (listener != null) listener.accept(text);
        while (events.size() > MAX_EVENTS) events.pollFirst();
        dirty = journalDirty = true;
    }

    /** Samples from {@code from} on, averaged into buckets of {@code stepSeconds} (60 or less returns them as they are). */
    public synchronized List<Sample> series(long from, int stepSeconds) {
        return seriesRange(from, Long.MAX_VALUE, stepSeconds);
    }

    /**
     * Samples between {@code from} and {@code to}, taken from the finest data that still exists for each moment
     * (minutes, then 5 minutes, then hours), averaged into buckets of {@code stepSeconds}.
     */
    public synchronized List<Sample> seriesRange(long from, long to, int stepSeconds) {
        long oldestMinute = samples.isEmpty() ? Long.MAX_VALUE : samples.peekFirst().t();
        long oldestFive = fiveMin.isEmpty() ? Long.MAX_VALUE : fiveMin.peekFirst().t();
        List<Sample> raw = new ArrayList<>();
        // A coarser record is used only if it lies wholly before the finer data (it covers 150 s / 1800 s either side of t).
        for (Sample r : hourly) {
            if (r.t() >= from && r.t() <= to && r.t() + 1800 <= oldestFive - 150 && r.t() + 1800 <= oldestMinute) raw.add(r);
        }
        for (Sample r : fiveMin) {
            if (r.t() >= from && r.t() <= to && r.t() + 150 <= oldestMinute) raw.add(r);
        }
        for (Sample s : samples) if (s.t() >= from && s.t() <= to) raw.add(s);
        if (stepSeconds <= 60) return raw;

        List<Sample> out = new ArrayList<>();
        Accum sum = new Accum();
        for (Sample s : raw) {
            long bucket = (s.t() - from) / stepSeconds;
            if (sum.n > 0 && bucket != sum.bucket) {
                out.add(sum.average(from + sum.bucket * stepSeconds + stepSeconds / 2));
                sum.reset();
            }
            sum.bucket = bucket;
            sum.add(s);
        }
        if (sum.n > 0) out.add(sum.average(from + sum.bucket * stepSeconds + stepSeconds / 2));
        return out;
    }

    /** How many one-minute samples are held (the finest tier). */
    public synchronized int minuteSampleCount() { return samples.size(); }

    /** Energy that went into the car since an epoch second, from the minute-by-minute samples (kept for 48 hours). */
    public synchronized double carEnergyWhSince(long epochSeconds) {
        double wh = 0;
        for (Sample s : samples) if (s.t() >= epochSeconds) wh += s.car() / 60.0;
        return wh;
    }

    /** Every day with data, oldest first. */
    public synchronized java.util.Map<LocalDate, DayTotals> allDays() {
        java.util.Map<LocalDate, DayTotals> copy = new java.util.LinkedHashMap<>();
        for (var e : days.entrySet()) copy.put(e.getKey(), totalsForDay(e.getKey()));
        return copy;
    }

    public synchronized DayTotals totalsForDay(LocalDate day) {
        DayTotals d = days.get(day);
        if (d == null) return new DayTotals();
        DayTotals copy = new DayTotals();
        copy.solarWh = d.solarWh; copy.carWh = d.carWh; copy.carSolarWh = d.carSolarWh;
        copy.importWh = d.importWh; copy.exportWh = d.exportWh; copy.savedCents = d.savedCents;
        return copy;
    }

    /** Newest first. */
    public synchronized List<Event> recentEvents(int limit) { return recentEvents(limit, 0); }

    /** One page of events, newest first, after skipping {@code offset}. */
    public synchronized List<Event> recentEvents(int limit, int offset) {
        List<Event> out = new ArrayList<>();
        var it = events.descendingIterator();
        int skipped = 0;
        long cutoff = eventCutoff();
        while (it.hasNext() && out.size() < limit) {
            Event e = it.next();
            if (e.t() < cutoff) continue;   // older than a day: gone from the list even before it is purged
            if (skipped++ < offset) continue;
            out.add(e);
        }
        return out;
    }

    public synchronized int eventCount() {
        long cutoff = eventCutoff();
        int n = 0;
        for (Event e : events) if (e.t() >= cutoff) n++;
        return n;
    }

    // ---------------------------------------------------------------- saving

    public synchronized void forEachLine(Consumer<String> write) {
        for (var d : days.entrySet()) {
            DayTotals t = d.getValue();
            write.accept(String.format(java.util.Locale.ROOT, "D\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f",
                    d.getKey().toEpochDay(), t.solarWh, t.carWh, t.carSolarWh, t.importWh, t.exportWh, t.savedCents));
        }
        for (var g : goals.entrySet()) write.accept(goalLine(g.getKey(), g.getValue()));
        for (Event e : events) write.accept("E\t" + e.t() + "\t" + e.text());
        for (Session x : sessions) write.accept(sessionLine('T', x));
        if (open != null) write.accept(sessionLine('O', open));
        for (Sample s : samples) {
            write.accept("S\t" + s.t() + "\t" + s.solar() + "\t" + s.home() + "\t" + s.grid() + "\t" + s.car() + "\t"
                    + s.mode() + "\t" + s.flags());
        }
    }

    /**
     * The 5-minute and hourly records, kept in a file of their own that only ever grows at the end (so saving them is
     * cheap). {@code onlyNew} writes just the records added since the last {@link #markLongSaved()}.
     */
    public synchronized void forEachLongLine(Consumer<String> write, boolean onlyNew) {
        int skip5 = onlyNew && pendingFiveMin < fiveMin.size() ? fiveMin.size() - pendingFiveMin : 0;
        int skipH = onlyNew && pendingHourly < hourly.size() ? hourly.size() - pendingHourly : 0;
        int i = 0;
        for (Sample s : fiveMin) if (i++ >= skip5) write.accept(longLine('M', s));
        i = 0;
        for (Sample s : hourly) if (i++ >= skipH) write.accept(longLine('H', s));
    }

    private static String longLine(char tag, Sample s) {
        return tag + "\t" + s.t() + "\t" + s.solar() + "\t" + s.home() + "\t" + s.grid() + "\t" + s.car() + "\t"
                + s.mode() + "\t" + s.flags();
    }

    public synchronized boolean longPending() { return pendingFiveMin > 0 || pendingHourly > 0; }

    public synchronized void markLongSaved() { pendingFiveMin = pendingHourly = 0; }

    /** How many records a full rewrite of the long file would hold. */
    public synchronized int longRecordCount() { return fiveMin.size() + hourly.size(); }

    private static String sessionLine(char tag, Session x) {
        return String.format(java.util.Locale.ROOT, "%c\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%s\t%.2f\t%d\t%d\t%d\t%s", tag, x.start, x.end,
                x.meterStartWh, x.meterWh, x.estimatedWh, x.sunWh, x.reason, x.savedCents, x.mode, x.mixed ? 1 : 0, x.pluggedAt, x.tag);
    }

    /** A goal result is its own line type, so an older version reading the file just skips it. */
    private static String goalLine(LocalDate day, GoalResult r) {
        return String.format(java.util.Locale.ROOT, "G\t%d\t%d\t%.0f\t%.0f", day.toEpochDay(), r.met ? 1 : 2, r.targetWh, r.doneWh);
    }

    /** Reads back one line written by {@link #forEachLine}. Unreadable lines are ignored. */
    public synchronized void loadLine(String line) {
        try {
            String[] p = line.split("\t", -1);
            if (p.length == 0 || p[0].length() != 1) return;
            switch (p[0].charAt(0)) {
                case 'S' -> {
                    if (p.length != 8) return;
                    long t = Long.parseLong(p[1]);
                    if (t <= 0 || t <= lastSampleAt) return;   // already loaded (a journal overlaps the full save)
                    samples.addLast(new Sample(t, Integer.parseInt(p[2]), Integer.parseInt(p[3]), Integer.parseInt(p[4]),
                            Integer.parseInt(p[5]), Integer.parseInt(p[6]), Integer.parseInt(p[7])));
                    while (samples.size() > MAX_SAMPLES) samples.pollFirst();
                    lastSampleAt = Math.max(lastSampleAt, t);
                }
                case 'M', 'H' -> {
                    if (p.length != 8) return;
                    long t = Long.parseLong(p[1]);
                    if (t <= 0) return;
                    Sample r = new Sample(t, Integer.parseInt(p[2]), Integer.parseInt(p[3]), Integer.parseInt(p[4]),
                            Integer.parseInt(p[5]), Integer.parseInt(p[6]), Integer.parseInt(p[7]));
                    if (p[0].charAt(0) == 'M') {
                        fiveMin.addLast(r);
                        while (fiveMin.size() > MAX_FIVE_MINUTE) fiveMin.pollFirst();
                    } else {
                        hourly.addLast(r);
                        while (hourly.size() > MAX_HOURLY) hourly.pollFirst();
                    }
                }
                case 'T', 'O' -> {
                    if (p.length < 7) return;
                    Session x = new Session();
                    x.start = Long.parseLong(p[1]);
                    x.end = Long.parseLong(p[2]);
                    x.meterStartWh = Double.parseDouble(p[3]);
                    x.meterWh = Double.parseDouble(p[4]);
                    x.estimatedWh = Double.parseDouble(p[5]);
                    x.sunWh = Double.parseDouble(p[6]);
                    x.reason = p.length > 7 ? p[7] : "";
                    x.savedCents = p.length > 8 ? Double.parseDouble(p[8]) : 0;   // older files have no money column
                    if (p.length > 10) { x.mode = Integer.parseInt(p[9]); x.mixed = "1".equals(p[10]); }   // older files have no mode
                    if (p.length > 11) x.pluggedAt = Long.parseLong(p[11]);   // older files do not know when the car was plugged in
                    if (p.length > 12 && p[12].matches("[0-9a-f]{0,8}")) x.tag = p[12];   // older files have no tag fingerprint; anything else is ignored
                    if (x.start <= 0) return;
                    if (p[0].charAt(0) == 'T') {
                        for (Session known : sessions) if (known.start == x.start) return;
                        if (open != null && open.start == x.start) open = null;
                        sessions.addLast(x);
                        while (sessions.size() > MAX_SESSIONS) sessions.pollFirst();
                    } else {
                        open = x;
                    }
                }
                case 'E' -> {
                    if (p.length < 3) return;
                    long at = Long.parseLong(p[1]);
                    for (Event known : events) if (known.t() == at && known.text().equals(p[2])) return;
                    events.addLast(new Event(at, p[2]));
                    while (events.size() > MAX_EVENTS) events.pollFirst();
                }
                case 'G' -> {
                    if (p.length != 5) return;
                    GoalResult r = new GoalResult();
                    r.met = "1".equals(p[2]);
                    r.targetWh = Double.parseDouble(p[3]); r.doneWh = Double.parseDouble(p[4]);
                    goals.put(LocalDate.ofEpochDay(Long.parseLong(p[1])), r);
                }
                case 'D' -> {
                    if (p.length != 7 && p.length != 8) return;
                    DayTotals d = new DayTotals();
                    d.solarWh = Double.parseDouble(p[2]); d.carWh = Double.parseDouble(p[3]);
                    d.carSolarWh = Double.parseDouble(p[4]); d.importWh = Double.parseDouble(p[5]);
                    d.exportWh = Double.parseDouble(p[6]);
                    d.savedCents = p.length == 8 ? Double.parseDouble(p[7]) : 0;   // older files have no money column
                    days.put(LocalDate.ofEpochDay(Long.parseLong(p[1])), d);
                }
                default -> { }
            }
        } catch (RuntimeException ignored) {
            // a damaged line is skipped
        }
    }

    public synchronized boolean dirty() { return dirty; }

    public synchronized void markClean() {
        dirty = false;
        journalDirty = false;
        mainSavedAt = Math.max(nowSeconds(), lastSampleAt);
    }

    /**
     * A small file written every minute between the full saves: the samples, events, sessions and today's totals added
     * since the last full save. After a crash or power cut it is replayed on top of the full save ({@link #loadLine}
     * ignores what is already there), so at most a minute is lost. A full save makes it obsolete.
     */
    public synchronized void forEachJournalLine(Consumer<String> write) {
        long from = mainSavedAt - 60;
        LocalDate firstDay = localDay(mainSavedAt);
        for (var d : days.entrySet()) {
            if (d.getKey().isBefore(firstDay)) continue;
            DayTotals t = d.getValue();
            write.accept(String.format(java.util.Locale.ROOT, "D\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f",
                    d.getKey().toEpochDay(), t.solarWh, t.carWh, t.carSolarWh, t.importWh, t.exportWh, t.savedCents));
        }
        for (var g : goals.entrySet()) if (!g.getKey().isBefore(firstDay.minusDays(1))) write.accept(goalLine(g.getKey(), g.getValue()));
        for (Session x : sessions) if (x.end >= from) write.accept(sessionLine('T', x));
        if (open != null) write.accept(sessionLine('O', open));
        for (Event e : events) if (e.t() >= from) write.accept("E\t" + e.t() + "\t" + e.text());
        for (Sample s : samples) {
            if (s.t() < from) continue;
            write.accept("S\t" + s.t() + "\t" + s.solar() + "\t" + s.home() + "\t" + s.grid() + "\t" + s.car() + "\t"
                    + s.mode() + "\t" + s.flags());
        }
    }

    public synchronized boolean journalPending() { return journalDirty; }

    public synchronized void markJournalSaved() { journalDirty = false; }

    private static int clamp(double watts) {
        return (int) Math.max(-32000, Math.min(32000, Math.round(watts)));
    }
}
