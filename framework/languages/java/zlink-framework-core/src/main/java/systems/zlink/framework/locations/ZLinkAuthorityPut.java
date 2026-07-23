package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkAuthorityPut(
    byte[] payload,
    ZLinkAuthorityGenerationTransition generationTransition)
    implements ZLinkAuthorityMutation {
    public ZLinkAuthorityPut {
        payload = Objects.requireNonNull(payload, "payload").clone();
        Objects.requireNonNull(generationTransition, "generationTransition");
    }

    @Override
    public byte[] payload() {
        return payload.clone();
    }
}
