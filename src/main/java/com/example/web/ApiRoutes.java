package com.example.web;

import com.example.config.AppConfig;
import com.example.daylight.DaylightSchedule;
import com.example.model.ChargingMode;
import com.example.state.SystemState;
import com.example.wallbox.Charger;
import com.example.wallbox.WallboxController;
import com.example.wallbox.WallboxSessionRegistry;
import io.javalin.Javalin;
import io.javalin.http.Context;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.OptionalInt;

/** Dashboard page and REST endpoints. */
public class ApiRoutes {

    private static final Logger logger = LoggerFactory.getLogger(ApiRoutes.class);

    private final SystemState state;
    private final WallboxController wallbox;
    private final WallboxSessionRegistry sessions;
    private final DaylightSchedule daylight;
    private final com.example.history.HistoryService history;

    private final com.example.notify.Notifier notifier;

    public ApiRoutes(SystemState state, WallboxController wallbox, WallboxSessionRegistry sessions,
                     DaylightSchedule daylight, com.example.history.HistoryService history, com.example.notify.Notifier notifier) {
        this.notifier = notifier;
        this.history = history;
        this.daylight = daylight;
        this.state = state;
        this.wallbox = wallbox;
        this.sessions = sessions;
    }

    /** Name of the header every changing request must carry. A web page on another site cannot add it without our permission. */
    public static final String SAFE_HEADER = "X-WattSync";
    public static final String CAR_UNSUPPORTED = "Car control needs the ESP32 board.";
    static final String[] CAR_ROUTES = {"/api/car/pair", "/api/car/refresh", "/api/car/watch", "/api/car/test", "/api/car/cancel", "/api/car/forget", "/api/car/through", "/api/car/policy", "/api/car/wake", "/api/car/allowwake", "/api/car/mode"};

    /** Changing requests (POST) are allowed only with the safe header; reading is always allowed. */
    static boolean isAllowed(String method, String headerValue) {
        return !"POST".equalsIgnoreCase(method) || "1".equals(headerValue);
    }

    public void register(Javalin app) {
        // Without this, any web page open on a phone or laptop in the house could send the controller a POST
        // (a "simple" cross-site request needs no permission): pause it, switch the mode, ask for 40 A.
        app.before("/api/*", ctx -> {
            if (!isAllowed(ctx.method().name(), ctx.header(SAFE_HEADER))) {
                throw new io.javalin.http.ForbiddenResponse("Blocked: changing requests need the " + SAFE_HEADER
                        + " header (the dashboard adds it). With curl: -H '" + SAFE_HEADER + ": 1'");
            }
        });
        app.get("/", this::dashboard);
        app.get("/favicon.ico", ctx -> {
            ctx.header("Cache-Control", "public, max-age=86400");
            ctx.contentType("image/png").result(DashboardPage.favicon());
        });
        app.post("/api/mode", this::updateMode);
        app.post("/api/allowance", this::updateAllowance);
        app.post("/api/daylight", this::updateDaylight);
        app.post("/api/pause", this::updatePause);
        app.post("/api/rate", this::updateRate);
        app.post("/api/maxamps", this::updateMaxAmps);
        app.post("/api/goal", this::updateGoal);
        app.post("/api/notify", this::updateNotify);
        app.post("/api/site", this::updateSite);
        app.post("/api/notify/test", this::testNotify);
        // The car link (Bluetooth) exists on the ESP32 board only; the Pi answers the same routes so the dashboard behaves the same.
        for (String carRoute : CAR_ROUTES) app.post(carRoute, ctx -> ctx.status(400).result(CAR_UNSUPPORTED));
        app.get("/api/car/hotspot", ctx -> ctx.status(400).result(CAR_UNSUPPORTED));   // Tesla mode and its hotspot are board-only
        app.post("/api/clock", ctx -> ctx.status(400).result(CAR_UNSUPPORTED));
        app.get("/api/history", this::history);
        app.get("/api/days", this::days);
        app.get("/api/sessions", this::sessions);
        app.get("/api/events", this::events);
        app.get("/api/diagnostics", this::diagnostics);
        app.get("/api/export.csv", this::exportCsv);
        app.get("/api/telemetry", this::telemetry);
    }

    private void dashboard(Context ctx) {
        ctx.res().setContentType("text/html; charset=utf-8");
        ctx.header("Cache-Control", "no-cache");   // the Home Screen app should pick up a new version of the page after an update
        ctx.result(DashboardPage.html());
    }

