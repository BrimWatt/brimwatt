#pragma once

#include <string>

#include "tesla/Proto.h"

namespace solar {
namespace tesla {

// Numbers from Tesla's open-source vehicle-command protocol files (universal_message, signatures, vcsec, keys, car_server, vehicle).
constexpr int kDomainVehicleSecurity = 2;
constexpr int kDomainInfotainment = 3;

constexpr int kRoleOwner = 2;
constexpr int kRoleDriver = 3;
constexpr int kRoleChargingManager = 6;

constexpr int kFormFactorCloudKey = 9;   // what a controller with its own key is closest to

constexpr uint32_t kFlagEncryptResponse = 1u << 1;

// Metadata tags and signature types (signatures.proto).
constexpr int kTagSignatureType = 0, kTagDomain = 1, kTagPersonalization = 2, kTagEpoch = 3, kTagExpiresAt = 4, kTagCounter = 5, kTagChallenge = 6,
              kTagFlags = 7, kTagRequestHash = 8, kTagFault = 9, kTagEnd = 255;
constexpr int kSigHmac = 6, kSigAesGcmPersonalized = 5, kSigAesGcmResponse = 9;

/** The envelope every message to or from the car travels in (RoutableMessage). Only the fields we use. */
struct Routable {
    int toDomain = -1;
    Bytes toRouting;
    Bytes fromRouting;
    bool hasPayload = false;
    Bytes payload;                    // protobuf_message_as_bytes (field 10)
    Bytes sessionInfo;                // field 15
    bool hasSessionInfoRequest = false;
    Bytes requestPublicKey;           // session_info_request.public_key
    Bytes requestChallenge;           // session_info_request.challenge
    Bytes signerPublicKey;            // signature_data.signer_identity.public_key
    bool hasGcm = false;              // signature_data.AES_GCM_Personalized_data
    Bytes gcmEpoch, gcmNonce;
    uint32_t gcmCounter = 0, gcmExpiresAt = 0;
    Bytes gcmTag;
    bool hasGcmResponse = false;      // signature_data.AES_GCM_Response_data
    Bytes respNonce;
    uint32_t respCounter = 0;
    Bytes respTag;
    Bytes sessionInfoTag;             // signature_data.session_info_tag.tag
    bool hasStatus = false;           // signedMessageStatus
    int statusOperation = 0, statusFault = 0;
    Bytes requestUuid, uuid;
    uint32_t flags = 0;
};

Bytes encodeRoutable(const Routable& r);
bool decodeRoutable(const Bytes& bytes, Routable& r);

struct SessionInfo {
    uint32_t counter = 0;
    Bytes publicKey;
    Bytes epoch;
    uint32_t clockTime = 0;
    int status = 0;          // 0 OK, 1 our key is not on the car's list
};
bool decodeSessionInfo(const Bytes& bytes, SessionInfo& info);

// ---- messages to the car's security computer (VCSEC), carried as a ToVCSECMessage -----------------------------------------

/** Asks the car to add this key. Not signed (nobody trusts us yet): the owner approves by tapping their key card. */
Bytes vcsecAddKey(const Bytes& publicKey65, int role, int formFactor);
/** Asks for the car's status (locked, asleep, someone near). Needs no key. */
Bytes vcsecGetStatus();
/** Asks whether our key is on the car's list (by the SHA-1 of the public key). */
Bytes vcsecGetWhitelistEntry(const Bytes& publicKey65);

struct VcsecReply {
    bool isCommandStatus = false;
    int operationStatus = -1;         // 0 OK, 1 WAIT, 2 ERROR
    bool hasWhitelistStatus = false;
    int whitelistInfo = 0;            // WhitelistOperation_information_E, 0 = none
    bool isVehicleStatus = false;
    int lockState = -1;               // 0 unlocked, 1 locked, 2 internal locked, 3 selective unlocked
    int sleepStatus = 0;              // 0 unknown, 1 awake, 2 asleep
    int userPresence = 0;             // 0 unknown, 1 not present, 2 present
    bool isWhitelistEntry = false;
    int keyRole = 0;
};
bool decodeVcsecReply(const Bytes& bytes, VcsecReply& reply);

/** Plain words for a refusal while adding a key. */
std::string whitelistInfoText(int info);

// ---- messages to the car's main computer (Infotainment / CarServer) -------------------------------------------------------

/** Action { vehicleAction { getVehicleData { getChargeState } } }: reads, changes nothing. */
Bytes carGetChargeState();

/** Action { vehicleAction { chargingStartStopAction { start | stop } } }: starts or stops the charge (the car must be plugged in and awake). */
Bytes carChargingStartStop(bool start);
/** Action { vehicleAction { setChargingAmpsAction { charging_amps } } }: the current the car asks the charger for. */
Bytes carSetChargingAmps(int amps);

struct ChargeState {
    bool valid = false;
    int chargingState = 0;            // 1 unknown, 2 disconnected, 3 no power, 4 starting, 5 charging, 6 complete, 7 stopped, 8 calibrating
    bool hasBatteryLevel = false;
    int batteryLevel = 0;
    bool hasChargeLimit = false;
    int chargeLimit = 0;
    bool hasActualCurrent = false;
    int actualCurrent = 0;
    bool hasCurrentRequest = false;
    int currentRequest = 0;
    bool hasCurrentRequestMax = false;
    int currentRequestMax = 0;
    bool hasPower = false;
    int power = 0;                    // kW as the car reports it
    bool hasVoltage = false;
    int voltage = 0;
};
struct CarReply {
    bool ok = false;                  // the action's own status says OK
    std::string reasonText;
    ChargeState charge;
};
bool decodeCarReply(const Bytes& bytes, CarReply& reply);

const char* chargingStateText(int s);
const char* sleepText(int s);
const char* lockText(int s);
const char* messageFaultText(int fault);

}  // namespace tesla
}  // namespace solar
