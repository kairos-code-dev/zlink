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
    private final List<ChannelRequestHandlerRegistration<?, ?, ?>> requestHandlers = new ArrayList<>();
    private boolean clientEnabled;
    private boolean serverEnabled;

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

    boolean clientEnabled() {
        return clientEnabled;
    }

    void enableClient() {
        clientEnabled = true;
    }

    void enableServer() {
        serverEnabled = true;
    }

    void addServerBind(String endpoint) {
        serverBinds.add(requireEndpoint(endpoint));
    }

    void addClientManualEndpoint(String endpoint) {
        clientManualEndpoints.add(requireEndpoint(endpoint));
    }

    void addRequestHandler(ChannelRequestHandlerRegistration<?, ?, ?> handler) {
        if (handler.packetName() == null || handler.packetName().isBlank()) {
            throw new ZLinkConfigurationException(
                "client/server channel request handler packet name is required: " + name);
        }
        requestHandlers.add(handler);
    }

    void validate(boolean discoveryEnabled) {
        if (kind == ChannelKind.CLIENT_SERVER) {
            validateClientServer(discoveryEnabled);
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

    private static String requireEndpoint(String endpoint) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException("endpoint is required");
        }
        return endpoint;
    }
}