    private void updateMode(Context ctx) {
        String requested = ctx.queryParam("mode");
        if (requested == null) {
            ctx.status(400).result("Missing mode.");
            return;
        }
        ChargingMode mode = ChargingMode.parse(requested).orElse(null);
        if (mode == null) {
            ctx.status(400).result("Unknown mode: " + requested);
            return;
        }

        // Check the current before changing anything, so a bad value never half-applies.
        String ampsParam = ctx.queryParam("amps");
        OptionalInt fastAmps = OptionalInt.empty();
        if (mode == ChargingMode.FAST && ampsParam != null) {
            fastAmps = parseFastAmps(ampsParam, state.maxAmps());
            if (fastAmps.isEmpty()) {
                ctx.status(400).result("Current must be a whole number from " + AppConfig.MIN_AMPS + " to "
                        + state.maxAmps() + " A.");
                return;
            }
        }

        state.setMode(mode);
        logger.info("[MODE SWITCH] System strategy updated to: {}", mode);
        history.addEvent("Mode set to " + modeLabel(mode));

        if (mode == ChargingMode.CHEAPEST && !state.paused()) {
            applyCheapNow();
        }
        if (mode == ChargingMode.FAST) {
            fastAmps.ifPresent(amps -> {
                state.setFastModeAmps(amps);
                logger.info("[FAST CONFIG] Target current updated to {} A", amps);
                history.addEvent("Custom speed set to " + amps + " A");
            });
            if (!state.paused()) {
                logger.info("[AUTOMATION RULE] Fast Override Active -> Enforcing current: {} Amps", state.fastModeAmps());
                wallbox.setCommandContext("custom current");
                wallbox.sendChargingLimit(state.fastModeAmps());
                wallbox.setCommandContext("");
            }
        }
        ctx.status(200).result("Mode updated");
    }

    /** The most current the charger is ever asked for (the house circuit's limit), in any mode. */
    /** Departure goal: kwh 0 turns it off; byMinutes is the time of day, in minutes after local midnight. */
    private void updateGoal(Context ctx) {
        OptionalInt kwh = parseInRange(ctx.queryParam("kwh"), 0, AppConfig.MAX_GOAL_KWH);
        String byParam = ctx.queryParam("byMinutes");
        OptionalInt by = byParam == null ? OptionalInt.of(state.goalMinutes()) : parseInRange(byParam, 0, 1439);
        if (kwh.isEmpty() || by.isEmpty()) {
            ctx.status(400).result("kwh must be a whole number from 0 to " + AppConfig.MAX_GOAL_KWH + " and byMinutes from 0 to 1439.");
            return;
        }
        state.setGoal(kwh.getAsInt(), by.getAsInt());
        String text = kwh.getAsInt() == 0 ? "Departure goal turned off"
                : "Departure goal: " + kwh.getAsInt() + " kWh by " + String.format("%d:%02d", by.getAsInt() / 60, by.getAsInt() % 60);
        logger.info("[GOAL] {}", text);
        history.addEvent(text);
        ctx.json(Map.of("goalKwh", kwh.getAsInt(), "goalMinutes", by.getAsInt()));
    }

    /** Notifications: url is a web address to post to, or empty to turn them off. */
    private void updateNotify(Context ctx) {
        String url = ctx.queryParam("url");
        if (url == null || !com.example.notify.Notifier.isValidUrl(url.trim())) {
            ctx.status(400).result("url must be empty or an http(s) address of at most " + com.example.notify.Notifier.MAX_URL_LENGTH + " characters.");
            return;
        }
        state.setNotifyUrl(url.trim());
        notifier.setUrl(url.trim());
        logger.info("[NOTIFY] Notifications {}", url.isBlank() ? "turned off" : "set to " + notifier.maskedHost());
        history.addEvent(url.isBlank() ? "Notifications turned off" : "Notifications turned on");
        ctx.status(200).result("Notifications updated");
    }

    private void testNotify(Context ctx) {
        String problem = notifier.sendTest();
        if (problem.isEmpty()) ctx.status(200).result("Sent");
        else ctx.status(502).result("Could not send: " + problem);
    }

    private void updateMaxAmps(Context ctx) {
        OptionalInt amps = parseFastAmps(ctx.queryParam("amps"), AppConfig.MAX_FAST_AMPS);
        if (amps.isEmpty()) {
            ctx.status(400).result("amps must be a whole number from " + AppConfig.MIN_AMPS + " to " + AppConfig.MAX_FAST_AMPS);
            return;
        }
        state.setMaxAmps(amps.getAsInt());
        logger.info("[MAX CURRENT] Maximum current set to {} A", amps.getAsInt());
        history.addEvent("Maximum current set to " + amps.getAsInt() + " A");
        if (state.mode() == ChargingMode.CHEAPEST && !state.paused()) applyCheapNow();
        if (state.fastModeAmps() > amps.getAsInt()) {
            state.setFastModeAmps(amps.getAsInt());
            history.addEvent("Custom speed lowered to " + amps.getAsInt() + " A");
            if (state.mode() == ChargingMode.FAST && !state.paused()) {
                wallbox.setCommandContext("maximum current lowered");
                wallbox.sendChargingLimit(state.fastModeAmps());
                wallbox.setCommandContext("");
            }
        }
        ctx.status(200).result("Maximum current updated");
    }

