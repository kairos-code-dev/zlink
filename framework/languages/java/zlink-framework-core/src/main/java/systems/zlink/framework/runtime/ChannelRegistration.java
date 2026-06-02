package systems.zlink.framework.runtime;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import systems.zlink.framework.errors.ZLinkConfigurationException;

final class ChannelRegistration {
    private final String name;
    private final ChannelKind kind;
    private final List<String> serverBinds = new ArrayList<>();
    private final List<String> clientManualEndpoints = new ArrayList<>();
    private final List<String> publisherBinds = new ArrayList<>();
    private final List<String> subscriberManualEndpoints = new ArrayList<>();
    private final List<ChannelRequestHandlerRegistration<?, ?, ?>> requestHandlers = new ArrayList<>();
    private final List<ChannelPublishHandlerRegistration<?, ?>> publishHandlers = new ArrayList<>();
    private boolean clientEnabled;
    private boolean serverEnabled;
    private boolean publisherEnabled;
    private boolean subscriberEnabled;

    ChannelRegistration(String name, ChannelKind kind) {
        this.name = name;
        this.kind = kind;
    }

    String name() {
        return name;
    }

    ChannelKind kind() {
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

    List<String> publisherBinds() {
        return publisherBinds;
    }

    List<String> subscriberManualEndpoints() {
        return subscriberManualEndpoints;
    }

    List<ChannelPublishHandlerRegistration<?, ?>> publishHandlers() {
        return publishHandlers;
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

    void addRequestHandler(ChannelRequestHandlerRegistration<?, ?, ?> handler) {
        if (handler.packetName() == null || handler.packetName().isBlank()) {
            throw new ZLinkConfigurationException(
                "client/server channel request handler packet name is required: " + name);
        }
        requestHandlers.add(handler);
    }

    void addPublishHandler(ChannelPublishHandlerRegistration<?, ?> handler) {
        if (handler.packetName() == null || handler.packetName().isBlank()) {
            throw new ZLinkConfigurationException(
                "fanout channel publish handler packet name is required: " + name);
        }
        publishHandlers.add(handler);
    }

    void validate(boolean discoveryEnabled) {
        if (kind == ChannelKind.CLIENT_SERVER) {
            validateClientServer(discoveryEnabled);
        } else if (kind == ChannelKind.FANOUT) {
            validateFanout(discoveryEnabled);
        }
    }

    private void validateClientServer(boolean discoveryEnabled) {
        if (clientEnabled && !discoveryEnabled && clientManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "client/server channel client requires discovery or manual connections: " + name);
        }
        if (clientEnabled && discoveryEnabled && !clientManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "client/server channel client cannot mix discovery and manual connections: " + name);
        }
        if (serverEnabled && serverBinds.isEmpty()) {
            throw new ZLinkConfigurationException(
                "client/server channel server requires at least one bind endpoint: " + name);
        }
        if (serverEnabled && requestHandlers.isEmpty()) {
            throw new ZLinkConfigurationException(
                "client/server channel server requires at least one request handler: " + name);
        }
        Set<String> packetNames = new HashSet<>();
        for (ChannelRequestHandlerRegistration<?, ?, ?> handler : requestHandlers) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    "duplicate client/server request handler packet name: "
                        + name + "/" + handler.packetName());
            }
        }
    }

    private void validateFanout(boolean discoveryEnabled) {
        if (publisherEnabled && publisherBinds.isEmpty()) {
            throw new ZLinkConfigurationException(
                "fanout channel publisher requires at least one bind endpoint: " + name);
        }
        if (subscriberEnabled && !discoveryEnabled && subscriberManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "fanout channel subscriber requires discovery or manual connections: " + name);
        }
        if (subscriberEnabled && discoveryEnabled && !subscriberManualEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException(
                "fanout channel subscriber cannot mix discovery and manual connections: " + name);
        }
        if (subscriberEnabled && publishHandlers.isEmpty()) {
            throw new ZLinkConfigurationException(
                "fanout channel subscriber requires at least one publish handler: " + name);
        }
        Set<String> packetNames = new HashSet<>();
        for (ChannelPublishHandlerRegistration<?, ?> handler : publishHandlers) {
            if (!packetNames.add(handler.packetName())) {
                throw new ZLinkConfigurationException(
                    "duplicate fanout publish handler packet name: "
                        + name + "/" + handler.packetName());
            }
        }
    }

    private static String requireEndpoint(String endpoint) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException("endpoint is required");
        }
        return endpoint;
    }
}
