#pragma once

#include <string>
#include <utility>
#include <vector>

namespace solar {
namespace fleet {

/** One HTTP request to send to Tesla. */
struct FleetHttpRequest {
    std::string method;   // "GET" or "POST"
    std::string url;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

/** What came back from Tesla; `sent` is false when no connection could be made at all. */
struct FleetHttpResponse {
    bool sent = false;    // false: no connection (no Wi-Fi, no internet, TLS failed, timed out)
    int status = 0;
    std::string body;
};

/** The one thing the Fleet client needs from the network: send a request, wait for the answer. The firmware does it over HTTPS; the tests fake it. */
class FleetHttp {
public:
    virtual ~FleetHttp() = default;
    virtual FleetHttpResponse send(const FleetHttpRequest& request) = 0;
};

}  // namespace fleet
}  // namespace solar