    private void updateAllowance(Context ctx) {
        OptionalInt watts = parseAllowance(ctx.queryParam("watts"));
        if (watts.isEmpty()) {
            ctx.status(400).result("watts must be a whole number from 0 to " + AppConfig.MAX_GRID_ALLOWANCE_WATTS);
            return;
        }
        state.setGridAllowanceWatts(watts.getAsInt());
        logger.info("[ALLOWANCE] Grid allowance set to {} W", watts.getAsInt());
        history.addEvent("Grid allowance set to " + watts.getAsInt() + " W");
        ctx.status(200).result("Allowance updated");
    }

    /** The kill switch: stops charging whatever the mode; resuming hands the charger back to the saved mode. */
    private void updatePause(Context ctx) {
        String value = ctx.queryParam("enabled");
        if (!"true".equals(value) && !"false".equals(value)) {
            ctx.status(400).result("enabled must be true or false");
            return;
        }
        boolean pause = Boolean.parseBoolean(value);
        state.setPaused(pause);
        if (pause) {
            logger.info("[PAUSE] Charging paused by the user (mode {} is kept).", state.mode());
            history.addEvent("Charging paused by you");
            wallbox.setCommandContext("paused by you");
            wallbox.sendChargingLimit(0);
            wallbox.setCommandContext("");
        } else {
            logger.info("[PAUSE] Charging resumed: back to {}.", state.mode());
            history.addEvent("Charging resumed (" + modeLabel(state.mode()) + ")");
            if (state.mode() == ChargingMode.CHEAPEST) applyCheapNow();
            if (state.mode() == ChargingMode.FAST) {
                wallbox.setCommandContext("custom current");
                wallbox.sendChargingLimit(state.fastModeAmps());
                wallbox.setCommandContext("");
            }
        }
        ctx.status(200).result(pause ? "Charging paused" : "Charging resumed");
    }

    static String modeLabel(ChargingMode mode) {
        return mode == ChargingMode.FAST ? "Custom" : mode == ChargingMode.ECO ? "Eco" : mode == ChargingMode.CHEAPEST ? "Cheapest hours" : "Max solar";
    }

    /** Cheapest hours: send the right limit for this moment (maximum current in the cheap hours, stop in the expensive ones). */
    private void applyCheapNow() {
        boolean expensive = com.example.charging.CheapHours.expensive(System.currentTimeMillis(), AppConfig.siteZone(),
                state.peakStartHour(), state.peakEndHour(), state.peakWeekends());
        wallbox.setCommandContext(expensive ? "expensive hours" : "cheap hours");
        wallbox.sendChargingLimit(expensive ? 0 : state.maxAmps());
        wallbox.setCommandContext("");
    }

    /** A whole number in a range, or empty. */
    static OptionalInt parseInRange(String value, int min, int max) {
        if (value == null) return OptionalInt.empty();
        try {
            int n = Integer.parseInt(value.trim());
            return n >= min && n <= max ? OptionalInt.of(n) : OptionalInt.empty();
        } catch (NumberFormatException e) {
            return OptionalInt.empty();
        }
    }

    /** The price of electricity: the normal price (cents) and, optionally, a peak price with its hours. Every part is optional. */
    /** A value from the form body (where the dashboard puts the location) or, failing that, the address. */
    private static String formOrQuery(Context ctx, String name) {
        String v = ctx.formParam(name);
        return v != null ? v : ctx.queryParam(name);
    }

    /** Saves where the panels are. It is used from the next start of the app (the clock, the history days and the daylight window are set up then). */
    private void updateSite(Context ctx) {
        String lat = formOrQuery(ctx, "lat"), lon = formOrQuery(ctx, "lon"), tz = formOrQuery(ctx, "tz");
        if (lat == null || lon == null || tz == null) {
            ctx.status(400).result("Missing lat, lon or tz.");
            return;
        }
        var result = com.example.config.SiteInput.parse(lat, lon, tz, formOrQuery(ctx, "name"));
        if (!result.ok()) {
            ctx.status(400).result(result.error());
            return;
        }
        var site = result.site();
        state.setSite(site.latitude(), site.longitude(), site.timezone(), site.name());
        ctx.result("Location saved. Restart the app to use it for sunrise times and the clock.");
    }

