package com.brimwatt.enphase;

import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import java.net.http.HttpClient;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;

/**
 * Builds an HTTP client that trusts any certificate. The Envoy gateway uses a self-signed
 * certificate on the local network, so this is only suitable for talking to that device.
 */
public final class HttpClientFactory {

    private HttpClientFactory() {}

    public static HttpClient createTrustAll() throws Exception {
        TrustManager[] trustAll = {
                new X509TrustManager() {
                    public X509Certificate[] getAcceptedIssuers() { return null; }
                    public void checkClientTrusted(X509Certificate[] certs, String authType) {}
                    public void checkServerTrusted(X509Certificate[] certs, String authType) {}
                }
        };
        SSLContext sslContext = SSLContext.getInstance("SSL");
        sslContext.init(null, trustAll, new SecureRandom());
        System.setProperty("jdk.internal.httpclient.disableHostnameVerification", "true");
        return HttpClient.newBuilder().sslContext(sslContext).version(HttpClient.Version.HTTP_1_1).build();
    }
}
