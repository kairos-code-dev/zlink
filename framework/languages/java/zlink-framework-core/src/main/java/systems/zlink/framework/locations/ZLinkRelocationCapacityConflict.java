package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkRelocationCapacityConflict(
    ZLinkAuthorityReadResult current)
    implements ZLinkRelocationCapacityReserveResult {
    public ZLinkRelocationCapacityConflict {
        Objects.requireNonNull(current, "current");
    }
}
