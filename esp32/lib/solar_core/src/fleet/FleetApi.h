#pragma once

#include <string>
#include <vector>

#include "fleet/FleetAuth.h"
#include "fleet/FleetHttp.h"
#include "tesla/TeslaMessages.h"

namespace solar {
namespace fleet {

/** The result of reading the car's charge state through the Fleet API. */
struct FleetRead {
    bool ok = false;          // the car answered with its charge state
    bool asleep = false;      // Tesla could not reach the car because it sleeps or is offline (a real answer, not a fault)
    std::string error;        // plain words when not ok (an asleep car has its own words here too)
    tesla::ChargeState charge;
};

/** The few Fleet API calls WattSync makes. Every call asks FleetAuth for a token, retries once with a new one after a 401, and turns Tesla's codes into plain words. */
class FleetApi {
public:
    FleetApi(FleetHttp& http, FleetAuth& auth) : http_(http), auth_(auth) {}

    /** GET vehicle_data (charge state only). Does NOT wake the car: a sleeping car answers 408, which becomes asleep = true. */
    FleetRead readCharge(const std::string& vin);
    /** GET vehicles: the VINs on the signed-in account. Used once after sign-in to find the car. */
    bool listVehicles(std::vector<std::string>& vins, std::string& error);
    /** POST wake_up. True when Tesla accepted the request (the car may need seconds to wake). */
    bool wake(const std::string& vin, std::string& error);
    /** POST signed_command with one routable message (the same protobuf the Bluetooth link sends, without the length prefix). `reply` is the car's routable answer. */
    bool signedCommand(const std::string& vin, const tesla::Bytes& routable, tesla::Bytes& reply, std::string& error);

    /** Maps Tesla's charging_state words to the codes the rest of WattSync uses (2 disconnected ... 8 calibrating, 1 unknown). */
    static int chargingStateCode(const std::string& text);
    static bool parseChargeState(const std::string& json, tesla::ChargeState& out);

private:
    /** Sends with a token; on 401 refreshes once and tries again. status < 0: nothing was sent. */
    FleetHttpResponse call(const std::string& method, const std::string& path, const std::string& body, std::string& error);
    static std::string describeStatus(int status);

    FleetHttp& http_;
    FleetAuth& auth_;
};

}  // namespace fleet
}  // namespace solar