    private void updateRate(Context ctx) {
        String centsParam = ctx.queryParam("cents"), peakParam = ctx.queryParam("peakCents");
        String fromParam = ctx.queryParam("peakStart"), toParam = ctx.queryParam("peakEnd"), weekendsParam = ctx.queryParam("peakWeekends");
        if (centsParam == null && peakParam == null && fromParam == null && toParam == null && weekendsParam == null) {
            ctx.status(400).result("Give cents, peakCents, peakStart, peakEnd or peakWeekends.");
            return;
        }
        if (weekendsParam != null && !"true".equals(weekendsParam) && !"false".equals(weekendsParam)) {
            ctx.status(400).result("peakWeekends must be true or false");
            return;
        }
        // Check everything before changing anything, so a bad value never half-applies.
        OptionalInt cents = centsParam == null ? OptionalInt.of(state.rateCents()) : parseInRange(centsParam, 0, AppConfig.MAX_RATE_CENTS);
        OptionalInt peak = peakParam == null ? OptionalInt.of(state.peakRateCents()) : parseInRange(peakParam, 0, AppConfig.MAX_RATE_CENTS);
        OptionalInt from = fromParam == null ? OptionalInt.of(state.peakStartHour()) : parseInRange(fromParam, 0, 23);
        OptionalInt to = toParam == null ? OptionalInt.of(state.peakEndHour()) : parseInRange(toParam, 1, 24);
        if (cents.isEmpty() || peak.isEmpty()) {
            ctx.status(400).result("cents and peakCents must be whole numbers from 0 to " + AppConfig.MAX_RATE_CENTS);
            return;
        }
        if (from.isEmpty() || to.isEmpty() || from.getAsInt() == to.getAsInt()) {
            ctx.status(400).result("peakStart must be 0 to 23 and peakEnd 1 to 24, and they must differ");
            return;
        }
        boolean rateChanged = cents.getAsInt() != state.rateCents();
        boolean peakChanged = peak.getAsInt() != state.peakRateCents() || from.getAsInt() != state.peakStartHour()
                || to.getAsInt() != state.peakEndHour();
        boolean weekends = weekendsParam == null ? state.peakWeekends() : Boolean.parseBoolean(weekendsParam);
        boolean weekendsChanged = weekends != state.peakWeekends();
        state.setPeakWeekends(weekends);
        state.setRateCents(cents.getAsInt());
        state.setPeakRate(peak.getAsInt(), from.getAsInt(), to.getAsInt());
        logger.info("[RATE] Price {} c/kWh, peak {} c/kWh from {}:00 to {}:00", cents.getAsInt(), peak.getAsInt(), from.getAsInt(), to.getAsInt());
        if (rateChanged) history.addEvent("Price set to " + cents.getAsInt() + "¢ per kWh");
        if (peakChanged) {
            history.addEvent(peak.getAsInt() == 0 ? "Peak price turned off"
                    : "Peak price set to " + peak.getAsInt() + "¢ from " + from.getAsInt() + ":00 to " + to.getAsInt() + ":00");
        }
        if (weekendsChanged) history.addEvent(weekends ? "Expensive hours apply every day" : "Expensive hours apply Monday to Friday only");
        if ((peakChanged || weekendsChanged) && state.mode() == ChargingMode.CHEAPEST && !state.paused()) applyCheapNow();
        ctx.status(200).result("Price updated");
    }

    /**
     * A day's power by 5 minutes (hourly after 30 days), the energy totals and, for today, the activity log: the data
     * behind the dashboard's graph. {@code daysAgo} picks a past day (0 is today).
     */
    private void history(Context ctx) {
        int daysAgo = 0;
        String param = ctx.queryParam("daysAgo");
        if (param != null) {
            try {
                daysAgo = Integer.parseInt(param.trim());
            } catch (NumberFormatException e) {
                daysAgo = -1;
            }
            if (daysAgo < 0 || daysAgo > 364) {
                ctx.status(400).result("daysAgo must be a whole number from 0 to 364");
                return;
            }
        }
        long now = System.currentTimeMillis() / 1000;
        java.time.LocalDate day = history.localDay(now).minusDays(daysAgo);
        long dayStart = history.startOfLocalDay(history.localDay(now).minusDays(daysAgo).atStartOfDay(history.zone()).toEpochSecond());
        long dayEnd = daysAgo == 0 ? Long.MAX_VALUE : day.plusDays(1).atStartOfDay(history.zone()).toEpochSecond() - 1;
        int step = daysAgo <= 29 ? 300 : 3600;   // after 30 days only hourly records remain

        Map<String, Object> body = new LinkedHashMap<>();
        body.put("now", now);
        body.put("daysAgo", daysAgo);
        body.put("today", history.localDay(now).toEpochDay());
        body.put("dayStart", dayStart);

        java.util.List<long[]> rows = new java.util.ArrayList<>();
        for (var s : history.seriesRange(dayStart, dayEnd, step)) {
            rows.add(new long[] {s.t(), s.solar(), s.home(), s.grid(), s.car(), s.mode(), s.flags()});
        }
        body.put("samples", rows);

        var t = history.totalsForDay(day);
        Map<String, Object> totals = new LinkedHashMap<>();
        totals.put("solarKwh", kwh(t.solarWh));
        totals.put("carKwh", kwh(t.carWh));
        totals.put("carSolarKwh", kwh(t.carSolarWh));
        totals.put("importKwh", kwh(t.importWh));
        totals.put("exportKwh", kwh(t.exportWh));
        totals.put("savedDollars", dollars(t));
        body.put("totals", totals);

        Map<String, Object> window = null;
        var span = daylight.windowFor(Instant.ofEpochSecond(dayStart + 43200));
        if (span.isPresent()) {
            window = new LinkedHashMap<>();
            window.put("start", span.get()[0]);
            window.put("end", span.get()[1]);
        }
        body.put("daylight", window);
        ctx.json(body);
    }

