package systems.zlink.framework.locations;

import java.time.Instant;
import java.util.Objects;

public record ZLinkAuthoritySnapshot(
    String storeVersion,
    byte[] payload,
    long objectGeneration,
    long authorityOwnerGeneration,
    String ownerId,
    long ownerLeaseGeneration,
    Instant storeNow)
    implements ZLinkAuthorityReadResult {
    public ZLinkAuthoritySnapshot {
        Objects.requireNonNull(storeVersion, "storeVersion");
        payload = Objects.requireNonNull(payload, "payload").clone();
        Objects.requireNonNull(ownerId, "ownerId");
        Objects.requireNonNull(storeNow, "storeNow");
    }

    @Override
    public byte[] payload() {
        return payload.clone();
    }
}
