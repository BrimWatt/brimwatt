package com.brimwatt;

import com.brimwatt.config.AppConfig;
import com.brimwatt.daylight.DaylightWindow;
import com.brimwatt.enphase.EnphaseClient;
import com.brimwatt.enphase.EnphasePoller;
import com.brimwatt.enphase.HttpClientFactory;
import com.brimwatt.history.HistoryService;
import com.brimwatt.history.HistoryStore;
import com.brimwatt.ocpp.CoreHandlers;
import com.brimwatt.ocpp.OcppRouter;
import com.brimwatt.persistence.StatePersistence;
import com.brimwatt.persistence.StateStore;
import com.brimwatt.state.SystemState;
import com.brimwatt.wallbox.WallboxController;
import com.brimwatt.wallbox.WallboxSessionRegistry;
import com.brimwatt.web.ApiRoutes;
import com.brimwatt.web.WallboxWebSocket;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.javalin.Javalin;
import java.time.Duration;
import java.time.Instant;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Entry point: wires the components together and starts the server and poller. */
public class SmartEnergyApp {

    private static final Logger logger = LoggerFactory.getLogger(SmartEnergyApp.class);

    public static void main(String[] args) {
        logger.info("Initializing Brimwatt components...");
        try {
            SystemState state = new SystemState();
            WallboxSessionRegistry sessions = new WallboxSessionRegistry();
            StatePersistence persistence = new StatePersistence(new StateStore(AppConfig.stateFile()), state, sessions);
            persistence.restore();
            if (state.siteSet()) AppConfig.useSite(state.siteLatitude(), state.siteLongitude(), state.siteTimezone());   // before anything reads the zone
            HistoryService history = new HistoryService(AppConfig.siteZone());
            history.setPriceProvider(t -> com.brimwatt.state.RateSchedule.centsAt(state.rateCents(), state.peakRateCents(),
                    state.peakStartHour(), state.peakEndHour(), java.time.Instant.ofEpochSecond(t).atZone(history.zone()).getHour()));
            HistoryStore historyStore = new HistoryStore(AppConfig.historyFile(), history);
            historyStore.load();
            com.brimwatt.notify.Notifier notifier = new com.brimwatt.notify.Notifier();
            notifier.setUrl(state.notifyUrl());
            history.setEventListener(notifier::onEvent);
            if (persistence.restoredFromDamagedFile()) {
                history.addEvent("The saved settings were damaged: charging is paused until you press Resume");
            }
            WallboxController wallbox = new WallboxController(sessions, state);
            wallbox.setHistory(history);
            OcppRouter ocpp = new OcppRouter(CoreHandlers.all(wallbox::onChargerChanged, history));
            EnphaseClient enphase = new EnphaseClient(
                    HttpClientFactory.createTrustAll(), new ObjectMapper(), AppConfig.enphaseToken());

            Javalin app = Javalin.create(config ->
                    config.jetty.modifyWebSocketServletFactory(
                            factory -> factory.setIdleTimeout(AppConfig.WEBSOCKET_IDLE_TIMEOUT)))
                    .start(AppConfig.HTTP_PORT);
            DaylightWindow daylight = new DaylightWindow(AppConfig.siteLatitude(), AppConfig.siteLongitude(),
                    AppConfig.siteZone(), Duration.ofMinutes(state.daylightStartMinutes()),
                    Duration.ofMinutes(state.daylightEndMinutes()));
            logger.info("Daylight window today: {} (timezone {}, latitude {}, longitude {}).", daylight.describe(Instant.now()),
                    daylight.zone(), AppConfig.siteLatitude(), AppConfig.siteLongitude());

            new ApiRoutes(state, wallbox, sessions, daylight, history, notifier).register(app);
            WallboxWebSocket wallboxSocket = new WallboxWebSocket(wallbox, sessions, ocpp, persistence);
            wallboxSocket.setHistory(history);
            wallboxSocket.register(app);
            logger.info("Javalin listening on port {}.", AppConfig.HTTP_PORT);

            EnphasePoller poller = new EnphasePoller(enphase, state, wallbox, daylight);
            poller.setHistory(history);
            poller.start();
            persistence.start();
            historyStore.start();
        } catch (Exception e) {
            logger.error("Fatal exception during startup: ", e);
            System.exit(1);
        }
    }
}
