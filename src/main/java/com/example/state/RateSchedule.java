package com.example.state;

/** The price of electricity by the hour: one normal price, and an optional peak price for certain hours of the day. */
public final class RateSchedule {

    private RateSchedule() {}

    /**
     * @param peakCents  the peak price, or 0 for no peak pricing
     * @param startHour  first hour of the peak, 0 to 23
     * @param endHour    the hour the peak ends, 1 to 24 (exclusive); a start after the end wraps past midnight
     */
    public static int centsAt(int baseCents, int peakCents, int startHour, int endHour, int hourOfDay) {
        if (peakCents <= 0) return baseCents;
        boolean inPeak = startHour < endHour ? hourOfDay >= startHour && hourOfDay < endHour
                : hourOfDay >= startHour || hourOfDay < endHour;
        return inPeak ? peakCents : baseCents;
    }
}
