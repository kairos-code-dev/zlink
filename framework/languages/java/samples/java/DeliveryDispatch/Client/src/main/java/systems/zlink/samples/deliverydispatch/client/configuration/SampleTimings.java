package systems.zlink.samples.deliverydispatch.client.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration RequestTimeout = Duration.ofSeconds(12);
    public static final Duration ConnectTimeout = Duration.ofSeconds(5);
    public static final Duration NotifyTimeout = Duration.ofSeconds(8);

    private SampleTimings() {
    }
}
