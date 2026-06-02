package systems.zlink.framework.runtime.backend;

import java.util.Optional;

public record ZLinkBackendRegistryQueryFilter(Optional<String> channelName) {
    public static ZLinkBackendRegistryQueryFilter all() {
        return new ZLinkBackendRegistryQueryFilter(Optional.empty());
    }
}
