#include "ble/TeslaCar.h"

#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include "common/Log.h"
#include "tesla/TeslaBle.h"
#include "tesla/TeslaSelfTest.h"

namespace device {

using solar::tesla::Bytes;

namespace {

const char* kWriteUuid = "00000212-b2d1-43f0-9b88-960cebf8b91e";
const char* kReadUuid = "00000213-b2d1-43f0-9b88-960cebf8b91e";

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
std::string incoming;                    // bytes the car sent, filled on the Bluetooth task
bool disconnected = false;
std::string wantedName;
bool found = false;
BLEAddress foundAddress("00:00:00:00:00:00");
esp_ble_addr_type_t foundType = BLE_ADDR_TYPE_PUBLIC;

class Finder : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) override {
        if (wantedName.empty() || !device.haveName() || std::string(device.getName().c_str()) != wantedName) return;
        portENTER_CRITICAL(&lock);
        found = true;
        foundAddress = device.getAddress();
        foundType = device.getAddressType();
        portEXIT_CRITICAL(&lock);
        BLEDevice::getScan()->stop();
    }
};
Finder finder;

class LinkEvents : public BLEClientCallbacks {
    void onConnect(BLEClient*) override {}
    void onDisconnect(BLEClient*) override {
        portENTER_CRITICAL(&lock);
        disconnected = true;
        portEXIT_CRITICAL(&lock);
    }
};
LinkEvents linkEvents;

void onData(BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
    portENTER_CRITICAL(&lock);
    if (incoming.size() + length < 4096) incoming.append(reinterpret_cast<char*>(data), length);
    portEXIT_CRITICAL(&lock);
}

}  // namespace

bool TeslaCar::hasKey() {
    Bytes k;
    return store_.loadKey(k);
}

bool TeslaCar::paired() { return hasKey() && !store_.loadVin().empty(); }

std::string TeslaCar::keyFingerprint() {
    Bytes priv, pub;
    if (!store_.loadKey(priv) || !crypto_.publicKeyFor(priv, pub)) return "";
    Bytes d = crypto_.sha256(pub);
    return solar::tesla::toHex(Bytes(d.begin(), d.begin() + 4));
}

std::string TeslaCar::pairText() const { return pair_ ? pair_->text() : std::string(); }

int TeslaCar::pairSecondsLeft() const {
    uint32_t used = millis() - startedAt_;
    return used >= 120000UL ? 0 : static_cast<int>((120000UL - used) / 1000);
}

void TeslaCar::forget() {
    if (busy()) cancel();
    store_.clear();
    result_ = Result();
    reading_ = Reading();
    LOG_INFO("Tesla: the key and the car were forgotten on this board. The car still lists the key: remove it there too (Controls > Locks).");
}

bool TeslaCar::selfTest(std::string& report) { return solar::tesla::runKnownAnswerTests(crypto_, report); }

bool TeslaCar::ensureKey() {
    if (store_.loadKey(priv_) && crypto_.publicKeyFor(priv_, pub_)) return true;
    if (!crypto_.generateKeyPair(priv_, pub_)) return false;
    store_.saveKey(priv_);
    LOG_INFO("Tesla: made this controller's own key (it stays on this board). Fingerprint %s.", keyFingerprint().c_str());
    return true;
}

std::string TeslaCar::startPair(const std::string& vin) {
    if (busy()) return "A Tesla job is already running. Type \"tesla stop\" to end it.";
    if (!solar::tesla::isValidVin(vin)) return "That is not a VIN (17 letters and digits, no I, O or Q).";
    if (!ensureKey()) return "Could not make a key on this board.";
    std::string upper;
    for (char c : vin) upper += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    vin_ = upper;
    name_ = solar::tesla::advertisedName(upper);
    pair_.reset(new solar::tesla::PairFlow(crypto_, pub_, solar::tesla::kRoleChargingManager));
    status_.reset();
    quiet_ = false;
    begin(Job::Pair);
    LOG_INFO("Tesla pair: looking for car %s. Sit in the car with your key card ready; nothing is changed until you tap it.", solar::tesla::maskVin(vin_).c_str());
    return "";
}

