#include "net/EspFleetHttp.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "common/Log.h"
#include "net/FleetRoots.h"

namespace device {

solar::fleet::FleetHttpResponse EspFleetHttp::send(const solar::fleet::FleetHttpRequest& request) {
    solar::fleet::FleetHttpResponse out;
    if (WiFi.status() != WL_CONNECTED) return out;
    WiFiClientSecure client;
    client.setCACert(kFleetRootCertificates);
    client.setTimeout(12);   // seconds
    HTTPClient http;
    http.setTimeout(12000);
    http.setConnectTimeout(8000);
    http.setReuse(false);
    if (!http.begin(client, request.url.c_str())) return out;
    for (const auto& header : request.headers) http.addHeader(header.first.c_str(), header.second.c_str());
    std::string body = request.body;   // the library wants a writable buffer
    const int code = http.sendRequest(request.method.c_str(), reinterpret_cast<uint8_t*>(&body[0]), body.size());
    if (code <= 0) {
        // Never logs the URL's query or any header: only the library's own reason.
        LOG_WARN("Tesla: request failed (%s)", HTTPClient::errorToString(code).c_str());
        http.end();
        return out;
    }
    out.sent = true;
    out.status = code;
    out.body = http.getString().c_str();
    http.end();
    return out;
}

}  // namespace device
