package com.wattsync.wallbox;

import io.javalin.websocket.WsContext;

import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiConsumer;

/** Tracks the chargers that are currently connected. */
public class WallboxSessionRegistry {

    private final Map<String, Charger> chargers = new ConcurrentHashMap<>();

    public Charger register(String chargerId, WsContext ctx) {
        Charger charger = new Charger(chargerId, ctx);
        chargers.put(chargerId, charger);
        return charger;
    }

    public Optional<Charger> find(WsContext ctx) {
        return chargers.values().stream().filter(c -> c.ctx().equals(ctx)).findFirst();
    }

    public void unregister(WsContext ctx) { chargers.values().removeIf(c -> c.ctx().equals(ctx)); }

    public boolean isEmpty() { return chargers.isEmpty(); }

    public int size() { return chargers.size(); }

    public void forEach(BiConsumer<String, Charger> action) { chargers.forEach(action); }
}
