#pragma once

#include <WebSocketsServer.h>

#include <map>
#include <memory>

#include "common/Platform.h"
#include "common/Scheduler.h"
#include "ocpp/OcppRouter.h"
#include "state/ChargerPersistence.h"
#include "wallbox/ChargerRegistry.h"
#include "wallbox/WallboxController.h"

namespace device {

/**
 * The WebSocket endpoint the wallbox connects to: ws://<board>:8080/ocpp/wallbox/<chargerId>.
 * This is only the transport. Reading frames, replying and tracking the charger are done by the portable core.
 */
class OcppServer {
public:
    OcppServer(solar::Platform& platform, solar::ChargerRegistry& chargers, solar::ocpp::OcppRouter& router,
               solar::WallboxController& wallbox, solar::Scheduler& scheduler,
               solar::ChargerPersistence& persistence);

    void begin();

    /** Where to log the charger connecting and disconnecting. Optional. */
    void setHistory(solar::HistoryService* history) { history_ = history; }

    /** Call from the main loop: services the sockets and drops chargers that have gone silent. */
    void loop();

private:
    /** One open WebSocket connection, seen by the core as a ChargerLink. */
    class Link : public solar::ChargerLink {
    public:
        Link(WebSocketsServer& server, uint8_t num) : server_(server), num_(num) {}
        bool isOpen() const override { return server_.clientIsConnected(num_); }
        void send(const std::string& frame) override { server_.sendTXT(num_, frame.c_str(), frame.length()); }

    private:
        WebSocketsServer& server_;
        uint8_t num_;
    };

    void onEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void onConnected(uint8_t num, const std::string& url);
    void onDisconnected(uint8_t num);
    void resyncLater(uint8_t num, uint64_t delayMillis);

    solar::Platform& platform_;
    solar::ChargerRegistry& chargers_;
    solar::ocpp::OcppRouter& router_;
    solar::WallboxController& wallbox_;
    solar::Scheduler& scheduler_;
    solar::ChargerPersistence& persistence_;
    solar::HistoryService* history_ = nullptr;
    WebSocketsServer server_;
    std::map<uint8_t, std::unique_ptr<Link>> links_;
    uint64_t lastIdleCheck_ = 0;
};

}  // namespace device
