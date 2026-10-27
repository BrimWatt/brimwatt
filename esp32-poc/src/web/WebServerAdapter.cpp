#include "web/WebServerAdapter.h"

#include "common/Log.h"
#include "config/DeviceConfig.h"

// The dashboard page, embedded in the firmware by PlatformIO (see embed_txtfiles in platformio.ini).
extern const char dashboardStart[] asm("_binary_src_web_dashboard_html_start");
extern const char dashboardEnd[] asm("_binary_src_web_dashboard_html_end");

extern const char setupStart[] asm("_binary_src_web_setup_html_start");
extern const char setupEnd[] asm("_binary_src_web_setup_html_end");

extern const uint8_t faviconStart[] asm("_binary_src_web_favicon_png_start");
extern const uint8_t faviconEnd[] asm("_binary_src_web_favicon_png_end");

namespace device {

namespace {
constexpr uint32_t kMinHeapForHistory = 40 * 1024;   // largest free block needed to build the history reply (about 20 KB of text, built in pieces)
}

void WebServerAdapter::begin() {
    // Every route is registered explicitly. The server logs an error ("request handler not found") for any
    // request that reaches onNotFound, so API calls must not be left to it.
    server_.on("/", HTTP_GET, [this] { serveDashboard(); });
    server_.on("/api/telemetry", HTTP_GET, [this] { serveApi(); });
    server_.on("/api/mode", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/allowance", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/daylight", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/pause", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/rate", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/maxamps", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/goal", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/notify", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/notify/test", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/pair", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/refresh", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/watch", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/test", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/cancel", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/forget", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/through", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/policy", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/wake", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/car/allowwake", HTTP_POST, [this] { serveApi(); });
    server_.on("/api/history", HTTP_GET, [this] { serveApi(); });
    server_.on("/api/days", HTTP_GET, [this] { serveApi(); });
    server_.on("/api/sessions", HTTP_GET, [this] { serveApi(); });
    server_.on("/api/events", HTTP_GET, [this] { serveApi(); });
    server_.on("/api/diagnostics", HTTP_GET, [this] { serveApi(); });
    server_.on("/api/export.csv", HTTP_GET, [this] { serveApi(); });
    server_.on("/setup", HTTP_GET, [this] { serveSetupPage(); });
    server_.on("/api/setup/status", HTTP_GET, [this] { serveSetupApi(); });
    server_.on("/api/setup/networks", HTTP_GET, [this] { serveSetupApi(); });
    server_.on("/api/setup/scan", HTTP_POST, [this] { serveSetupApi(); });
    server_.on("/api/setup/save", HTTP_POST, [this] { serveSetupApi(); });
    server_.on("/favicon.ico", HTTP_GET, [this] { serveFavicon(); });   // browsers ask for it on every visit
    server_.onNotFound([this] { serveApi(); });                            // answers 404 for anything else
    static const char* kHeaders[] = {"X-WattSync"};
    server_.collectHeaders(kHeaders, 1);   // so changing requests can be checked for the safe header
    server_.begin();
    LOG_INFO("Dashboard: http://%s.local/ (port 80)", kHostname);
}

void WebServerAdapter::serveDashboard() {
    if (redirectToSetup()) return;
    // The embedded text ends with a terminating zero byte that must not be sent.
    size_t length = static_cast<size_t>(dashboardEnd - dashboardStart);
    if (length > 0 && dashboardStart[length - 1] == '\0') length--;
    server_.sendHeader("Cache-Control", "no-cache");
    server_.send_P(200, "text/html; charset=utf-8", dashboardStart, length);
}

void WebServerAdapter::serveFavicon() {
    server_.sendHeader("Cache-Control", "public, max-age=86400");
    server_.send_P(200, "image/png", reinterpret_cast<const char*>(faviconStart), static_cast<size_t>(faviconEnd - faviconStart));
}

bool WebServerAdapter::redirectToSetup() {
    if (!portal_.isActive() || server_.method() != HTTP_GET) return false;
    if (!SetupPortal::isPortalClient(server_.client().remoteIP())) return false;   // people on the home network see the dashboard as usual
    server_.sendHeader("Location", String("http://") + solar::setup::kPortalAddress + "/setup", true);
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(302, "text/plain", "");
    return true;
}

solar::ApiHandler::Params WebServerAdapter::readParams() {
    // Query values and form fields both arrive as arguments. Secrets are sent in the body of a POST, never in the address.
    solar::ApiHandler::Params params;
    for (int i = 0; i < server_.args(); i++) {
        params[server_.argName(i).c_str()] = server_.arg(i).c_str();
    }
    return params;
}

void WebServerAdapter::serveSetupPage() {
    if (!portal_.isActive()) {
        server_.send(403, "text/plain", "Setup is off. Hold the BOOT button on the board for 5 seconds to turn it on.");
        return;
    }
    portal_.userActive();
    size_t length = static_cast<size_t>(setupEnd - setupStart);
    if (length > 0 && setupStart[length - 1] == '\0') length--;
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html; charset=utf-8", setupStart, length);
}

void WebServerAdapter::serveSetupApi() {
    std::string method = server_.method() == HTTP_GET ? "GET" : server_.method() == HTTP_POST ? "POST" : "OTHER";
    solar::HttpResponse response = setupApi_.handle(method, server_.uri().c_str(), readParams(), server_.hasHeader("X-WattSync"));
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(response.status, response.contentType.c_str(), response.body.c_str());
}

void WebServerAdapter::serveApi() {
    // The setup network's address probes (phones ask for things like /generate_204 and /hotspot-detect.html) land here.
    if (redirectToSetup()) return;
    // The history reply is built in memory (tens of KB). If the largest free block is small, say so instead of
    // risking the allocation: the dashboard simply tries again on its next refresh.
    if (server_.uri() == "/api/history" && ESP.getMaxAllocHeap() < kMinHeapForHistory) {
        LOG_WARN("Dashboard: skipped the history reply, largest free block is only %u KB.", (unsigned)(ESP.getMaxAllocHeap() / 1024));
        server_.send(503, "text/plain", "Low memory, try again shortly");
        return;
    }
    std::string method = server_.method() == HTTP_GET ? "GET" : server_.method() == HTTP_POST ? "POST" : "OTHER";
    solar::HttpResponse response = api_.handle(method, server_.uri().c_str(), readParams(), server_.hasHeader("X-WattSync"));
    server_.sendHeader("Cache-Control", "no-store");
    if (!response.downloadName.empty()) {
        server_.sendHeader("Content-Disposition", String("attachment; filename=\"") + response.downloadName.c_str() + "\"");
    }
    server_.send(response.status, response.contentType.c_str(), response.body.c_str());
}

}  // namespace device
