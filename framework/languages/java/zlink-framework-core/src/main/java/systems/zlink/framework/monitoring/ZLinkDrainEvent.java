package systems.zlink.framework.monitoring;

import java.time.Instant;

public record ZLinkDrainEvent(ZLinkDrainState state, Instant timestamp)
    implements ZLinkRuntimeEvent {
    public ZLinkDrainEvent {
        if (state == null || timestamp == null) {
            throw new IllegalArgumentException("state and timestamp are required");
        }
    }

    @Override
    public String sourceName() {
        return "drain";
    }
}
