package systems.zlink.samples.shoppingmall.server.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration FrameworkTimeout = Duration.ofSeconds(8);
    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration WorkflowTimeout = Duration.ofSeconds(8);
    public static final Duration ChannelRetryDelay = Duration.ofMillis(100);
    public static final Duration PollDelay = Duration.ofMillis(100);
    public static final int MaxChannelAttempts = 80;

    private SampleTimings() {
    }
}
