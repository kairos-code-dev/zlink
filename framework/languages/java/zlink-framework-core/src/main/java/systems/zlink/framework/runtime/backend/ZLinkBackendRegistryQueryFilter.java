package systems.zlink.framework.runtime.backend;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceKind;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.TopologySource;
import systems.zlink.contracts.service.registry.TopologyState;

public record ZLinkBackendRegistryQueryFilter(
    Optional<AutoConnectType> autoConnectType,
    Optional<ServiceKind> serviceKind,
    Optional<ServiceRole> serviceRole,
    Optional<String> channelName,
    Optional<RoutingId> routingId,
    Optional<TopologyState> state,
    Optional<TopologySource> source) {
    public ZLinkBackendRegistryQueryFilter {
        autoConnectType = autoConnectType == null ? Optional.empty() : autoConnectType;
        serviceKind = serviceKind == null ? Optional.empty() : serviceKind;
        serviceRole = serviceRole == null ? Optional.empty() : serviceRole;
        channelName = channelName == null ? Optional.empty() : channelName;
        routingId = routingId == null ? Optional.empty() : routingId;
        state = state == null ? Optional.empty() : state;
        source = source == null ? Optional.empty() : source;
    }

    public static ZLinkBackendRegistryQueryFilter all() {
        return new ZLinkBackendRegistryQueryFilter(
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty());
    }
}
