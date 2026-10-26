#include "tesla/TeslaFlows.h"

namespace solar {
namespace tesla {

// ---- pairing ----------------------------------------------------------------------------------------------------------------

Bytes PairFlow::request() {
    routing_ = c_.randomBytes(16);
    Routable r;
    r.toDomain = kDomainVehicleSecurity;
    r.fromRouting = routing_;
    r.hasPayload = true;
    r.payload = vcsecAddKey(pub_, role_, kFormFactorCloudKey);
    r.uuid = c_.randomBytes(16);
    state_ = Ready;
    text_ = "Request sent. Tap your key card on the centre console now.";
    return encodeRoutable(r);
}

void PairFlow::onMessage(const Bytes& routable) {
    if (state_ == Accepted || state_ == Refused) return;
    Routable r;
    if (!decodeRoutable(routable, r)) return;
    if (r.hasStatus && r.statusFault != 0) {
        state_ = Refused;
        text_ = std::string("The car refused: ") + messageFaultText(r.statusFault) + ".";
        return;
    }
    if (!r.hasPayload) return;
    VcsecReply v;
    if (!decodeVcsecReply(r.payload, v) || !v.isCommandStatus) return;
    if (v.hasWhitelistStatus && v.whitelistInfo != 0) {
        state_ = Refused;
        alreadyOnCar_ = v.whitelistInfo == 13;
        text_ = "The car refused: " + whitelistInfoText(v.whitelistInfo) + ".";
    } else if (v.operationStatus == 1) {
        state_ = Waiting;
        text_ = "The car is waiting for your key card: tap it on the centre console, then confirm on the screen if asked.";
    } else if (v.operationStatus == 2) {
        state_ = Refused;
        text_ = "The car refused, without saying why.";
    } else if (v.hasWhitelistStatus) {
        state_ = Accepted;
        text_ = "The car accepted our key.";
    }
}

// ---- status -----------------------------------------------------------------------------------------------------------------

Bytes StatusFlow::envelope(int domain, const Bytes& payload) {
    Routable r;
    r.toDomain = domain;
    r.fromRouting = routing_;
    r.hasPayload = true;
    r.payload = payload;
    r.uuid = c_.randomBytes(16);
    return encodeRoutable(r);
}

Bytes StatusFlow::start() {
    routing_ = c_.randomBytes(16);
    stage_ = AwaitStatus;
    ok_ = false;
    lines_.clear();
    return envelope(kDomainVehicleSecurity, vcsecGetStatus());
}

void StatusFlow::fail(const std::string& why) {
    lines_.push_back(why);
    ok_ = false;
    stage_ = Done;
}

void StatusFlow::timedOut() {
    if (stage_ == Done) return;
    fail(stage_ == AwaitStatus ? "The car did not answer. Is it in range?" : stage_ == AwaitSession ? "The car's main computer did not answer (it may be falling asleep)."
                                                                                                  : "The car did not answer the charge-state request.");
}

void StatusFlow::addCharge(const ChargeState& s) {
    charge_ = s;
    std::string a = std::string("Charging: ") + chargingStateText(s.chargingState) + ".";
    lines_.push_back(a);
    if (s.hasBatteryLevel) lines_.push_back("Battery: " + std::to_string(s.batteryLevel) + " %" + (s.hasChargeLimit ? ", charge limit " + std::to_string(s.chargeLimit) + " %." : "."));
    if (s.hasActualCurrent || s.hasCurrentRequest) {
        std::string c = "Current:";
        if (s.hasActualCurrent) c += " drawing " + std::to_string(s.actualCurrent) + " A";
        if (s.hasCurrentRequest) c += (s.hasActualCurrent ? "," : "") + std::string(" set to ") + std::to_string(s.currentRequest) + " A";
        if (s.hasCurrentRequestMax) c += " (up to " + std::to_string(s.currentRequestMax) + " A)";
        if (s.hasVoltage) c += " at " + std::to_string(s.voltage) + " V";
        lines_.push_back(c + ".");
    }
    if (s.hasPower) lines_.push_back("Charger power: " + std::to_string(s.power) + " kW.");
}

Bytes StatusFlow::onMessage(const Bytes& routable, uint32_t nowMs) {
    if (stage_ == Done) return Bytes();
    Routable r;
    if (!decodeRoutable(routable, r)) return Bytes();

    if (stage_ == AwaitStatus) {
        if (r.hasStatus && r.statusFault != 0) {
            fail(std::string("The car refused: ") + messageFaultText(r.statusFault) + ".");
            return Bytes();
        }
        VcsecReply v;
        if (!r.hasPayload || !decodeVcsecReply(r.payload, v) || !v.isVehicleStatus) return Bytes();
        sleep_ = v.sleepStatus;
        lines_.push_back(std::string("Car is ") + sleepText(v.sleepStatus) + ", " + lockText(v.lockState) + (v.userPresence == 2 ? ", someone is nearby." : "."));
        if (v.sleepStatus == 2) {
            lines_.push_back("The car is asleep, so its charge state is not available. Waking it is a command, so it is not done here.");
            ok_ = true;
            stage_ = Done;
            return Bytes();
        }
        stage_ = AwaitSession;
        Routable h = session_.handshakeRequest();
        h.fromRouting = routing_;
        return encodeRoutable(h);
    }

    if (stage_ == AwaitSession) {
        if (r.sessionInfo.empty() && !(r.hasStatus && r.statusFault != 0)) return Bytes();
        std::string error;
        if (!session_.acceptSessionInfo(r, nowMs, error)) {
            fail(session_.keyUnknown() ? "The car does not have our key. Pair again (tesla pair)." : "Could not start a session: " + error + ".");
            return Bytes();
        }
        Routable cmd = session_.command(carGetChargeState(), kFlagEncryptResponse, 10, nowMs, requestTag_);
        cmd.fromRouting = routing_;
        stage_ = AwaitReply;
        return encodeRoutable(cmd);
    }

    // AwaitReply
    if (r.hasStatus && r.statusFault != 0) {
        fail(std::string("The car refused the request: ") + messageFaultText(r.statusFault) + ".");
        return Bytes();
    }
    if (!r.hasPayload) return Bytes();
    Bytes plain;
    std::string error;
    if (!session_.openReply(r, requestTag_, plain, error)) {
        fail("Could not read the car's reply: " + error + ".");
        return Bytes();
    }
    CarReply reply;
    if (!decodeCarReply(plain, reply)) {
        fail("The car's reply could not be understood.");
        return Bytes();
    }
    if (!reply.ok) {
        fail("The car said no: " + (reply.reasonText.empty() ? std::string("no reason given") : reply.reasonText) + ".");
        return Bytes();
    }
    if (!reply.charge.valid) {
        fail("The car answered without charge information.");
        return Bytes();
    }
    addCharge(reply.charge);
    ok_ = true;
    stage_ = Done;
    return Bytes();
}

}  // namespace tesla
}  // namespace solar
