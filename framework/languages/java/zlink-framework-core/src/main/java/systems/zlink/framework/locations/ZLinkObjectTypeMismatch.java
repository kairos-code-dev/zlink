package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkObjectTypeMismatch(ZLinkAuthoritySnapshot current)
    implements ZLinkObjectReserveResult {
    public ZLinkObjectTypeMismatch {
        Objects.requireNonNull(current, "current");
    }
}
