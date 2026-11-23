#pragma once

#include "fleet/FleetHttp.h"

namespace device {

/**
 * HTTPS to Tesla's Fleet hosts. The server's certificate is CHECKED against the two DigiCert roots in FleetRoots.h (the sign-in token is a
 * secret), which needs the board's clock to be right: before the first time sync every call fails as "could not reach Tesla".
 * One blocking call, about a second; run it from a job task, never from the main loop.
 */
class EspFleetHttp : public solar::fleet::FleetHttp {
public:
    solar::fleet::FleetHttpResponse send(const solar::fleet::FleetHttpRequest& request) override;
};

}  // namespace device
