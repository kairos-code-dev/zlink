package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkObjectConflict(ZLinkAuthorityReadResult current)
    implements ZLinkObjectReserveResult {
    public ZLinkObjectConflict {
        Objects.requireNonNull(current, "current");
    }
}
