#include "car/ChargeRouter.h"

namespace solar {

void ChargeRouter::apply(bool car) {
    if (car) {
        wallbox_.holdFixedLimit(std::nullopt);   // let go of any fixed current on the wallbox
        throughCar_ = true;
        commander_.setActive(true);
    } else {
        commander_.setActive(false);
        throughCar_ = false;
    }
}

CarResult ChargeRouter::setThroughCar(bool car) {
    CarResult r;
    if (car == throughCar_) return r;
    if (car && !car_.snapshot().paired) {
        r.ok = false;
        r.message = "No car is paired yet.";
        return r;
    }
    apply(car);
    if (settings_) settings_->saveThrough(car);
    if (history_) history_->addEvent(car ? "Control moved to the car" : "Control moved back to the charger");
    if (onChanged_) onChanged_();
    return r;
}

void ChargeRouter::setOnReturn(CarOnReturn policy) {
    commander_.setOnReturn(policy);
    if (settings_) settings_->saveOnReturn(static_cast<int>(policy));
}

void ChargeRouter::setAllowWake(bool on) {
    commander_.setAllowWake(on);
    if (settings_) settings_->saveAllowWake(on);
}

void ChargeRouter::restore() {
    if (!settings_) return;
    int policy = settings_->loadOnReturn();
    commander_.setOnReturn(policy == 1 ? CarOnReturn::Tell : policy == 2 ? CarOnReturn::Stop : CarOnReturn::Keep);
    commander_.setAllowWake(settings_->loadAllowWake());
    if (settings_->loadThrough()) {
        if (car_.snapshot().paired) apply(true);
        else settings_->saveThrough(false);   // the car is gone: never leave it in command
    }
}

}  // namespace solar
