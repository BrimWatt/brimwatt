package com.brimwatt.enphase;

import com.brimwatt.config.AppConfig;
import com.brimwatt.model.EnphaseMeterReading;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.List;

/** Fetches live meter readings from the Enphase gateway. */
public class EnphaseClient implements MeterReadingSource {

    private final HttpClient httpClient;
    private final ObjectMapper objectMapper;
    private final String token;

    public EnphaseClient(HttpClient httpClient, ObjectMapper objectMapper, String token) {
        this.httpClient = httpClient;
        this.objectMapper = objectMapper;
        this.token = token;
    }

    @Override
    public List<EnphaseMeterReading> fetchMeterReadings() throws IOException, InterruptedException {
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(AppConfig.ENPHASE_HOST + "/ivp/meters/readings"))
                .timeout(Duration.ofSeconds(5))
                .header("Authorization", "Bearer " + token)
                .header("Accept", "application/json")
                .GET()
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        int status = response.statusCode();
        if (status == 401 || status == 403) {
            throw new EnphaseAuthException(status);
        }
        if (status != 200) {
            throw new IOException("Enphase gateway returned HTTP " + response.statusCode());
        }
        return objectMapper.readValue(
                response.body(),
                objectMapper.getTypeFactory().constructCollectionType(List.class, EnphaseMeterReading.class));
    }
}
