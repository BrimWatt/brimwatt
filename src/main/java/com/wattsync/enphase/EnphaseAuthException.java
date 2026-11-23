package com.wattsync.enphase;

import java.io.IOException;

/** The gateway rejected our access token, which usually means it has expired. */
public class EnphaseAuthException extends IOException {

    public EnphaseAuthException(int httpStatus) {
        super("Enphase rejected the access token (HTTP " + httpStatus + "); renew the ENPHASE_TOKEN value");
    }
}
