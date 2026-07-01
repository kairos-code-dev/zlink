package systems.zlink.samples.deliverydispatch.server.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration OfferRequestTimeout = Duration.ofSeconds(2);
    public static final Duration CourierDecisionTimeout = Duration.ofMillis(900);

    private SampleTimings() {
    }
}
