package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkAuthorityExpectFound(String storeVersion)
    implements ZLinkAuthorityExpectation {
    public ZLinkAuthorityExpectFound {
        Objects.requireNonNull(storeVersion, "storeVersion");
    }
}
