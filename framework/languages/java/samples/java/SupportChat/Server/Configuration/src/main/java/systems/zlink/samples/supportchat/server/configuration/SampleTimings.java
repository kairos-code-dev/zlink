package systems.zlink.samples.supportchat.server.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration IdleTimeout = Duration.ofSeconds(3);
    public static final Duration CloseGraceTimeout = Duration.ofSeconds(2);
    public static final Duration IdleCheckPeriod = Duration.ofMillis(200);
    public static final int MaxMessageLength = 500;

    private SampleTimings() {
    }
}