    /** The charging sessions, newest first, plus the one running now. */
    /** How the app is doing: uptime and memory. (The board also counts restarts and crashes; the Pi's service manager does that.) */
    private void diagnostics(Context ctx) {
        Runtime runtime = Runtime.getRuntime();
        long usedBytes = runtime.totalMemory() - runtime.freeMemory();
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("platform", "Raspberry Pi");
        body.put("now", System.currentTimeMillis() / 1000);
        body.put("uptimeSeconds", java.lang.management.ManagementFactory.getRuntimeMXBean().getUptime() / 1000);
        Map<String, Object> restart = new LinkedHashMap<>();
        restart.put("reason", "service start");
        restart.put("crash", false);
        body.put("lastRestart", restart);
        body.put("memory", Map.of("freeKb", (runtime.maxMemory() - usedBytes) / 1024));
        body.put("clockSet", true);
        ctx.json(body);
    }

    /** One page of the activity log, newest first: ?n=1-20 (default 5) and ?offset=0-1000, plus the total. */
    private void events(Context ctx) {
        int n = 5;
        String param = ctx.queryParam("n");
        if (param != null) {
            try {
                n = Integer.parseInt(param.trim());
            } catch (NumberFormatException e) {
                n = -1;
            }
            if (n < 1 || n > 20) {
                ctx.status(400).result("n must be a whole number from 1 to 20");
                return;
            }
        }
        OptionalInt offset = parseOffset(ctx.queryParam("offset"));
        if (offset.isEmpty()) {
            ctx.status(400).result("offset must be a whole number from 0 to 1000");
            return;
        }
        java.util.List<Map<String, Object>> rows = new java.util.ArrayList<>();
        for (var e : history.recentEvents(n, offset.getAsInt())) {
            Map<String, Object> entry = new LinkedHashMap<>();
            entry.put("t", e.t());
            entry.put("text", e.text());
            rows.add(entry);
        }
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("events", rows);
        body.put("total", history.eventCount());
        ctx.json(body);
    }

    /** The paging offset: 0 when absent, empty if it is not a whole number from 0 to 1000. */
    static OptionalInt parseOffset(String value) {
        if (value == null) return OptionalInt.of(0);
        try {
            int offset = Integer.parseInt(value.trim());
            return offset >= 0 && offset <= 1000 ? OptionalInt.of(offset) : OptionalInt.empty();
        } catch (NumberFormatException e) {
            return OptionalInt.empty();
        }
    }

    private void sessions(Context ctx) {
        int n = 10;
        String param = ctx.queryParam("n");
        if (param != null) {
            try {
                n = Integer.parseInt(param.trim());
            } catch (NumberFormatException e) {
                n = -1;
            }
            if (n < 1 || n > 100) {
                ctx.status(400).result("n must be a whole number from 1 to 100");
                return;
            }
        }
        OptionalInt offset = parseOffset(ctx.queryParam("offset"));
        if (offset.isEmpty()) {
            ctx.status(400).result("offset must be a whole number from 0 to 1000");
            return;
        }
        long now = System.currentTimeMillis() / 1000;
        Map<String, Object> body = new LinkedHashMap<>();
        java.util.List<Map<String, Object>> rows = new java.util.ArrayList<>();
        for (var x : history.recentSessions(n, offset.getAsInt())) rows.add(sessionJson(x, x.end));
        body.put("sessions", rows);
        body.put("total", history.sessionCount());
        var open = history.openSession();
        body.put("open", open == null ? null : sessionJson(open, now));
        ctx.json(body);
    }

