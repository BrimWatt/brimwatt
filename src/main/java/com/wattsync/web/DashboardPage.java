package com.wattsync.web;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;

/** Loads the single-page dashboard from the classpath once. */
public final class DashboardPage {

    private static final String HTML = load("/dashboard.html");

    private DashboardPage() {}

    private static final byte[] FAVICON = loadBytes("/favicon.png");

    public static String html() { return HTML; }

    /** The 32 px PNG icon, also served at /favicon.ico for browsers that ask for it there. */
    public static byte[] favicon() { return FAVICON; }

    private static byte[] loadBytes(String resource) {
        try (InputStream in = DashboardPage.class.getResourceAsStream(resource)) {
            if (in == null) throw new IllegalStateException("Missing resource " + resource);
            return in.readAllBytes();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private static String load(String resource) {
        try (InputStream in = DashboardPage.class.getResourceAsStream(resource)) {
            if (in == null) throw new IllegalStateException("Missing resource " + resource);
            return new String(in.readAllBytes(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }
}
