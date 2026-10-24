#pragma once

#include <WebServer.h>

#include "net/SetupPortal.h"
#include "setup/SetupApi.h"
#include "web/ApiHandler.h"

namespace device {

/**
 * The dashboard's HTTP server (port 80): serves the embedded page at / and hands the /api/ endpoints to the
 * portable ApiHandler. It also serves the setup page (/setup and /api/setup/..., only while the setup network is
 * open) and, while it is open, leads phones joined to it to that page. It runs in the main loop, so it never
 * touches the controller from another thread.
 */
class WebServerAdapter {
public:
    WebServerAdapter(solar::ApiHandler& api, solar::setup::SetupApi& setupApi, SetupPortal& portal)
        : api_(api), setupApi_(setupApi), portal_(portal), server_(80) {}

    void begin();

    /** Call from the main loop. */
    void loop() { server_.handleClient(); }

private:
    void serveDashboard();
    void serveFavicon();
    void serveApi();
    void serveSetupPage();
    void serveSetupApi();
    /** While the setup network is open, a phone joined to it gets the setup page for every address it tries. */
    bool redirectToSetup();
    solar::ApiHandler::Params readParams();

    solar::ApiHandler& api_;
    solar::setup::SetupApi& setupApi_;
    SetupPortal& portal_;
    WebServer server_;
};

}  // namespace device
