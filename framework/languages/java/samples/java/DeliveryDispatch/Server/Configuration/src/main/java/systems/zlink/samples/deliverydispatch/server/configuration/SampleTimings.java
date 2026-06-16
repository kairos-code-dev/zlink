package systems.zlink.samples.deliverydispatch.server.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration FrameworkTimeout = Duration.ofSeconds(5);
    public static final Duration DispatchTimeout = Duration.ofMillis(700);
    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration ConnectTimeout = Duration.ofSeconds(5);
    public static final Duration NotifyTimeout = Duration.ofSeconds(8);
    public static final Duration ChannelRetryDelay = Duration.ofMillis(100);
    public static final int MaxChannelAttempts = 40;

    private SampleTimings() {
    }
}
