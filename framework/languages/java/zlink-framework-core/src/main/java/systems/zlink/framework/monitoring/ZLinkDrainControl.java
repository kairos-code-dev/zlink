package systems.zlink.framework.monitoring;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkDrainControl {
    CompletionStage<ZLinkDrainResult> drain();

    CompletionStage<ZLinkDrainResult> drain(Duration deadline);

    CompletionStage<ZLinkDrainResult> awaitDrained();

    boolean isReady();
}
