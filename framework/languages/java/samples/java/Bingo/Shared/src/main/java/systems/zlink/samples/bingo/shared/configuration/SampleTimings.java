package systems.zlink.samples.bingo.shared.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration ConnectTimeout = Duration.ofSeconds(5);
    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration DrawPeriod = Duration.ofMillis(20);

    private SampleTimings() {
    }
}