    private Map<String, Object> sessionJson(com.example.history.HistoryService.Session x, long endAt) {
        double energyWh = x.energyWh();
        double hours = Math.max(1, endAt - x.start) / 3600.0;
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("start", x.start);
        out.put("end", x.end);
        out.put("kwh", kwh(energyWh));
        out.put("sunPct", x.estimatedWh > 1 ? Math.round(Math.min(1.0, x.sunWh / x.estimatedWh) * 100) : 0);
        out.put("sunKwh", kwh(Math.min(x.sunWh, x.estimatedWh)));
        out.put("avgAmps", Math.round(energyWh / hours / AppConfig.voltage() * 10) / 10.0);
        out.put("reason", x.reason);
        out.put("tag", x.tag);   // fingerprint of the charger's ID tag, "" if none (never the tag itself)
        out.put("plugged", x.pluggedAt);   // when the car was plugged in before this session, 0 if unknown
        out.put("saved", plausibleSaving(x.savedCents, x.sunWh) ? Math.round(x.savedCents) / 100.0 : -1);
        out.put("mode", x.mode == 0 ? "ECO" : x.mode == 1 ? "MAX_SOLAR" : x.mode == 2 ? "FAST" : x.mode == 3 ? "CHEAPEST" : "");
        out.put("modeMixed", x.mixed);
        return out;
    }

    /** Daily energy totals for the last {@code n} days (default 30, up to 365), oldest first. */
    private void days(Context ctx) {
        int n = 30;
        String param = ctx.queryParam("n");
        if (param != null) {
            try {
                n = Integer.parseInt(param.trim());
            } catch (NumberFormatException e) {
                n = -1;
            }
            if (n < 1 || n > 365) {
                ctx.status(400).result("n must be a whole number from 1 to 365");
                return;
            }
        }
        java.time.LocalDate today = history.localDay(System.currentTimeMillis() / 1000);
        java.util.List<double[]> rows = new java.util.ArrayList<>();
        for (var e : history.allDays().entrySet()) {
            if (!e.getKey().isAfter(today.minusDays(n)) || e.getKey().isAfter(today)) continue;
            var t = e.getValue();
            rows.add(new double[] {e.getKey().toEpochDay(), kwh(t.solarWh), kwh(t.carWh), kwh(t.carSolarWh),
                    kwh(t.importWh), kwh(t.exportWh), dollars(t), history.goalOutcome(e.getKey())});   // last: 0 no goal judged, 1 met, 2 missed
        }
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("today", today.toEpochDay());
        body.put("days", rows);
        ctx.json(body);
    }

    /** Every day's energy totals as a spreadsheet file, a simple backup of the long-term numbers. */
    private void exportCsv(Context ctx) {
        StringBuilder csv = new StringBuilder("date,solar_kwh,car_kwh,car_from_sun_kwh,grid_import_kwh,grid_export_kwh,saved_dollars\n");
        for (var e : history.allDays().entrySet()) {
            var t = e.getValue();
            csv.append(String.format(java.util.Locale.ROOT, "%s,%.2f,%.2f,%.2f,%.2f,%.2f,%s%n", e.getKey(), t.solarWh / 1000,
                    t.carWh / 1000, t.carSolarWh / 1000, t.importWh / 1000, t.exportWh / 1000,
                    plausibleSaving(t.savedCents, t.carSolarWh) && t.savedCents > 0 ? String.format(java.util.Locale.ROOT, "%.2f", t.savedCents / 100) : ""));
        }
        ctx.contentType("text/csv");
        ctx.header("Content-Disposition", "attachment; filename=\"wattsync-days.csv\"");
        ctx.result(csv.toString());
    }

    private static double kwh(double wh) { return Math.round(wh / 10.0) / 100.0; }

    /** Money saved, in dollars, or -1 when no price was recorded for that energy (older days), so the page can estimate it. */
    private double dollars(com.example.history.HistoryService.DayTotals t) {
        // Money recorded for only part of the day (the day began before prices were recorded) would read far too low:
        // a price well under the cheapest rate in use counts as "not recorded", so the page estimates it instead.
        return plausibleSaving(t.savedCents, t.carSolarWh) ? Math.round(t.savedCents) / 100.0 : -1;
    }

    /** False when the recorded money is missing or far below what the sun's energy is worth at the cheapest rate in use. */
    private boolean plausibleSaving(double savedCents, double sunWh) {
        if (sunWh < 1) return true;
        if (savedCents <= 0) return false;
        double lowestRate = state.peakRateCents() > 0 ? Math.min(state.rateCents(), state.peakRateCents()) : state.rateCents();
        return sunWh < 1000 || savedCents / (sunWh / 1000.0) >= lowestRate * 0.5;
    }

