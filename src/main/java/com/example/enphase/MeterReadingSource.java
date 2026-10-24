package com.example.enphase;

import com.example.model.EnphaseMeterReading;

import java.util.List;

/** Source of live meter readings: production first, net grid second. */
public interface MeterReadingSource {

    List<EnphaseMeterReading> fetchMeterReadings() throws Exception;
}
