package systems.zlink.framework.runtime.channels;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;

public final class ChannelRegistration {
    private final String name;
    private final ChannelKind kind;
    private final List<String> serverBinds = new ArrayList<>();
    private final List<String> clientManualEndpoints = new ArrayList<>();
    private final List<String> publisherBinds = new ArrayList<>();
    private final List<String> subscriberManualEndpoints = new ArrayList<>();
    private final List<String> routeBinds = new ArrayList<>();
    private final List<String> routeManualEndpoints = new ArrayList<>();
    private final List<String> handlerGroups = new ArrayList<>();
    private final List<ChannelSendHandlerRegistration<?, ?>> sendHandlers = new ArrayList<>();
    private final List<ChannelRequestHandlerRegistration<?, ?, ?>> requestHandlers = new ArrayList<>();
    private final List<ChannelPublishHandlerRegistration<?, ?>> publishHandlers = new ArrayList<>();
    private final List<ChannelRouteSendHandlerRegistration<?, ?>> routeSendHandlers =
        new ArrayList<>();
    private final List<ChannelRouteRequestHandlerRegistration<?, ?, ?>> routeRequestHandlers =
        new ArrayList<>();
    private String spotRouteEgressTarget;
    private boolean clientEnabled;
    private boolean serverEnabled;
    private boolean publisherEnabled;
    private boolean subscriberEnabled;
    private RoutingId routeRoutingId;

    public ChannelRegistration(String name, ChannelKind kind) {
        this.name = name;
        this.kind = kind;
    }

    public String name() {
        return name;
    }

    public ChannelKind kind() {
        return kind;
    }

    List<String> serverBinds() {
        return serverBinds;
    }

    List<String> clientManualEndpoints() {
        return clientManualEndpoints;
    }

    List<ChannelRequestHandlerRegistration<?, ?, ?>> requestHandlers() {
        return requestHandlers;
    }

    List<ChannelSendHandlerRegistration<?, ?>> sendHandlers() {
        return sendHandlers;
    }

    List<String> publisherBinds() {
        return publisherBinds;
    }

    List<String> subscriberManualEndpoints() {
        return subscriberManualEndpoints;
    }

    List<ChannelPublishHandlerRegistration<?, ?>> publishHandlers() {
        return publishHandlers;
    }

    List<String> routeBinds() {
        return routeBinds;
    }

    List<String> routeManualEndpoints() {
        return routeManualEndpoints;
    }

    List<ChannelRouteRequestHandlerRegistration<?, ?, ?>> routeRequestHandlers() {
        return routeRequestHandlers;
    }

    List<ChannelRouteSendHandlerRegistration<?, ?>> routeSendHandlers() {
        return routeSendHandlers;
    }

    public String spotRouteEgressTarget() {
        return spotRouteEgressTarget;
    }

    public List<String> handlerGroups() {
        return handlerGroups;
    }

    public List<Class<?>> handlerTypes() {
        List<Class<?>> types = new ArrayList<>();
        for (ChannelSendHandlerRegistration<?, ?> handler : sendHandlers) {
            types.add(handler.handlerType());
        }
        for (ChannelRequestHandlerRegistration<?, ?, ?> handler : requestHandlers) {
            types.add(handler.handlerType());
        }
        for (ChannelPublishHandlerRegistration<?, ?> handler : publishHandlers) {
            types.add(handler.handlerType());
        }
        for (ChannelRouteSendHandlerRegistration<?, ?> handler : routeSendHandlers) {
            types.add(handler.handlerType());
        }
        for (ChannelRouteRequestHandlerRegistration<?, ?, ?> handler : routeRequestHandlers) {
            types.add(handler.handlerType());
        }
        return List.copyOf(types);
    }

    public RoutingId routeRoutingId() {
        return routeRoutingId;
    }

    boolean clientEnabled() {
        return clientEnabled;
    }

    boolean publisherEnabled() {
        return publisherEnabled;
    }

    boolean subscriberEnabled() {
        return subscriberEnabled;
    }