    private void updateDaylight(Context ctx) {
        String value = ctx.queryParam("enabled");
        String startParam = ctx.queryParam("startMin"), endParam = ctx.queryParam("endMin");
        if (value == null && startParam == null && endParam == null) {
            ctx.status(400).result("Give enabled, startMin or endMin.");
            return;
        }
        if (value != null && !"true".equals(value) && !"false".equals(value)) {
            ctx.status(400).result("enabled must be true or false");
            return;
        }
        // Check everything before changing anything, so a bad value never half-applies.
        boolean marginsGiven = startParam != null || endParam != null;
        OptionalInt start = startParam == null ? OptionalInt.of(state.daylightStartMinutes()) : parseMargin(startParam);
        OptionalInt end = endParam == null ? OptionalInt.of(state.daylightEndMinutes()) : parseMargin(endParam);
        if (marginsGiven && (start.isEmpty() || end.isEmpty())) {
            ctx.status(400).result("startMin and endMin must be whole numbers from 0 to " + AppConfig.MAX_DAYLIGHT_MARGIN_MINUTES);
            return;
        }
        if (value != null) {
            state.setDaylightWindowEnabled(Boolean.parseBoolean(value));
            logger.info("[DAYLIGHT] Daylight window {}", "true".equals(value) ? "enabled" : "disabled");
        }
        if (marginsGiven) {
            state.setDaylightMargins(start.getAsInt(), end.getAsInt());
            daylight.setMargins(java.time.Duration.ofMinutes(start.getAsInt()), java.time.Duration.ofMinutes(end.getAsInt()));
            logger.info("[DAYLIGHT] Margins set to {} min after sunrise, {} min before sunset: {}", start.getAsInt(),
                    end.getAsInt(), daylight.describe(Instant.now()));
            if (history != null) {
                history.addEvent("Solar hours set: " + start.getAsInt() + " min after sunrise to " + end.getAsInt()
                        + " min before sunset");
            }
        }
        ctx.status(200).result("Daylight setting updated");
    }

    /** A whole number of minutes from 0 to the maximum, otherwise empty. */
    static OptionalInt parseMargin(String value) {
        if (value == null) return OptionalInt.empty();
        try {
            int minutes = Integer.parseInt(value.trim());
            return minutes >= 0 && minutes <= AppConfig.MAX_DAYLIGHT_MARGIN_MINUTES ? OptionalInt.of(minutes) : OptionalInt.empty();
        } catch (NumberFormatException e) {
            return OptionalInt.empty();
        }
    }

    /** A whole number of watts from 0 to the maximum, otherwise empty. */
    static OptionalInt parseAllowance(String value) {
        if (value == null) return OptionalInt.empty();
        try {
            int watts = Integer.parseInt(value.trim());
            boolean inRange = watts >= 0 && watts <= AppConfig.MAX_GRID_ALLOWANCE_WATTS;
            return inRange ? OptionalInt.of(watts) : OptionalInt.empty();
        } catch (NumberFormatException e) {
            return OptionalInt.empty();
        }
    }

    /** A whole number of amps within the Custom (FAST) range up to {@code maxAmps}, otherwise empty. */
    static OptionalInt parseFastAmps(String value, int maxAmps) {
        if (value == null) return OptionalInt.empty();
        try {
            int amps = Integer.parseInt(value.trim());
            boolean inRange = amps >= AppConfig.MIN_AMPS && amps <= maxAmps;
            return inRange ? OptionalInt.of(amps) : OptionalInt.empty();
        } catch (NumberFormatException e) {
            return OptionalInt.empty();
        }
    }

