#pragma once

#include <WebServer.h>

#include "web/ApiHandler.h"

namespace device {

/**
 * The dashboard's HTTP server (port 80): serves the embedded page at / and hands the /api/ endpoints to the
 * portable ApiHandler. It runs in the main loop, so it never touches the controller from another thread.
 */
class WebServerAdapter {
public:
    explicit WebServerAdapter(solar::ApiHandler& api) : api_(api), server_(80) {}

    void begin();

    /** Call from the main loop. */
    void loop() { server_.handleClient(); }

private:
    void serveDashboard();
    void serveFavicon();
    void serveApi();

    solar::ApiHandler& api_;
    WebServer server_;
};

}  // namespace device
