package systems.zlink.samples.gamequest.client.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration ConnectTimeout = Duration.ofSeconds(10);
    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration NotifyTimeout = Duration.ofSeconds(20);
    public static final Duration PollDelay = Duration.ofMillis(100);

    private SampleTimings() {
    }
}
