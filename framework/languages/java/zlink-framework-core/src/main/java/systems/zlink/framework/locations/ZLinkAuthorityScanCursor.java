package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkAuthorityScanCursor(String encoded) {
    public ZLinkAuthorityScanCursor {
        Objects.requireNonNull(encoded, "encoded");
    }
}
