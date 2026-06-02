package systems.zlink.framework.monitoring;

import java.time.Instant;

public interface ZLinkRuntimeEvent {
    String sourceName();

    Instant timestamp();
}
