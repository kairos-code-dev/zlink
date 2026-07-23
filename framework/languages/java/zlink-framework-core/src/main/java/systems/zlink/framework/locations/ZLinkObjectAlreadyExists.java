package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkObjectAlreadyExists(ZLinkAuthoritySnapshot current)
    implements ZLinkObjectReserveResult {
    public ZLinkObjectAlreadyExists {
        Objects.requireNonNull(current, "current");
    }
}
