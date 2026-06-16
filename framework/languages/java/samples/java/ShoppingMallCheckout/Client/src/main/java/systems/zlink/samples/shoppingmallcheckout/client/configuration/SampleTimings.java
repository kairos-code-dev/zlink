package systems.zlink.samples.shoppingmallcheckout.client.configuration;

import java.time.Duration;

public final class SampleTimings {
    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration WorkflowTimeout = Duration.ofSeconds(12);
    public static final Duration PollDelay = Duration.ofMillis(100);

    private SampleTimings() {
    }
}