std::string TeslaCar::startStatus(bool quiet) {
    if (busy()) return "A Tesla job is already running. Type \"tesla stop\" to end it.";
    if (!store_.loadKey(priv_) || !crypto_.publicKeyFor(priv_, pub_)) return "No key yet. Pair first: tesla pair <VIN>.";
    vin_ = store_.loadVin();
    if (vin_.empty()) return "No car paired yet. Pair first: tesla pair <VIN>.";
    name_ = solar::tesla::advertisedName(vin_);
    status_.reset(new solar::tesla::StatusFlow(crypto_, priv_, pub_, vin_));
    pair_.reset();
    quiet_ = quiet;
    begin(Job::Status);
    if (!quiet) LOG_INFO("Tesla status: looking for car %s. This only reads; it changes nothing and does not wake the car.", solar::tesla::maskVin(vin_).c_str());
    return "";
}

std::string TeslaCar::startCharge(solar::tesla::ChargeCommandFlow::Command command, int amps) {
    using solar::tesla::ChargeCommandFlow;
    if (busy()) return "A Tesla job is already running. Type \"tesla stop\" to end it.";
    if (command == ChargeCommandFlow::SetAmps && (amps < ChargeCommandFlow::kMinAmps || amps > ChargeCommandFlow::kMaxAmps))
        return "The current must be between 6 and 40 A.";
    if (!store_.loadKey(priv_) || !crypto_.publicKeyFor(priv_, pub_)) return "No key yet. Pair first: tesla pair <VIN>.";
    vin_ = store_.loadVin();
    if (vin_.empty()) return "No car paired yet. Pair first: tesla pair <VIN>.";
    name_ = solar::tesla::advertisedName(vin_);
    charge_.reset(new ChargeCommandFlow(crypto_, priv_, pub_, vin_, command, amps));
    chargeWhat_ = command == ChargeCommandFlow::Start ? "start charging" : command == ChargeCommandFlow::Stop ? "stop charging" : "set the charging current to " + std::to_string(amps) + " A";
    pair_.reset();
    status_.reset();
    quiet_ = false;
    begin(Job::Charge);
    LOG_INFO("Tesla charge: looking for car %s to %s. This CHANGES the car's charging; it does not wake a sleeping car.", solar::tesla::maskVin(vin_).c_str(), chargeWhat_.c_str());
    return "";
}

void TeslaCar::begin(Job job) {
    job_ = job;
    stage_ = Stage::Finding;
    startedAt_ = lastProgress_ = lastActivity_ = millis();
    rx_.clear();
    lastPairState_ = -1;
    portENTER_CRITICAL(&lock);
    incoming.clear();
    disconnected = false;
    found = false;
    portEXIT_CRITICAL(&lock);
    wantedName = name_;
    if (!radioOn_) {
        BLEDevice::init("");
        radioOn_ = true;
    }
    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&finder, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scanFailed_ = !scan->start(kFindSeconds, nullptr, false);
}

void TeslaCar::cancel() {
    if (!busy()) return;
    // A command that is already out may have been applied: say so rather than just "stopped".
    if (job_ == Job::Charge && charge_ && stage_ == Stage::Running && !charge_->finished()) charge_->timedOut();
    finish("Stopped.");
}