    void enableClient() {
        clientEnabled = true;
    }

    void enableServer() {
        serverEnabled = true;
    }

    void enablePublisher() {
        publisherEnabled = true;
    }

    void enableSubscriber() {
        subscriberEnabled = true;
    }

    void addServerBind(String endpoint) {
        serverBinds.add(requireEndpoint(endpoint));
    }

    void addClientManualEndpoint(String endpoint) {
        clientManualEndpoints.add(requireEndpoint(endpoint));
    }

    void addPublisherBind(String endpoint) {
        publisherBinds.add(requireEndpoint(endpoint));
    }

    void addSubscriberManualEndpoint(String endpoint) {
        subscriberManualEndpoints.add(requireEndpoint(endpoint));
    }

    void setRouteRoutingId(RoutingId routingId) {
        routeRoutingId = routingId;
    }

    void addRouteBind(String endpoint) {
        routeBinds.add(requireEndpoint(endpoint));
    }

    void addRouteManualEndpoint(String endpoint) {
        routeManualEndpoints.add(requireEndpoint(endpoint));
    }

    void addHandlerGroup(String groupName) {
        if (groupName == null || groupName.isBlank()) {
            throw new ZLinkConfigurationException(
                "channel handler group name is required: " + name);
        }
        handlerGroups.add(groupName);
    }

    void addSendHandler(ChannelSendHandlerRegistration<?, ?> handler) {
        requireNonBlankPacketName(handler.packetName(),
            "client/server channel send handler packet name");
        sendHandlers.add(handler.withPacketName(
            resolvePacketName(handler.messageType(), handler.packetName())));
    }

    void addRequestHandler(ChannelRequestHandlerRegistration<?, ?, ?> handler) {
        requireNonBlankPacketName(handler.packetName(),
            "client/server channel request handler packet name");
        requestHandlers.add(handler.withPacketName(
            resolvePacketName(handler.requestType(), handler.packetName())));
    }

    void addPublishHandler(ChannelPublishHandlerRegistration<?, ?> handler) {
        requireNonBlankPacketName(handler.packetName(),
            "fanout channel publish handler packet name");
        publishHandlers.add(handler.withPacketName(
            resolvePacketName(handler.messageType(), handler.packetName())));
    }

    void addRouteRequestHandler(ChannelRouteRequestHandlerRegistration<?, ?, ?> handler) {
        requireNonBlankPacketName(handler.packetName(),
            "route mesh request handler packet name");
        routeRequestHandlers.add(handler.withPacketName(
            resolvePacketName(handler.requestType(), handler.packetName())));
    }

    void addRouteSendHandler(ChannelRouteSendHandlerRegistration<?, ?> handler) {
        requireNonBlankPacketName(handler.packetName(),
            "route mesh send handler packet name");
        routeSendHandlers.add(handler.withPacketName(
            resolvePacketName(handler.messageType(), handler.packetName())));
    }

    void enableSpotRouteEgress(String targetSpotNodeChannelName) {
        if (targetSpotNodeChannelName == null || targetSpotNodeChannelName.isBlank()) {
            throw new ZLinkConfigurationException(
                "spot route egress target SpotNode channel name is required: " + name);
        }
        spotRouteEgressTarget = targetSpotNodeChannelName;
    }

    public void validate(boolean discoveryEnabled) {
        validate(discoveryEnabled, new ZLinkScannedHandlerCatalog(List.of()));
    }

    public void validate(
        boolean discoveryEnabled,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        validateHandlerGroups();
        if (kind == ChannelKind.CLIENT_SERVER) {
            validateClientServer(discoveryEnabled, handlerCatalog);
        } else if (kind == ChannelKind.FANOUT) {
            validateFanout(discoveryEnabled, handlerCatalog);
        } else if (kind == ChannelKind.DEALER_MESH) {
            validateDealerMesh(discoveryEnabled, handlerCatalog);
        } else if (kind == ChannelKind.ROUTE_MESH) {
            validateRouteMesh(discoveryEnabled, handlerCatalog);
        }
    }

