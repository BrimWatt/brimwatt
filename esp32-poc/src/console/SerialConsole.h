#pragma once

#include <string>

#include "control/SolarController.h"
#include "enphase/MeterPoller.h"
#include "wallbox/WallboxController.h"

namespace device {

/**
 * Commands typed in the serial monitor: the same controls the dashboard will offer (mode, Custom current,
 * allowance, daylight window), plus a few for trying the wallbox connection by hand.
 */
class SerialConsole {
public:
    SerialConsole(solar::SolarController& controller, solar::WallboxController& wallbox, MeterPoller& meters)
        : controller_(controller), wallbox_(wallbox), meters_(meters) {}

    /** Call from the main loop: reads any typed characters and runs a command at the end of the line. */
    void poll();

    void printHelpHint();

    /** The one-line summary printed every minute. */
    std::string statusLine();

private:
    void run(const std::string& line);
    void printHelp();

    solar::SolarController& controller_;
    solar::WallboxController& wallbox_;
    MeterPoller& meters_;
    std::string buffer_;
};

}  // namespace device