void TeslaCar::finish(const std::string& why) {
    if (client_) {
        if (client_->isConnected()) {
            client_->disconnect();
            // Wait for the Bluetooth task to report the disconnect before the client is freed: deleting it while that event is still on its
            // way crashed the board (assert in FreeRTOS::Semaphore::give).
            for (int i = 0; i < 150; i++) {
                bool gone;
                portENTER_CRITICAL(&lock);
                gone = disconnected;
                portEXIT_CRITICAL(&lock);
                if (gone) break;
                delay(10);
            }
        }
        delay(150);   // let the event handler finish with the client
        delete client_;
        client_ = nullptr;
    }
    write_ = nullptr;
    BLEDevice::getScan()->stop();
    wantedName.clear();
    result_ = Result();
    result_.have = true;
    result_.job = jobName();
    result_.atMs = millis();
    if (job_ == Job::Pair && pair_) {
        bool done = pair_->state() == solar::tesla::PairFlow::Accepted || pair_->alreadyOnCar();
        result_.ok = done;
        result_.lines.push_back(done ? "The car accepted our key." : (why.empty() ? pair_->text() : why));
        if (done) {
            store_.saveVin(vin_);
            LOG_INFO("Tesla pair: done. Our key (fingerprint %s) is on the car with the Charging Manager role. You can see it, rename it or delete it in the car under Controls > Locks.", keyFingerprint().c_str());
        } else {
            LOG_WARN("Tesla pair: not paired. %s", why.empty() ? pair_->text().c_str() : why.c_str());
        }
    } else if (job_ == Job::Status && status_) {
        result_.ok = status_->ok();
        result_.lines = status_->lines();
        if (result_.lines.empty() && !why.empty()) result_.lines.push_back(why);
        if (status_->ok()) {
            reading_.have = true;
            reading_.atMs = result_.atMs;
            reading_.sleep = status_->sleepStatus();
            reading_.lock = status_->lockState();
            reading_.charge = status_->charge();
        }
        if (!quiet_) {
            for (const auto& line : status_->lines()) LOG_INFO("Tesla status: %s", line.c_str());
            if (!status_->ok() && status_->lines().empty()) LOG_WARN("Tesla status: %s", why.c_str());
            LOG_INFO("Tesla status: read-only; nothing was sent that changes the car.");
        } else if (!status_->ok()) {
            LOG_WARN("Tesla watch: %s", result_.lines.empty() ? "no answer." : result_.lines.back().c_str());
        }
    } else if (job_ == Job::Charge && charge_) {
        result_.ok = charge_->ok();
        result_.lines = charge_->lines();
        if (result_.lines.empty() && !why.empty()) result_.lines.push_back(why);
        if (charge_->ok() && charge_->charge().valid) {   // the read-back after the command
            reading_.have = true;
            reading_.atMs = result_.atMs;
            reading_.charge = charge_->charge();
        }
        for (const auto& line : charge_->lines()) {
            if (charge_->ok()) LOG_INFO("Tesla charge: %s", line.c_str());
            else LOG_WARN("Tesla charge: %s", line.c_str());
        }
        if (charge_->lines().empty()) LOG_WARN("Tesla charge: %s", why.empty() ? "no answer." : why.c_str());
    }
    job_ = Job::None;
    stage_ = Stage::Idle;
    pair_.reset();
    status_.reset();
    charge_.reset();
    vin_.clear();
    BLEDevice::deinit(false);   // never release_memory=true: the radio could not start again until a reboot, and a second release aborts
    radioOn_ = false;
}

bool TeslaCar::connect() {
    client_ = BLEDevice::createClient();
    client_->setClientCallbacks(&linkEvents);
    BLEAddress address("00:00:00:00:00:00");
    esp_ble_addr_type_t type;
    portENTER_CRITICAL(&lock);
    address = foundAddress;
    type = foundType;
    portEXIT_CRITICAL(&lock);
    if (!client_->connect(address, type)) {
        LOG_WARN("Tesla: could not connect (the car may be too far away or already talking to 3 other devices).");
        return false;
    }
    client_->setMTU(247);
    BLERemoteService* service = client_->getService(BLEUUID(solar::tesla::kServiceUuid));
    if (!service) {
        LOG_WARN("Tesla: the car does not offer the expected Bluetooth service.");
        return false;
    }
    BLERemoteCharacteristic* write = service->getCharacteristic(BLEUUID(kWriteUuid));
    BLERemoteCharacteristic* read = service->getCharacteristic(BLEUUID(kReadUuid));
    if (!write || !read) {
        LOG_WARN("Tesla: the car's Bluetooth service is missing a channel.");
        return false;
    }
    // The car answers by indication; fall back to notification if that is what it offers.
    bool indicate = read->canIndicate();
    if (!indicate && !read->canNotify()) {
        LOG_WARN("Tesla: the car's reply channel cannot send.");
        return false;
    }
    read->registerForNotify(onData, /*notifications=*/!indicate);
    write_ = write;
    return true;
}

void TeslaCar::sendMessage(const Bytes& routable) {
    auto* write = static_cast<BLERemoteCharacteristic*>(write_);
    if (!write || !client_) return;
    // Diagnostics (tesla debug on): what goes out and what comes back. Nothing secret is in these bytes (public key, challenge,
    // routing ids and encrypted data; the VIN is never sent in the clear).
    if (debug_) LOG_INFO("Tesla >> %u bytes: %s", (unsigned)routable.size(), solar::tesla::toHex(routable).c_str());
    Bytes frame;
    frame.push_back(static_cast<uint8_t>(routable.size() >> 8));
    frame.push_back(static_cast<uint8_t>(routable.size()));
    frame.insert(frame.end(), routable.begin(), routable.end());
    size_t chunk = client_->getMTU() > 23 ? client_->getMTU() - 3 : 20;
    for (size_t off = 0; off < frame.size(); off += chunk) {
        size_t n = std::min(chunk, frame.size() - off);
        write->writeValue(frame.data() + off, n, true);
    }
    lastActivity_ = millis();
}