    private void validateClientServer(
        boolean discoveryEnabled,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        if (clientEnabled && !discoveryEnabled && clientManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "client/server channel client requires discovery or manual connections: " + name);
        }
        if (serverEnabled && serverBinds.isEmpty()) {
            throw new ZLinkConfigurationException(
                "client/server channel server requires at least one bind endpoint: " + name);
        }
        if (spotRouteEgressTarget != null && !clientEnabled) {
            throw new ZLinkConfigurationException(
                "client/server channel routed Spot egress requires client capability: " + name);
        }
        validateMappedGroups(handlerCatalog, ZLinkScannedHandlerSurface.CHANNEL,
            Set.of(ZLinkScannedHandlerKind.SEND, ZLinkScannedHandlerKind.REQUEST));
        boolean hasMappedHandlers =
            hasMappedHandler(handlerCatalog, ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.SEND)
                || hasMappedHandler(handlerCatalog, ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.REQUEST);
        if (serverEnabled && requestHandlers.isEmpty() && sendHandlers.isEmpty() && !hasMappedHandlers) {
            throw new ZLinkConfigurationException(
                "client/server channel server requires a send/request handler or handler group: " + name);
        }
        Set<String> packetNames = mappedPacketNames(
            handlerCatalog,
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.SEND,
            "duplicate client/server send handler packet name");
        for (ChannelSendHandlerRegistration<?, ?> handler : sendHandlers) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    "duplicate client/server send handler packet name: "
                        + name + "/" + handler.packetName());
            }
        }
        packetNames = mappedPacketNames(
            handlerCatalog,
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.REQUEST,
            "duplicate client/server request handler packet name");
        for (ChannelRequestHandlerRegistration<?, ?, ?> handler : requestHandlers) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    "duplicate client/server request handler packet name: "
                        + name + "/" + handler.packetName());
            }
        }
    }

    private void validateFanout(
        boolean discoveryEnabled,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        if (publisherEnabled && publisherBinds.isEmpty()) {
            throw new ZLinkConfigurationException(
                "fanout channel publisher requires at least one bind endpoint: " + name);
        }
        if (subscriberEnabled && !discoveryEnabled && subscriberManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "fanout channel subscriber requires discovery or manual connections: " + name);
        }
        validateMappedGroups(handlerCatalog, ZLinkScannedHandlerSurface.CHANNEL,
            Set.of(ZLinkScannedHandlerKind.PUBLISH));
        boolean hasMappedPublishHandlers =
            hasMappedHandler(handlerCatalog, ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.PUBLISH);
        if (subscriberEnabled && publishHandlers.isEmpty() && !hasMappedPublishHandlers) {
            throw new ZLinkConfigurationException(
                "fanout channel subscriber requires a publish handler or handler group: " + name);
        }
        Set<String> packetNames = mappedPacketNames(
            handlerCatalog,
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.PUBLISH,
            "duplicate fanout publish handler packet name");
        for (ChannelPublishHandlerRegistration<?, ?> handler : publishHandlers) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    "duplicate fanout publish handler packet name: "
                        + name + "/" + handler.packetName());
            }
        }
    }

    private void validateRouteMesh(
        boolean discoveryEnabled,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        if (routeBinds.isEmpty()) {
            throw new ZLinkConfigurationException(
                "route mesh channel requires at least one bind endpoint: " + name);
        }
        if (!discoveryEnabled && routeManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "route mesh channel requires discovery or manual connections: " + name);
        }
        validateMappedGroups(handlerCatalog, ZLinkScannedHandlerSurface.ROUTE,
            Set.of(ZLinkScannedHandlerKind.SEND, ZLinkScannedHandlerKind.REQUEST));
        Set<String> packetNames = mappedPacketNames(
            handlerCatalog,
            ZLinkScannedHandlerSurface.ROUTE,
            ZLinkScannedHandlerKind.SEND,
            "duplicate route mesh send handler packet name");
        for (ChannelRouteSendHandlerRegistration<?, ?> handler : routeSendHandlers) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    "duplicate route mesh send handler packet name: "
                        + name + "/" + handler.packetName());
            }
        }
        addMappedPacketNames(
            packetNames,
            handlerCatalog,
            ZLinkScannedHandlerSurface.ROUTE,
            ZLinkScannedHandlerKind.REQUEST,
            "duplicate route mesh request handler packet name");
        for (ChannelRouteRequestHandlerRegistration<?, ?, ?> handler : routeRequestHandlers) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    "duplicate route mesh request handler packet name: "
                        + name + "/" + handler.packetName());
            }
        }
    }

    private void validateDealerMesh(
        boolean discoveryEnabled,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        if (clientEnabled && !discoveryEnabled && clientManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "dealer mesh channel client requires discovery or manual connections: " + name);
        }
        if (!clientEnabled && handlerGroups.isEmpty()) {
            throw new ZLinkConfigurationException(
                "dealer mesh channel requires client capability or handler groups: " + name);
        }
        validateMappedGroups(handlerCatalog, ZLinkScannedHandlerSurface.CHANNEL,
            Set.of(ZLinkScannedHandlerKind.SEND, ZLinkScannedHandlerKind.REQUEST));
    }

    private void validateHandlerGroups() {
        Set<String> groups = new HashSet<>();
        for (String group : handlerGroups) {
            if (!groups.add(group)) {
                throw new ZLinkConfigurationException(
                    "duplicate channel handler group: " + name + "/" + group);
            }
        }
    }

    private static String requireEndpoint(String endpoint) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException("endpoint is required");
        }
        return endpoint;
    }

    private void requireNonBlankPacketName(String packetName, String label) {
        if (packetName != null && packetName.isBlank()) {
            throw new ZLinkConfigurationException(label + " is required: " + name);
        }
    }

    private static String resolvePacketName(Class<?> messageType, String explicitPacketName) {
        return explicitPacketName == null
            ? resolvePacketName(messageType)
            : explicitPacketName;
    }

    private static String resolvePacketName(Class<?> messageType) {
        ZLinkPacket packet = messageType.getAnnotation(ZLinkPacket.class);
        return packet == null ? messageType.getSimpleName() : packet.value();
    }

    private void validateMappedGroups(
        ZLinkScannedHandlerCatalog handlerCatalog,
        ZLinkScannedHandlerSurface surface,
        Set<ZLinkScannedHandlerKind> allowedKinds) {
        for (String group : handlerGroups) {
            if (!handlerCatalog.containsGroup(group)) {
                throw new ZLinkConfigurationException(
                    "channel maps unknown handler group: " + name + "/" + group);
            }
            if (!handlerCatalog.groupHasOnly(group, surface, allowedKinds)) {
                throw new ZLinkConfigurationException(
                    "channel maps incompatible handler group: " + name + "/" + group);
            }
        }
    }

    private boolean hasMappedHandler(
        ZLinkScannedHandlerCatalog handlerCatalog,
        ZLinkScannedHandlerSurface surface,
        ZLinkScannedHandlerKind kind) {
        return !handlerCatalog.matching(Set.copyOf(handlerGroups), surface, kind).isEmpty();
    }

    private Set<String> mappedPacketNames(
        ZLinkScannedHandlerCatalog handlerCatalog,
        ZLinkScannedHandlerSurface surface,
        ZLinkScannedHandlerKind kind,
        String label) {
        Set<String> packetNames = new HashSet<>();
        addMappedPacketNames(packetNames, handlerCatalog, surface, kind, label);
        return packetNames;
    }

    private void addMappedPacketNames(
        Set<String> packetNames,
        ZLinkScannedHandlerCatalog handlerCatalog,
        ZLinkScannedHandlerSurface surface,
        ZLinkScannedHandlerKind kind,
        String label) {
        for (var handler : handlerCatalog.matching(Set.copyOf(handlerGroups), surface, kind)) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    label + ": " + name + "/" + handler.packetName());
            }
        }
    }

}
