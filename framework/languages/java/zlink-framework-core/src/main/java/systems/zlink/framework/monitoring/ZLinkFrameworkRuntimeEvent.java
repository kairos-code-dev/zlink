package systems.zlink.framework.monitoring;

import java.time.Instant;

public record ZLinkFrameworkRuntimeEvent(
    long sequence,
    Instant timestamp,
    ZLinkFrameworkRuntimeSnapshot runtime) implements ZLinkRuntimeEvent {
    @Override
    public String sourceName() {
        return "zlink.runtime.host.termination_changed";
    }
}
