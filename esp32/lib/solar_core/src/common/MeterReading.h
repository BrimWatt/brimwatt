#pragma once

namespace solar {

/** One reading of the two Envoy meters. */
struct MeterReading {
    double solarWatts = 0;   // production meter, never negative
    double gridWatts = 0;    // net consumption meter: positive is importing, negative is exporting
};

}  // namespace solar
