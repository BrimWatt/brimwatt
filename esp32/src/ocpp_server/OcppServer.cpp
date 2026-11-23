#include "ocpp_server/OcppServer.h"

#include "common/Config.h"
#include "common/Log.h"
#include "ocpp/ChargerId.h"

namespace device {

using namespace solar;

OcppServer::OcppServer(Platform& platform, ChargerRegistry& chargers, ocpp::OcppRouter& router,
                       WallboxController& wallbox, Scheduler& scheduler, ChargerPersistence& persistence)
    : platform_(platform), chargers_(chargers), router_(router), wallbox_(wallbox), scheduler_(scheduler),
      persistence_(persistence),
      // The library answers the handshake with this subprotocol, which the wallbox requires.
      server_(config::kOcppPort, "", config::kOcppSubprotocol) {}

void OcppServer::begin() {
    server_.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        onEvent(num, type, payload, length);
    });
    server_.begin();
    LOG_INFO("OCPP: listening on port %u (subprotocol %s)", (unsigned)config::kOcppPort, config::kOcppSubprotocol);
}

void OcppServer::loop() {
    server_.loop();

    uint64_t now = platform_.nowMillis();
    if (now - lastIdleCheck_ < 10000) return;
    lastIdleCheck_ = now;
    // A charger beats every 60 s. Silence for five minutes means the connection is dead (power cut, Wi-Fi gone),
    // and closing it lets the wallbox reconnect cleanly.
    for (int key : chargers_.silentFor(now, config::kChargerIdleTimeoutMillis)) {
        LOG_WARN("OCPP: connection %d has been silent for %lu s; closing it.", key,
                 (unsigned long)(config::kChargerIdleTimeoutMillis / 1000));
        server_.disconnect(static_cast<uint8_t>(key));
    }
}

void OcppServer::onEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            onConnected(num, std::string(reinterpret_cast<char*>(payload), length));
            break;
        case WStype_DISCONNECTED:
            onDisconnected(num);
            break;
        case WStype_TEXT:
            if (Charger* charger = chargers_.find(num)) {
                router_.onMessage(*charger, std::string(reinterpret_cast<char*>(payload), length));
            }
            break;
        case WStype_ERROR:
            LOG_ERROR("OCPP: WebSocket fault on connection %u", (unsigned)num);
            break;
        default:
            break;   // pings, pongs and binary frames need nothing from us
    }
}

void OcppServer::onConnected(uint8_t num, const std::string& url) {
    std::string id = ocpp::chargerIdFromUrl(url);
    links_[num] = std::make_unique<Link>(server_, num);

    auto replaced = chargers_.add(num, id, *links_[num], platform_.nowMillis());
    if (replaced) {
        LOG_WARN("OCPP: charger %s reconnected; closing its old connection %d.", id.c_str(), *replaced);
        server_.disconnect(static_cast<uint8_t>(*replaced));
    }
    LOG_INFO("Wallbox connected -> ID: %s (%s). Total active sessions: %u", id.c_str(), url.c_str(),
             (unsigned)chargers_.size());

    if (history_) history_->addEvent("Charger connected");

    // After a restart or a dropped connection we know nothing live, so start from the last saved state and then
    // ask the charger what it is really doing.
    if (Charger* charger = chargers_.find(num)) {
        if (auto saved = persistence_.recall(id)) {
            charger->restoreFrom(*saved);
            LOG_INFO("Restored last known state of charger %s: %s (transaction %s, limit %s); verifying.", id.c_str(),
                     toString(saved->status), saved->transactionId ? std::to_string(*saved->transactionId).c_str() : "none",
                     saved->appliedAmps ? (std::to_string(*saved->appliedAmps) + " A").c_str() : "none");
        }
    }

    // We know nothing live about a charger that has just connected, so ask what it is really doing.
    resyncLater(num, config::kResyncFirstMillis);
    resyncLater(num, config::kResyncSecondMillis);
}

void OcppServer::onDisconnected(uint8_t num) {
    // Capture its state before it disappears, then drop it (the core's Charger holds a reference to the link).
    if (Charger* charger = chargers_.find(num)) {
        persistence_.onDisconnect(*charger);
        if (history_) history_->addEvent("Charger disconnected");
    }
    chargers_.remove(num);
    links_.erase(num);
    LOG_INFO("Wallbox disconnected. Remaining active sessions: %u", (unsigned)chargers_.size());
}

void OcppServer::resyncLater(uint8_t num, uint64_t delayMillis) {
    Charger* original = chargers_.find(num);
    if (!original) return;
    std::string id = original->id();
    scheduler_.after(delayMillis, [this, num, id] {
        Charger* charger = chargers_.find(num);
        // The connection may have gone, or been replaced by another charger, since this was scheduled.
        if (!charger || charger->id() != id) return;
        if (charger->isRestored() || charger->status() == ChargerStatus::Unknown) wallbox_.requestStatus(*charger);
    });
}

}  // namespace device
