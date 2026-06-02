package systems.zlink.framework.registry;

import java.util.Optional;

public record ZLinkRegistryQueryFilter(Optional<String> channelName) {
    public ZLinkRegistryQueryFilter {
        channelName = channelName == null ? Optional.empty() : channelName;
    }

    public static ZLinkRegistryQueryFilter all() {
        return new ZLinkRegistryQueryFilter(Optional.empty());
    }

    public static ZLinkRegistryQueryFilter channel(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new IllegalArgumentException("channelName is required");
        }
        return new ZLinkRegistryQueryFilter(Optional.of(channelName));
    }
}
