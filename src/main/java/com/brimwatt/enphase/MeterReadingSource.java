package com.brimwatt.enphase;

import com.brimwatt.model.EnphaseMeterReading;

import java.util.List;

/** Source of live meter readings: production first, net grid second. */
public interface MeterReadingSource {

    List<EnphaseMeterReading> fetchMeterReadings() throws Exception;
}
