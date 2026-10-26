#include "tesla/TeslaMessages.h"

#include "tesla/TeslaBle.h"

namespace solar {
namespace tesla {

Bytes encodeRoutable(const Routable& r) {
    Writer w;
    if (r.toDomain >= 0 || !r.toRouting.empty()) {
        Writer d;
        if (r.toDomain >= 0) d.varint(1, static_cast<uint64_t>(r.toDomain)); else d.bytes(2, r.toRouting);
        w.message(6, d);
    }
    if (!r.fromRouting.empty()) {
        Writer d;
        d.bytes(2, r.fromRouting);
        w.message(7, d);
    }
    if (r.hasPayload) w.bytes(10, r.payload);
    if (!r.signerPublicKey.empty() || r.hasGcm || r.hasGcmResponse || !r.sessionInfoTag.empty()) {
        Writer sig;
        if (!r.signerPublicKey.empty()) {
            Writer id;
            id.bytes(1, r.signerPublicKey);
            sig.message(1, id);
        }
        if (r.hasGcm) {
            Writer g;
            g.bytes(1, r.gcmEpoch);
            g.bytes(2, r.gcmNonce);
            g.varint(3, r.gcmCounter);
            g.fixed32(4, r.gcmExpiresAt);
            g.bytes(5, r.gcmTag);
            sig.message(5, g);
        }
        if (!r.sessionInfoTag.empty()) {
            Writer t;
            t.bytes(1, r.sessionInfoTag);
            sig.message(6, t);
        }
        if (r.hasGcmResponse) {
            Writer g;
            g.bytes(1, r.respNonce);
            g.varint(2, r.respCounter);
            g.bytes(3, r.respTag);
            sig.message(9, g);
        }
        if (!sig.data().empty()) w.message(13, sig);
    }
    if (r.hasSessionInfoRequest) {
        Writer q;
        q.bytes(1, r.requestPublicKey);
        q.bytes(2, r.requestChallenge);
        w.message(14, q);
    }
    if (!r.sessionInfo.empty()) w.bytes(15, r.sessionInfo);
    if (!r.requestUuid.empty()) w.bytes(50, r.requestUuid);
    if (!r.uuid.empty()) w.bytes(51, r.uuid);
    if (r.flags) w.varint(52, r.flags);
    return w.data();
}

bool decodeRoutable(const Bytes& bytes, Routable& r) {
    r = Routable();
    Reader in(bytes);
    Field f;
    while (in.next(f)) {
        switch (f.number) {
            case 6: {
                Reader d(f.data, f.len);
                Field g;
                while (d.next(g)) {
                    if (g.number == 1 && g.wire == 0) r.toDomain = static_cast<int>(g.value);
                    if (g.number == 2 && g.wire == 2) r.toRouting = g.copy();
                }
                break;
            }
            case 7: {
                Reader d(f.data, f.len);
                Field g;
                while (d.next(g)) if (g.number == 2 && g.wire == 2) r.fromRouting = g.copy();
                break;
            }
            case 10: if (f.wire == 2) { r.hasPayload = true; r.payload = f.copy(); } break;
            case 12: {
                r.hasStatus = true;
                Reader d(f.data, f.len);
                Field g;
                while (d.next(g)) {
                    if (g.number == 1) r.statusOperation = static_cast<int>(g.value);
                    if (g.number == 2) r.statusFault = static_cast<int>(g.value);
                }
                break;
            }
            case 13: {
                Reader d(f.data, f.len);
                Field g;
                while (d.next(g)) {
                    if (g.wire != 2) continue;
                    Reader s(g.data, g.len);
                    Field h;
                    if (g.number == 1) {
                        while (s.next(h)) if (h.number == 1 && h.wire == 2) r.signerPublicKey = h.copy();
                    } else if (g.number == 5) {
                        r.hasGcm = true;
                        while (s.next(h)) {
                            if (h.number == 1) r.gcmEpoch = h.copy();
                            if (h.number == 2) r.gcmNonce = h.copy();
                            if (h.number == 3) r.gcmCounter = static_cast<uint32_t>(h.value);
                            if (h.number == 4) r.gcmExpiresAt = static_cast<uint32_t>(h.value);
                            if (h.number == 5) r.gcmTag = h.copy();
                        }
                    } else if (g.number == 6) {
                        while (s.next(h)) if (h.number == 1 && h.wire == 2) r.sessionInfoTag = h.copy();
                    } else if (g.number == 9) {
                        r.hasGcmResponse = true;
                        while (s.next(h)) {
                            if (h.number == 1) r.respNonce = h.copy();
                            if (h.number == 2) r.respCounter = static_cast<uint32_t>(h.value);
                            if (h.number == 3) r.respTag = h.copy();
                        }
                    }
                }
                break;
            }
            case 14: {
                r.hasSessionInfoRequest = true;
                Reader d(f.data, f.len);
                Field g;
                while (d.next(g)) {
                    if (g.number == 1) r.requestPublicKey = g.copy();
                    if (g.number == 2) r.requestChallenge = g.copy();
                }
                break;
            }
            case 15: if (f.wire == 2) r.sessionInfo = f.copy(); break;
            case 50: if (f.wire == 2) r.requestUuid = f.copy(); break;
            case 51: if (f.wire == 2) r.uuid = f.copy(); break;
            case 52: r.flags = static_cast<uint32_t>(f.value); break;
            default: break;
        }
    }
    return in.ok();
}

bool decodeSessionInfo(const Bytes& bytes, SessionInfo& info) {
    info = SessionInfo();
    Reader in(bytes);
    Field f;
    while (in.next(f)) {
        if (f.number == 1) info.counter = static_cast<uint32_t>(f.value);
        else if (f.number == 2) info.publicKey = f.copy();
        else if (f.number == 3) info.epoch = f.copy();
        else if (f.number == 4) info.clockTime = static_cast<uint32_t>(f.value);
        else if (f.number == 5) info.status = static_cast<int>(f.value);
    }
    return in.ok() && info.publicKey.size() == 65 && info.epoch.size() == 16;
}

// ---- VCSEC ------------------------------------------------------------------------------------------------------------------

static Bytes wrapVcsec(const Writer& unsignedMessage, int signatureType) {
    Writer signedMessage;
    signedMessage.bytes(2, unsignedMessage.data());
    if (signatureType) signedMessage.varint(3, static_cast<uint64_t>(signatureType));
    Writer to;
    to.message(1, signedMessage);
    return to.data();
}

Bytes vcsecAddKey(const Bytes& publicKey65, int role, int formFactor) {
    Writer key;
    key.bytes(1, publicKey65);                       // PublicKey.PublicKeyRaw
    Writer change;
    change.message(1, key);                          // PermissionChange.key
    change.varint(4, static_cast<uint64_t>(role));   // PermissionChange.keyRole (there is no field 2; 3 is the active time, left out = permanent)
    Writer meta;
    meta.varint(1, static_cast<uint64_t>(formFactor));
    Writer op;
    op.message(5, change);                           // addKeyToWhitelistAndAddPermissions
    op.message(6, meta);                             // metadataForKey
    Writer unsignedMessage;
    unsignedMessage.message(16, op);                 // UnsignedMessage.WhitelistOperation
    return wrapVcsec(unsignedMessage, 2);            // SIGNATURE_TYPE_PRESENT_KEY: approved by tapping a key card
}

Bytes vcsecGetStatus() {
    Writer request;                                  // informationRequestType 0 = GET_STATUS (left out as the default)
    Writer unsignedMessage;
    unsignedMessage.message(1, request);
    return wrapVcsec(unsignedMessage, 0);
}

Bytes vcsecGetWhitelistEntry(const Bytes& publicKey65) {
    std::vector<uint8_t> digest = sha1(std::string(publicKey65.begin(), publicKey65.end()));
    Writer id;
    id.bytes(1, Bytes(digest.begin(), digest.end()));
    Writer request;
    request.varint(1, 6);                            // GET_WHITELIST_ENTRY_INFO
    request.message(2, id);
    Writer unsignedMessage;
    unsignedMessage.message(1, request);
    return wrapVcsec(unsignedMessage, 0);
}

bool decodeVcsecReply(const Bytes& bytes, VcsecReply& reply) {
    reply = VcsecReply();
    Reader in(bytes);
    Field f;
    while (in.next(f)) {
        if (f.wire != 2) continue;
        Reader d(f.data, f.len);
        Field g;
        if (f.number == 1) {                         // vehicleStatus
            reply.isVehicleStatus = true;
            while (d.next(g)) {
                if (g.number == 2) reply.lockState = static_cast<int>(g.value);
                if (g.number == 3) reply.sleepStatus = static_cast<int>(g.value);
                if (g.number == 4) reply.userPresence = static_cast<int>(g.value);
            }
            if (reply.lockState < 0) reply.lockState = 0;   // the default value is not written
        } else if (f.number == 4) {                  // commandStatus
            reply.isCommandStatus = true;
            reply.operationStatus = 0;
            while (d.next(g)) {
                if (g.number == 1) reply.operationStatus = static_cast<int>(g.value);
                if (g.number == 3 && g.wire == 2) {
                    reply.hasWhitelistStatus = true;
                    Reader s(g.data, g.len);
                    Field h;
                    while (s.next(h)) {
                        if (h.number == 1) reply.whitelistInfo = static_cast<int>(h.value);
                        if (h.number == 3 && reply.operationStatus == 0) reply.operationStatus = static_cast<int>(h.value);
                    }
                }
            }
        } else if (f.number == 17) {                 // whitelistEntryInfo
            reply.isWhitelistEntry = true;
            while (d.next(g)) if (g.number == 7) reply.keyRole = static_cast<int>(g.value);
        }
    }
    return in.ok();
}

std::string whitelistInfoText(int info) {
    switch (info) {
        case 0: return "accepted";
        case 3: return "the car's key-fob slots are full";
        case 4: return "the car's list of keys is full: remove a key you no longer use (Controls > Locks)";
        case 5: return "this key is not allowed to add keys";
        case 6: return "the car does not accept this public key";
        case 13: return "this key is already on the car";
        case 14: return "keys can only be added from inside the car, with a key card on the reader";
        case 19: return "the car wants a role for the new key";
        case 20: return "the car refused the role";
        case 23: return "the car could not start the key-card check";
        case 24: return "the request was denied on the car's screen";
        case 25: return "the key card was not tapped in time";
        case 26: return "the car's screen was not answered in time";
        case 27: return "the car is in Valet mode: turn it off first";
        case 28: return "the request was cancelled on the car";
        default: return "the car refused (code " + std::to_string(info) + ")";
    }
}

// ---- CarServer --------------------------------------------------------------------------------------------------------------

Bytes carGetChargeState() {
    Writer charge;                                   // GetChargeState, empty
    Writer get;
    get.message(2, charge);                          // GetVehicleData.getChargeState
    Writer vehicleAction;
    vehicleAction.message(1, get);                   // VehicleAction.getVehicleData
    Writer action;
    action.message(2, vehicleAction);                // Action.vehicleAction
    return action.data();
}

static void decodeChargeState(const uint8_t* p, size_t n, ChargeState& c) {
    c.valid = true;
    Reader in(p, n);
    Field f;
    while (in.next(f)) {
        int v = static_cast<int>(f.value);
        switch (f.number) {
            case 1: {                                // ChargingState: the chosen option is the field that is set
                Reader s(f.data, f.len);
                Field g;
                if (s.next(g)) c.chargingState = g.number;
                break;
            }
            case 104: c.hasChargeLimit = true; c.chargeLimit = v; break;
            case 114: c.hasBatteryLevel = true; c.batteryLevel = v; break;
            case 119: c.hasVoltage = true; c.voltage = v; break;
            case 121: c.hasActualCurrent = true; c.actualCurrent = v; break;
            case 122: c.hasPower = true; c.power = v; break;
            case 137: c.hasCurrentRequest = true; c.currentRequest = v; break;
            case 138: c.hasCurrentRequestMax = true; c.currentRequestMax = v; break;
            default: break;
        }
    }
}

bool decodeCarReply(const Bytes& bytes, CarReply& reply) {
    reply = CarReply();
    reply.ok = true;
    Reader in(bytes);
    Field f;
    while (in.next(f)) {
        if (f.wire != 2) continue;
        Reader d(f.data, f.len);
        Field g;
        if (f.number == 1) {                         // actionStatus
            while (d.next(g)) {
                if (g.number == 1 && g.value != 0) reply.ok = false;
                if (g.number == 2 && g.wire == 2) {
                    Reader r(g.data, g.len);
                    Field h;
                    while (r.next(h)) if (h.number == 1 && h.wire == 2) reply.reasonText = h.str();
                }
            }
        } else if (f.number == 2) {                  // vehicleData
            while (d.next(g)) if (g.number == 3 && g.wire == 2) decodeChargeState(g.data, g.len, reply.charge);
        }
    }
    return in.ok();
}

const char* chargingStateText(int s) {
    switch (s) {
        case 2: return "not plugged in";
        case 3: return "plugged in, no power";
        case 4: return "starting";
        case 5: return "charging";
        case 6: return "complete";
        case 7: return "stopped";
        case 8: return "calibrating";
        default: return "unknown";
    }
}

const char* sleepText(int s) { return s == 1 ? "awake" : s == 2 ? "asleep" : "unknown"; }

const char* lockText(int s) {
    switch (s) {
        case 0: return "unlocked";
        case 1: return "locked";
        case 2: return "locked from inside";
        case 3: return "partly unlocked";
        default: return "unknown";
    }
}

const char* messageFaultText(int fault) {
    switch (fault) {
        case 0: return "no fault";
        case 1: return "the car is busy";
        case 2: return "the car timed out";
        case 3: return "the car does not know our key (pair again)";
        case 4: return "our key is not active on the car";
        case 5: return "the car rejected our signature";
        case 6: return "the car rejected our counter (session out of step)";
        case 7: return "our key's role is not allowed to do this";
        case 8: return "wrong domain";
        case 9: return "the car did not understand the command";
        case 10: return "the car could not read the message";
        case 12: return "the car expects our identity (VIN) to match";
        case 15: return "the car restarted its session: try again";
        case 17: return "the message came too late";
        case 18: return "the car has no identity set";
        case 21: return "remote access is switched off on the car";
        case 28: return "the car requires an encrypted reply";
        default: return "the car reported a fault";
    }
}

}  // namespace tesla
}  // namespace solar
