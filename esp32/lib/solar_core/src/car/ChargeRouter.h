#pragma once

#include <functional>
#include <optional>
#include <string>

#include "car/CarCommander.h"
#include "car/CarSettings.h"
#include "wallbox/ChargeDevice.h"

namespace solar {

/**
 * The one ChargeDevice the planner talks to: it passes everything on to whichever device is in command, the OCPP wallbox (the default) or
 * the car. Exactly one at a time. Switching releases the device that lets go and tells the controller to start its decisions afresh.
 */
class ChargeRouter : public ChargeDevice {
public:
    ChargeRouter(ChargeDevice& wallbox, CarCommander& commander, CarControl& car) : wallbox_(wallbox), commander_(commander), car_(car) {}

    void setHistory(HistoryService* history) override {
        history_ = history;
        wallbox_.setHistory(history);
        commander_.setHistory(history);
    }
    void setSettings(CarSettingsStore* store) { settings_ = store; }
    /** Called after every switch (the controller starts afresh). */
    void onChanged(std::function<void()> callback) { onChanged_ = std::move(callback); }

    bool throughCar() const { return throughCar_; }
    const CarCommander& commander() const { return commander_; }
    /** Moves the command to the car (paired cars only) or back to the charger. */
    CarResult setThroughCar(bool car);
    void setOnReturn(CarOnReturn policy);
    void setAllowWake(bool on);
    /**
     * Keeps the command in line with Tesla mode: on means the car is in command as soon as one is paired, off means the charger. Cheap to call
     * about once a second.
     */
    void enforceMode(bool teslaMode);
    /** At start-up: applies the saved choice (never the car if it is no longer paired). */
    void restore();

    // ---- ChargeDevice: everything goes to the device in command ----
    bool hasConnectedChargers() const override { return device().hasConnectedChargers(); }
    void sendChargingLimit(int amps) override { device().sendChargingLimit(amps); }
    void holdFixedLimit(std::optional<int> amps, const std::string& context = "") override { device().holdFixedLimit(amps, context); }
    void setCommandContext(const std::string& context) override { device().setCommandContext(context); }
    void refreshMeasurements() override { device().refreshMeasurements(); }
    double currentDrawAmps() override { return device().currentDrawAmps(); }
    bool drawIsMeasured() override { return device().drawIsMeasured(); }
    std::optional<int> currentLimitAmps() override { return device().currentLimitAmps(); }
    bool anyRestored() override { return device().anyRestored(); }
    std::optional<ChargerStatus> primaryStatus() override { return device().primaryStatus(); }
    const std::string& lastCommand() const override { return device().lastCommand(); }
    int64_t lastCommandAtSeconds() const override { return device().lastCommandAtSeconds(); }

private:
    ChargeDevice& device() { return throughCar_ ? static_cast<ChargeDevice&>(commander_) : wallbox_; }
    const ChargeDevice& device() const { return throughCar_ ? static_cast<const ChargeDevice&>(commander_) : wallbox_; }
    void apply(bool car);

    ChargeDevice& wallbox_;
    CarCommander& commander_;
    CarControl& car_;
    HistoryService* history_ = nullptr;
    CarSettingsStore* settings_ = nullptr;
    std::function<void()> onChanged_;
    bool throughCar_ = false;
};

}  // namespace solar
