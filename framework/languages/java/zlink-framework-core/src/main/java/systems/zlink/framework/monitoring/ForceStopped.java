package systems.zlink.framework.monitoring;

import java.util.Objects;

public record ForceStopped(ZLinkDrainForceReason reason) implements ZLinkDrainResult {
    public ForceStopped {
        Objects.requireNonNull(reason, "reason");
    }
}
