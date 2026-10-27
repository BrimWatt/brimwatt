package com.example.web;

import com.example.config.AppConfig;
import com.example.ocpp.OcppRouter;
import com.example.persistence.StatePersistence;
import com.example.wallbox.Charger;
import com.example.wallbox.WallboxController;
import com.example.wallbox.WallboxSessionRegistry;
import io.javalin.Javalin;
import io.javalin.websocket.WsConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/**
 * WebSocket endpoint chargers connect to: {@code /pair} (the wallbox adds its id) or the older
 * {@code /ocpp/wallbox}. The charger id can be given in the path
 * ({@code /pair/<chargerId>}, {@code /ocpp/wallbox/{chargerId}}) or as {@code ?chargerId=}. Javalin echoes the
 * requested subprotocol (e.g. {@code ocpp1.6}) back during the handshake.
 */
public class WallboxWebSocket {

    private static final Logger logger = LoggerFactory.getLogger(WallboxWebSocket.class);
    private static final String BASE_PATH = "/ocpp/wallbox";
    /** What owners enter in the wallbox: ws://host:8080/pair (the wallbox adds its own id; may contain a slash). */
    private static final String PAIR_PATH = "/pair";

    private final WallboxController wallbox;
    private final WallboxSessionRegistry sessions;
    private final OcppRouter router;
    private final StatePersistence persistence;
    private volatile com.example.history.HistoryService history;
    private final ScheduledExecutorService resync = Executors.newSingleThreadScheduledExecutor(runnable -> {
        Thread thread = new Thread(runnable, "charger-resync");
        thread.setDaemon(true);
        return thread;
    });

    public WallboxWebSocket(WallboxController wallbox, WallboxSessionRegistry sessions, OcppRouter router,
                            StatePersistence persistence) {
        this.wallbox = wallbox;
        this.sessions = sessions;
        this.router = router;
        this.persistence = persistence;
    }

    /** Where to log the charger connecting and disconnecting. Optional. */
    public void setHistory(com.example.history.HistoryService history) { this.history = history; }

    public void register(Javalin app) {
        app.ws(BASE_PATH, this::configure);
        app.ws(BASE_PATH + "/{chargerId}", this::configure);
        app.ws(PAIR_PATH, this::configure);
        app.ws(PAIR_PATH + "/<chargerId>", this::configure);
    }

    /** Asks the charger for its real state later, unless it has already told us. */
    private void scheduleResync(Charger charger, int delaySeconds) {
        resync.schedule(() -> {
            boolean unsure = charger.isRestored() || charger.status() == Charger.Status.UNKNOWN;
            if (unsure) wallbox.requestStatus(charger);
        }, delaySeconds, TimeUnit.SECONDS);
    }

    private void configure(WsConfig ws) {
        ws.onConnect(ctx -> {
            String chargerId = ctx.pathParamMap().getOrDefault("chargerId", ctx.queryParam("chargerId"));
            if (chargerId == null) chargerId = AppConfig.DEFAULT_CHARGER_ID;

            Charger charger = sessions.register(chargerId, ctx);
            logger.info("Wallbox connected -> ID: {}. Total active sessions: {}", charger.id(), sessions.size());
            if (history != null) history.addEvent("Charger connected");

            // After a restart or a dropped connection we know nothing live, so start from the last
            // saved state and then ask the charger what it is really doing.
            persistence.recall(charger.id()).ifPresent(snapshot -> {
                charger.restoreFrom(snapshot);
                logger.info("Restored last known state of charger {}: {} (transaction {}, limit {}A); verifying.",
                        charger.id(), charger.status(), snapshot.transactionId(), snapshot.appliedAmps());
            });
            scheduleResync(charger, AppConfig.CHARGER_RESYNC_FIRST_SECONDS);
            scheduleResync(charger, AppConfig.CHARGER_RESYNC_SECOND_SECONDS);
        });

        ws.onMessage(ctx -> sessions.find(ctx).ifPresent(charger -> router.onMessage(charger, ctx.message())));

        ws.onClose(ctx -> {
            sessions.find(ctx).ifPresent(charger -> {
                persistence.onDisconnect(charger);
                if (history != null) history.addEvent("Charger disconnected");
            });
            sessions.unregister(ctx);
            logger.info("Wallbox disconnected. Remaining active sessions: {}", sessions.size());
        });

        ws.onError(ctx -> logger.error("WebSocket fault on session {}: ",
                ctx.session.getRemoteAddress(), ctx.error()));
    }
}
