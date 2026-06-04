package systems.zlink.framework.registry;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkRegistryTopologyFilter(
    Optional<String> autoConnectType,
    Optional<String> serviceKind,
    Optional<String> serviceRole,
    Optional<String> channelName,
    Optional<RoutingId> routingId,
    Optional<String> state,
    Optional<String> source) {
    public ZLinkRegistryTopologyFilter {
        autoConnectType = autoConnectType == null ? Optional.empty() : autoConnectType;
        serviceKind = serviceKind == null ? Optional.empty() : serviceKind;
        serviceRole = serviceRole == null ? Optional.empty() : serviceRole;
        channelName = channelName == null ? Optional.empty() : channelName;
        routingId = routingId == null ? Optional.empty() : routingId;
        state = state == null ? Optional.empty() : state;
        source = source == null ? Optional.empty() : source;
    }

    public static ZLinkRegistryTopologyFilter all() {
        return new ZLinkRegistryTopologyFilter(
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty());
    }

    public static ZLinkRegistryTopologyFilter channel(String channelName) {
        return new ZLinkRegistryTopologyFilter(
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.of(requireName(channelName, "channelName")),
            Optional.empty(),
            Optional.empty(),
            Optional.empty());
    }

    private static String requireName(String value, String label) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(label + " is required");
        }
        return value;
    }
}