    private void telemetry(Context ctx) {
        SystemState.Control control = state.control();
        double volts = AppConfig.voltage();
        double chargerAmps = wallbox.currentDrawAmps();
        double chargerWatts = chargerAmps * volts;
        Integer limit = wallbox.currentLimitAmps();
        Charger.Status chargerStatus = wallbox.primaryStatus();

        Map<String, Object> power = new LinkedHashMap<>();
        power.put("voltage", volts);
        power.put("solarWatts", state.solarWatts());
        power.put("gridWatts", control == null ? 0 : control.gridWatts());       // + import, - export
        power.put("chargerWatts", chargerWatts);
        power.put("homeWatts", control == null ? 0
                : Math.max(0, state.solarWatts() + control.gridWatts() - chargerWatts));

        Map<String, Object> charger = new LinkedHashMap<>();
        charger.put("connected", !sessions.isEmpty());
        charger.put("status", chargerStatus == null ? "NONE" : chargerStatus.name());
        charger.put("amps", chargerAmps);
        charger.put("limitAmps", limit == null ? 0 : limit);
        charger.put("restored", wallbox.anyRestored());
        charger.put("drawMeasured", wallbox.drawIsMeasured());
        charger.put("lastCommand", wallbox.lastCommand());
        charger.put("lastCommandAt", wallbox.lastCommandAtMillis() / 1000);   // epoch seconds, 0 if none yet

        Map<String, Object> controller = new LinkedHashMap<>();
        controller.put("hasReading", control != null);
        controller.put("availableAmps", control == null ? 0 : control.availableAmps());
        controller.put("availableWatts", control == null ? 0 : control.availableAmps() * volts);
        controller.put("smoothedAmps", control == null ? 0 : control.smoothedAmps());
        controller.put("targetAmps", control == null ? 0 : control.targetAmps());
        controller.put("reason", control == null ? "waiting for first reading" : control.reason());
        controller.put("readingAgeSeconds",
                control == null ? -1 : (System.currentTimeMillis() - control.readingAtMillis()) / 1000);
        // Seconds until the controller may start or stop on its own, counted from the latest reading (-1: no timer running).
        long waitLeft = control == null || control.waitSeconds() < 0 ? -1
                : Math.max(0, control.waitSeconds() - (System.currentTimeMillis() - control.readingAtMillis()) / 1000);
        controller.put("waitSeconds", waitLeft);
        // ECO and MAX_SOLAR both use the allowance; Custom (FAST) ignores it.
        controller.put("gridAllowanceWatts", state.mode() == ChargingMode.ECO || state.mode() == ChargingMode.MAX_SOLAR ? state.gridAllowanceWatts() : 0);
        // Cheapest hours: is the price high right now, and how long until it changes (-1: never within a week).
        boolean expensiveNow = com.example.charging.CheapHours.expensive(System.currentTimeMillis(), AppConfig.siteZone(),
                state.peakStartHour(), state.peakEndHour(), state.peakWeekends());
        controller.put("expensiveNow", expensiveNow);
        controller.put("secondsToPriceChange", com.example.charging.CheapHours.secondsToChange(System.currentTimeMillis(), AppConfig.siteZone(),
                state.peakStartHour(), state.peakEndHour(), state.peakWeekends()));
        controller.put("failSafe", state.failSafe());
        controller.put("pollSeconds", AppConfig.POLL_INTERVAL_SECONDS);

        Map<String, Object> body = new LinkedHashMap<>();
        body.put("car", Map.of("supported", false));
        body.put("currentMode", state.mode().name());
        body.put("paused", state.paused());
        body.put("rateCents", state.rateCents());
        body.put("peakRateCents", state.peakRateCents());
        body.put("peakStartHour", state.peakStartHour());
        body.put("peakEndHour", state.peakEndHour());
        body.put("peakWeekends", state.peakWeekends());
        body.put("fastModeAmps", state.fastModeAmps());
        body.put("maxAmps", state.maxAmps());
        Map<String, Object> goal = new LinkedHashMap<>();
        goal.put("kwh", state.goalKwh());
        goal.put("byMinutes", state.goalMinutes());
        goal.put("doneKwh", Math.round(state.goalDoneWh() / 100.0) / 10.0);
        goal.put("forcing", state.goalForcing());
        goal.put("forcedAmps", state.goalForcedAmps());
        body.put("goal", goal);
        body.put("notifyTo", notifier.maskedHost());
        body.put("gridAllowanceWatts", state.gridAllowanceWatts());
        body.put("chargerConnected", !sessions.isEmpty());
        body.put("meterLinked", true);   // the Pi cannot start without a meter token
        body.put("enphaseOk", state.enphaseOk());
        body.put("enphaseMessage", state.enphaseMessage());
        Instant now = Instant.now();
        Map<String, Object> daylightInfo = new LinkedHashMap<>();
        daylightInfo.put("enabled", state.daylightWindowEnabled());
        daylightInfo.put("open", !state.daylightWindowEnabled() || daylight.isOpen(now));
        daylightInfo.put("window", daylight.describe(now));
        daylightInfo.put("startMinutes", state.daylightStartMinutes());
        daylightInfo.put("endMinutes", state.daylightEndMinutes());

        body.put("daylight", daylightInfo);
        // Where the site is, rounded to a tenth of a degree (about 11 km): enough for a weather forecast, not a street address.
        Map<String, Object> site = new LinkedHashMap<>();
        // `set` is false until the owner has saved a location; then the environment or the built-in default is what the app uses.
        double siteLat = state.siteSet() ? state.siteLatitude() : AppConfig.siteLatitude();
        double siteLon = state.siteSet() ? state.siteLongitude() : AppConfig.siteLongitude();
        site.put("lat", Math.round(siteLat * 10) / 10.0);
        site.put("lon", Math.round(siteLon * 10) / 10.0);
        site.put("set", state.siteSet());
        site.put("timezone", state.siteSet() ? state.siteTimezone() : AppConfig.siteZone().getId());
        site.put("name", state.siteSet() ? state.siteName() : "");
        body.put("site", site);
        body.put("power", power);
        body.put("charger", charger);
        body.put("controller", controller);
        ctx.json(body);
    }
}
