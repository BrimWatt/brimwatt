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
    r.payload = vcsecAddKey(pub_, role_, formFactor_);
    r.uuid = c_.randomBytes(16);
    state_ = Ready;
    text_ = "Request sent. Tap your key card on the centre console now.";
    return encodeRoutable(r);
}

Bytes PairFlow::onMessage(const Bytes& routable) {
    if (state_ == Accepted || state_ == Refused) return Bytes();
    Routable r;
    if (!decodeRoutable(routable, r)) return Bytes();
    if (r.hasStatus && r.statusFault != 0) {
        lastFault_ = r.statusFault;
        if (r.statusFault == 10 && !retried_) {       // "could not read the message": try once as a phone key
            retried_ = true;
            formFactor_ = kFormFactorAndroidDevice;
            Bytes again = request();
            text_ = "The car could not read the first request; trying again as a phone key. Tap your key card when asked.";
            return again;
        }
        state_ = Refused;
        text_ = std::string("The car refused: ") + messageFaultText(r.statusFault) + " (fault " + std::to_string(r.statusFault) + ").";
        return Bytes();
    }
    if (!r.hasPayload) return Bytes();
    VcsecReply v;
    if (!decodeVcsecReply(r.payload, v) || !v.isCommandStatus) return Bytes();
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
    return Bytes();
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

void describeCharge(const ChargeState& s, std::vector<std::string>& lines) {
    lines.push_back(std::string("Charging: ") + chargingStateText(s.chargingState) + ".");
    if (s.hasBatteryLevel) lines.push_back("Battery: " + std::to_string(s.batteryLevel) + " %" + (s.hasChargeLimit ? ", charge limit " + std::to_string(s.chargeLimit) + " %." : "."));
    if (s.hasActualCurrent || s.hasCurrentRequest) {
        std::string c = "Current:";
        if (s.hasActualCurrent) c += " drawing " + std::to_string(s.actualCurrent) + " A";
        if (s.hasCurrentRequest) c += (s.hasActualCurrent ? "," : "") + std::string(" set to ") + std::to_string(s.currentRequest) + " A";
        if (s.hasCurrentRequestMax) c += " (up to " + std::to_string(s.currentRequestMax) + " A)";
        if (s.hasVoltage) c += " at " + std::to_string(s.voltage) + " V";
        lines.push_back(c + ".");
    }
    if (s.hasPower) lines.push_back("Charger power: " + std::to_string(s.power) + " kW.");
}

void StatusFlow::addCharge(const ChargeState& s) {
    charge_ = s;
    describeCharge(s, lines_);
}

Bytes StatusFlow::onMessage(const Bytes& routable, uint32_t nowMs) {
    if (stage_ == Done) return Bytes();
    Routable r;
    if (!decodeRoutable(routable, r)) return Bytes();
    if (r.toRouting != routing_) return Bytes();   // the car also broadcasts to everyone: only what is addressed to us counts

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

// ---- charge commands --------------------------------------------------------------------------------------------------------

Bytes ChargeCommandFlow::envelope(int domain, const Bytes& payload) {
    Routable r;
    r.toDomain = domain;
    r.fromRouting = routing_;
    r.hasPayload = true;
    r.payload = payload;
    r.uuid = c_.randomBytes(16);
    return encodeRoutable(r);
}

void ChargeCommandFlow::fail(const std::string& why) {
    lines_.push_back(why);
    ok_ = false;
    stage_ = Done;
}

std::string ChargeCommandFlow::doneText() const {
    switch (command_) {
        case Start: return "Charging started.";
        case Stop: return "Charging stopped.";
        default: return "Charging current set to " + std::to_string(amps_) + " A.";
    }
}

Bytes ChargeCommandFlow::start() {
    lines_.clear();
    ok_ = false;
    if (command_ == SetAmps && (amps_ < kMinAmps || amps_ > kMaxAmps)) {
        fail("The current must be between " + std::to_string(kMinAmps) + " and " + std::to_string(kMaxAmps) + " A. Nothing was sent.");
        return Bytes();
    }
    routing_ = c_.randomBytes(16);
    stage_ = AwaitStatus;
    return envelope(kDomainVehicleSecurity, vcsecGetStatus());
}

Bytes ChargeCommandFlow::sendCar(const Bytes& payload, uint32_t nowMs) {
    Routable cmd = session_.command(payload, kFlagEncryptResponse, 10, nowMs, requestTag_);
    cmd.fromRouting = routing_;
    return encodeRoutable(cmd);
}

void ChargeCommandFlow::timedOut() {
    if (stage_ == Done) return;
    switch (stage_) {
        case AwaitStatus: fail("The car did not answer. Is it in range? Nothing was changed."); break;
        case AwaitSession: fail("The car's main computer did not answer (it may be falling asleep). Nothing was changed."); break;
        case AwaitCommand: fail("The car did not answer after the command: it may or may not have been applied. Check with: tesla status."); break;
        default:
            lines_.push_back("Could not read the charge state back. Check with: tesla status.");
            stage_ = Done;
            break;
    }
}

Bytes ChargeCommandFlow::onMessage(const Bytes& routable, uint32_t nowMs) {
    if (stage_ == Done) return Bytes();
    Routable r;
    if (!decodeRoutable(routable, r)) return Bytes();
    if (r.toRouting != routing_) return Bytes();   // the car also broadcasts to everyone: only what is addressed to us counts

    if (stage_ == AwaitStatus) {
        if (r.hasStatus && r.statusFault != 0) {
            fail(std::string("The car refused: ") + messageFaultText(r.statusFault) + ". Nothing was changed.");
            return Bytes();
        }
        VcsecReply v;
        if (!r.hasPayload || !decodeVcsecReply(r.payload, v) || !v.isVehicleStatus) return Bytes();
        if (v.sleepStatus == 2) {
            fail("The car is asleep, so the command was not sent. Waking it is not done here; wake it from the Tesla app and try again.");
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
            fail(session_.keyUnknown() ? "The car does not have our key. Pair again (tesla pair). Nothing was changed."
                                       : "Could not start a session: " + error + ". Nothing was changed.");
            return Bytes();
        }
        stage_ = AwaitCommand;
        Bytes payload = command_ == SetAmps ? carSetChargingAmps(amps_) : carChargingStartStop(command_ == Start);
        return sendCar(payload, nowMs);
    }

    // AwaitCommand and AwaitVerify both get an encrypted reply.
    if (r.hasStatus && r.statusFault != 0) {
        if (stage_ == AwaitCommand) fail(std::string("The car refused the command: ") + messageFaultText(r.statusFault) + ".");
        else {
            lines_.push_back("Could not read the charge state back. Check with: tesla status.");
            stage_ = Done;
        }
        return Bytes();
    }
    if (!r.hasPayload) return Bytes();
    Bytes plain;
    std::string error;
    if (!session_.openReply(r, requestTag_, plain, error)) {
        if (stage_ == AwaitCommand) fail("Could not read the car's reply (" + error + "): the command may or may not have been applied. Check with: tesla status.");
        else {
            lines_.push_back("Could not read the charge state back. Check with: tesla status.");
            stage_ = Done;
        }
        return Bytes();
    }
    CarReply reply;
    bool parsed = decodeCarReply(plain, reply);

    if (stage_ == AwaitCommand) {
        if (!parsed) {
            fail("The car's reply could not be understood: the command may or may not have been applied. Check with: tesla status.");
            return Bytes();
        }
        if (!reply.ok) {
            fail("The car said no: " + (reply.reasonText.empty() ? std::string("no reason given") : reply.reasonText) + ".");
            return Bytes();
        }
        ok_ = true;
        lines_.push_back(doneText());
        stage_ = AwaitVerify;
        return sendCar(carGetChargeState(), nowMs);
    }

    // AwaitVerify
    if (parsed && reply.ok && reply.charge.valid) {
        charge_ = reply.charge;
        describeCharge(reply.charge, lines_);
    } else {
        lines_.push_back("Could not read the charge state back. Check with: tesla status.");
    }
    stage_ = Done;
    return Bytes();
}

}  // namespace tesla
}  // namespace solar