void TeslaCar::handleFrames() {
    {
        portENTER_CRITICAL(&lock);
        rx_.append(incoming);
        incoming.clear();
        portEXIT_CRITICAL(&lock);
    }
    while (rx_.size() >= 2) {
        size_t n = (static_cast<uint8_t>(rx_[0]) << 8) | static_cast<uint8_t>(rx_[1]);
        if (rx_.size() < 2 + n) break;
        Bytes message(rx_.begin() + 2, rx_.begin() + 2 + n);
        rx_.erase(0, 2 + n);
        lastActivity_ = millis();
        if (debug_) LOG_INFO("Tesla << %u bytes: %s", (unsigned)message.size(), solar::tesla::toHex(message).c_str());
        if (job_ == Job::Pair && pair_) {
            Bytes next = pair_->onMessage(message);
            if (!next.empty()) sendMessage(next);
        } else if (job_ == Job::Status && status_) {
            Bytes next = status_->onMessage(message, millis());
            if (!next.empty()) sendMessage(next);
        } else if (job_ == Job::Charge && charge_) {
            Bytes next = charge_->onMessage(message, millis());
            if (!next.empty()) sendMessage(next);
        }
    }
}

void TeslaCar::loop() {
    if (job_ == Job::None) return;
    uint32_t now = millis();

    if (stage_ == Stage::Finding) {
        if (scanFailed_) {
            finish("Could not start the Bluetooth scan (the radio was busy). Try again.");
            return;
        }
        bool have;
        portENTER_CRITICAL(&lock);
        have = found;
        portEXIT_CRITICAL(&lock);
        if (have) {
            if (!quiet_) LOG_INFO("Tesla: car found, connecting...");
            if (!connect()) {
                finish("Could not connect.");
                return;
            }
            stage_ = Stage::Running;
            lastActivity_ = millis();
            if (job_ == Job::Pair) {
                sendMessage(pair_->request());
            }
            else if (job_ == Job::Status) sendMessage(status_->start());
            else {
                Bytes first = charge_->start();
                if (first.empty()) {
                    finish("");
                    return;
                }
                sendMessage(first);
            }
            return;
        }
        if (now - startedAt_ > (kFindSeconds + 1) * 1000UL) {
            finish("The car was not heard. Move the controller closer to the car and try again.");
        }
        return;
    }

    // Running
    bool gone;
    portENTER_CRITICAL(&lock);
    gone = disconnected;
    portEXIT_CRITICAL(&lock);
    handleFrames();
    now = millis();   // handleFrames may have sent a message (a slow, blocking write): an older `now` makes now - lastActivity_ wrap around to a huge number

    if (job_ == Job::Pair && pair_) {
        int state = static_cast<int>(pair_->state());
        if (state != lastPairState_) {
            lastPairState_ = state;
            LOG_INFO("Tesla pair: %s", pair_->text().c_str());
        }
        if (pair_->state() == solar::tesla::PairFlow::Accepted || pair_->state() == solar::tesla::PairFlow::Refused) {
            finish("");
            return;
        }
        if (now - startedAt_ > 120000UL) {
            finish("Timed out waiting for the key card (2 minutes).");
            return;
        }
        if (now - lastProgress_ >= 15000) {
            lastProgress_ = now;
            LOG_INFO("Tesla pair: still waiting (%lu s left)...", static_cast<unsigned long>((120000UL - (now - startedAt_)) / 1000));
        }
    } else if (job_ == Job::Status && status_) {
        if (status_->finished()) {
            finish("");
            return;
        }
        if (static_cast<int32_t>(now - lastActivity_) > 8000) {
            status_->timedOut();
            finish("");
            return;
        }
    } else if (job_ == Job::Charge && charge_) {
        if (charge_->finished()) {
            finish("");
            return;
        }
        if (static_cast<int32_t>(now - lastActivity_) > 8000) {
            charge_->timedOut();
            finish("");
            return;
        }
    }
    if (gone) {
        if (job_ == Job::Charge && charge_ && !charge_->finished()) charge_->timedOut();
        finish("The car dropped the connection.");
    }
}

}  // namespace device
