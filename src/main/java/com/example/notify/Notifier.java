package com.example.notify;

import com.example.history.HistoryService;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.Map;
import java.util.function.LongSupplier;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Tells the owner about the few events worth a message (charging ended, solar meter lost, charger disconnected ...) by
 * posting a small JSON body to a web address they chose: {"text": "...", "content": "..."} (Slack, Discord, Home
 * Assistant, IFTTT, Zapier and similar all accept one of these). Nothing is sent until an address is set.
 */
public class Notifier {

    private static final Logger logger = LoggerFactory.getLogger(Notifier.class);

    /** The same message is sent at most this often, so a flapping meter cannot flood a phone. */
    static final long REPEAT_MILLIS = 10 * 60_000L;
    public static final int MAX_URL_LENGTH = 200;

    /** How a message leaves the app; replaced in tests. */
    public interface Sender {
        void send(String url, String json) throws Exception;
    }

    private static final String[] WORTH_SENDING = {
        "Charging ended", "Lost contact with the solar meter", "Solar meter is back", "Charger disconnected",
        "The saved settings were damaged", "Departure goal needs charging now",
        "Car not reachable", "Car reachable again", "Control moved",
    };

    private final Sender sender;
    private final LongSupplier clockMillis;
    private final ObjectMapper mapper = new ObjectMapper();
    private final java.util.Map<String, Long> lastSent = new java.util.HashMap<>();
    private volatile String url = "";

    public Notifier() { this(Notifier::httpSend, System::currentTimeMillis); }

    public Notifier(Sender sender, LongSupplier clockMillis) {
        this.sender = sender;
        this.clockMillis = clockMillis;
    }

    public String url() { return url; }
    public void setUrl(String url) { this.url = url == null ? "" : url; }
    public boolean enabled() { return !url.isEmpty(); }

    /** True for an empty string (turn it off) or an http(s) address of a sensible length. */
    public static boolean isValidUrl(String value) {
        if (value == null) return false;
        if (value.isEmpty()) return true;
        if (value.length() > MAX_URL_LENGTH) return false;
        try {
            URI u = URI.create(value);
            return ("http".equals(u.getScheme()) || "https".equals(u.getScheme())) && u.getHost() != null && !u.getHost().isEmpty();
        } catch (IllegalArgumentException e) {
            return false;
        }
    }

    /** Only the host, since the rest of the address is usually a secret. Empty if not set. */
    public String maskedHost() {
        String u = url;
        if (u.isEmpty()) return "";
        try {
            URI parsed = URI.create(u);
            return parsed.getScheme() + "://" + parsed.getHost() + "/…";
        } catch (IllegalArgumentException e) {
            return "set";
        }
    }

    public static boolean isWorthSending(String eventText) {
        for (String prefix : WORTH_SENDING) if (eventText.startsWith(prefix)) return true;
        return false;
    }

    /** Wire this to {@link HistoryService#setEventListener}. */
    public void onEvent(String text) {
        if (!enabled() || !isWorthSending(text)) return;
        long now = clockMillis.getAsLong();
        synchronized (lastSent) {
            Long before = lastSent.get(text);
            if (before != null && now - before < REPEAT_MILLIS) return;
            lastSent.put(text, now);
            if (lastSent.size() > 50) lastSent.clear();
        }
        deliver("WattSync: " + text);
    }

    /** Sends a test message right away; returns an empty string on success, otherwise what went wrong. */
    public String sendTest() {
        if (!enabled()) return "No address set";
        try {
            sender.send(url, body("WattSync: test message, notifications work"));
            return "";
        } catch (Exception e) {
            // "the server answered 404" is already plain; anything else means the address could not be reached at all
            return e.getMessage() != null && e.getMessage().startsWith("the server answered") ? e.getMessage() : "could not reach that address";
        }
    }

    private void deliver(String text) {
        String target = url;
        Thread.ofVirtual().start(() -> {
            try {
                sender.send(target, body(text));
            } catch (Exception e) {
                logger.warn("Could not send the notification: {}", e.getMessage());
            }
        });
    }

    private String body(String text) {
        try {
            return mapper.writeValueAsString(Map.of("text", text, "content", text));
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    private static final HttpClient CLIENT = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(8)).build();

    private static void httpSend(String url, String json) throws Exception {
        HttpRequest request = HttpRequest.newBuilder(URI.create(url)).timeout(Duration.ofSeconds(10))
                .header("Content-Type", "application/json").POST(HttpRequest.BodyPublishers.ofString(json)).build();
        HttpResponse<Void> response = CLIENT.send(request, HttpResponse.BodyHandlers.discarding());
        if (response.statusCode() >= 300) throw new java.io.IOException("the server answered " + response.statusCode());
    }
}
