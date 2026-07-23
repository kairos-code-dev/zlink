package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkAuthorityConflict(ZLinkAuthorityReadResult current)
    implements ZLinkAuthorityWriteResult {
    public ZLinkAuthorityConflict {
        Objects.requireNonNull(current, "current");
    }
}
