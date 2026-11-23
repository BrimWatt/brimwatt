package com.wattsync.ocpp;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * A short one-way fingerprint of the ID tag a charger sends when a charge starts (a card, the charger's own app, or the random tag we send
 * on a remote start). Only this fingerprint is ever stored or shown, never the tag itself: the same tag always gives the same fingerprint, so
 * charges can be told apart, but the tag cannot be read back. Mirrored by {@code solar::idTagHash} on the ESP32.
 */
public final class IdTagHash {

    private IdTagHash() {}

    /** The first 8 hex digits of SHA-256("wattsync:" + tag), or "" if the charger sent no tag. */
    public static String of(String idTag) {
        if (idTag == null || idTag.isBlank()) return "";
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(("wattsync:" + idTag).getBytes(StandardCharsets.UTF_8));
            StringBuilder out = new StringBuilder();
            for (int i = 0; i < 4; i++) out.append(String.format("%02x", digest[i]));
            return out.toString();
        } catch (NoSuchAlgorithmException e) {
            return "";   // every Java has SHA-256; if not, store nothing rather than the tag
        }
    }
}
