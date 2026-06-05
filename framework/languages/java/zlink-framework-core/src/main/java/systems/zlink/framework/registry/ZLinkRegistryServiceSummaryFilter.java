package systems.zlink.framework.registry;

import java.util.Optional;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceRole;

public record ZLinkRegistryServiceSummaryFilter(
    Optional<AutoConnectType> autoConnectType,
    Optional<ServiceRole> serviceRole,
    Optional<String> channelName) {
    public ZLinkRegistryServiceSummaryFilter {
        autoConnectType = autoConnectType == null ? Optional.empty() : autoConnectType;
        serviceRole = serviceRole == null ? Optional.empty() : serviceRole;
        channelName = channelName == null ? Optional.empty() : channelName;
    }

    public static ZLinkRegistryServiceSummaryFilter all() {
        return new ZLinkRegistryServiceSummaryFilter(
            Optional.empty(),
            Optional.empty(),
            Optional.empty());
    }

    public static ZLinkRegistryServiceSummaryFilter channel(String channelName) {
        return new ZLinkRegistryServiceSummaryFilter(
            Optional.empty(),
            Optional.empty(),
            Optional.of(requireName(channelName, "channelName")));
    }

    private static String requireName(String value, String label) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(label + " is required");
        }
        return value;
    }
}
