package systems.zlink.framework.runtime.channels;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkClientServerServerDescriptor;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.runtime.backend.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;
import systems.zlink.framework.runtime.backend.ZLinkBackendPublisherSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRouteBridge;
import systems.zlink.framework.runtime.backend.ZLinkBackendSubscriberSocket;

final class ZLinkChannelSocketRegistry {
    private final Map<String, ChannelRegistration> registrations = new HashMap<>();
    private final Map<String, ZLinkBackendDealerSocket> clients = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> servers = new HashMap<>();
    private final Map<String, RoutingId> serverRoutingIds = new HashMap<>();
    private final Map<String, ZLinkBackendPublisherSocket> publishers = new HashMap<>();
    private final Map<String, ZLinkBackendSubscriberSocket> subscribers = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> routeRouters = new HashMap<>();
    private final Map<String, ClientServerConnection> clientServerConnections =
        new HashMap<>();
    private final Map<String, ZLinkClientServerServerDescriptor>
        clientServerServerDescriptors = new HashMap<>();
    private final Map<String, AtomicLong> clientServerSelectionCursors =
        new HashMap<>();
    private final Map<String, Object> routeSocketLocks = new HashMap<>();
    private final Map<String, ZLinkBackendSpotRouteBridge> spotRouteBridges =
        new ConcurrentHashMap<>();
    private final Map<String, ZLinkInternalSpotNode> spotRouterNodes = new HashMap<>();
    private final List<ZLinkBackendObject> ownedSockets = new ArrayList<>();
    private boolean legacyClientFallback;

    void registerChannel(ChannelRegistration registration) {
        registrations.put(registration.name(), registration);
    }

    ChannelRegistration registration(String channelName) {
        return registrations.get(channelName);
    }

    void registerClient(String channelName, ZLinkBackendDealerSocket socket) {
        clients.put(channelName, socket);
        ownedSockets.add(socket);
    }

    void registerServer(
        String channelName,
        RoutingId routingId,
        ZLinkBackendRouterSocket socket) {
        servers.put(channelName, socket);
        serverRoutingIds.put(channelName, routingId);
        ownedSockets.add(socket);
    }

    void registerPublisher(String channelName, ZLinkBackendPublisherSocket socket) {
        publishers.put(channelName, socket);
        ownedSockets.add(socket);
    }

    void registerSubscriber(String channelName, ZLinkBackendSubscriberSocket socket) {
        subscribers.put(channelName, socket);
        ownedSockets.add(socket);
    }

    void registerRouteRouter(String channelName, ZLinkBackendRouterSocket socket) {
        routeRouters.put(channelName, socket);
        routeSocketLocks.put(channelName, new Object());
        ownedSockets.add(socket);
    }

    ZLinkBackendDealerSocket client(String channelName) {
        return clients.get(channelName);
    }

    synchronized ZLinkBackendDealerSocket clientForOutbound(String channelName) {
        List<ClientServerConnection> eligible = clientServerConnections.values()
            .stream()
            .filter(connection -> connection.ready()
                && connection.descriptor().channelName().equals(channelName)
                && connection.descriptor().weight() > 0
                && connection.descriptor().state()
                    == systems.zlink.framework.runtime.host
                        .ZLinkFrameworkRuntimeState.SERVING)
            .sorted(java.util.Comparator.comparing(
                connection -> connection.descriptor().serverRid().toHex()))
            .toList();
        long total = eligible.stream()
            .mapToLong(connection -> connection.descriptor().weight())
            .sum();
        if (total == 0) {
            return legacyClientFallback ? clients.get(channelName) : null;
        }
        AtomicLong cursor = clientServerSelectionCursors.computeIfAbsent(
            channelName, ignored -> new AtomicLong());
        long selected = Math.floorMod(cursor.getAndIncrement(), total);
        long offset = 0;
        for (ClientServerConnection connection : eligible) {
            offset += connection.descriptor().weight();
            if (selected < offset) {
                return connection.dealer();
            }
        }
        throw new IllegalStateException(
            "ClientServer weighted selection did not select a connection");
    }

    synchronized void enableLegacyClientFallback() {
        legacyClientFallback = true;
    }

    synchronized void addClientServerConnection(
        String connectionId,
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkBackendDealerSocket dealer) {
        clientServerConnections.put(
            connectionId,
            new ClientServerConnection(connectionId, descriptor, dealer, false));
        ownedSockets.add(dealer);
    }

    synchronized boolean admitClientServerConnection(
        String connectionId,
        ZLinkClientServerServerDescriptor descriptor) {
        ClientServerConnection current =
            clientServerConnections.get(connectionId);
        if (current == null) {
            return false;
        }
        clientServerConnections.put(
            connectionId,
            new ClientServerConnection(
                connectionId, descriptor, current.dealer(), true));
        return true;
    }

    synchronized void updateClientServerConnection(
        String connectionId,
        ZLinkClientServerServerDescriptor descriptor,
        boolean ready) {
        ClientServerConnection current =
            clientServerConnections.get(connectionId);
        if (current != null) {
            clientServerConnections.put(
                connectionId,
                new ClientServerConnection(
                    connectionId,
                    descriptor,
                    current.dealer(),
                    ready));
        }
    }

    synchronized void removeClientServerConnection(String connectionId) {
        ClientServerConnection current =
            clientServerConnections.remove(connectionId);
        if (current == null) {
            return;
        }
        ownedSockets.remove(current.dealer());
        try {
            current.dealer().close();
        } catch (systems.zlink.contracts.errors.ZlinkCloseException ignored) {
        }
    }

    synchronized void setClientServerServerDescriptor(
        String channelName,
        ZLinkClientServerServerDescriptor descriptor) {
        if (descriptor == null) {
            clientServerServerDescriptors.remove(channelName);
        } else {
            clientServerServerDescriptors.put(channelName, descriptor);
        }
    }

    synchronized ZLinkClientServerServerDescriptor
        clientServerServerDescriptor(String channelName) {
        return clientServerServerDescriptors.get(channelName);
    }

    synchronized int clientServerServerWeight(
        String channelName,
        int fallback) {
        ZLinkBackendRouterSocket server = servers.get(channelName);
        return server == null ? fallback : server.peerWeight();
    }

    synchronized void initializeClientServerServerDescriptors(String ownerId) {
        for (Map.Entry<String, ZLinkBackendRouterSocket> entry
            : servers.entrySet()) {
            ChannelRegistration registration =
                registrations.get(entry.getKey());
            if (registration == null
                || registration.serverBinds().isEmpty()) {
                continue;
            }
            String endpoint = advertisedEndpoint(
                registration.serverBinds().get(0), entry.getValue());
            clientServerServerDescriptors.put(
                entry.getKey(),
                new ZLinkClientServerServerDescriptor(
                    entry.getKey(),
                    serverRoutingIds.get(entry.getKey()),
                    java.util.concurrent.ThreadLocalRandom.current()
                        .nextLong(1, Long.MAX_VALUE),
                    1,
                    endpoint,
                    entry.getValue().peerWeight(),
                    systems.zlink.framework.runtime.host
                        .ZLinkFrameworkRuntimeState.SERVING,
                    "default",
                    ownerId,
                    1,
                    java.time.Instant.EPOCH));
        }
    }

    boolean tryHandleClientServerControl(
        String channelName,
        ZLinkBackendRouterSocket router,
        systems.zlink.framework.runtime.backend.ZLinkBackendReceived received) {
        if (received.parts().isEmpty()) {
            return false;
        }
        byte[] frame = received.parts().get(0).toByteArray();
        if (!ZLinkClientServerServiceWire.isControlFrame(frame)) {
            return false;
        }
        byte[] reply;
        try {
            ZLinkClientServerServiceWire.Control control =
                ZLinkClientServerServiceWire.decode(frame);
            ZLinkClientServerServerDescriptor descriptor;
            synchronized (this) {
                descriptor = clientServerServerDescriptors.get(channelName);
            }
            if (!(control instanceof ZLinkClientServerServiceWire.Hello hello)
                || descriptor == null
                || !hello.channelName().equals(channelName)
                || !hello.securityIdentity().equals(
                    descriptor.securityIdentity())) {
                reply = ZLinkClientServerServiceWire.encodeReject(2);
            } else {
                reply = ZLinkClientServerServiceWire.encodeAdmit(
                    descriptor,
                    normalizedMessageLimit(router.maxMessageSize()));
            }
        } catch (RuntimeException failure) {
            reply = ZLinkClientServerServiceWire.encodeReject(1);
        }
        try (Message message = Message.from(reply)) {
            received.reply(List.of(message));
        }
        received.close();
        return true;
    }

    ZLinkBackendRouterSocket server(String channelName) {
        return servers.get(channelName);
    }

    ZLinkBackendPublisherSocket publisher(String channelName) {
        return publishers.get(channelName);
    }

    ZLinkBackendSubscriberSocket subscriber(String channelName) {
        return subscribers.get(channelName);
    }

    ZLinkBackendRouterSocket routeRouter(String channelName) {
        return routeRouters.get(channelName);
    }

    Object routeSocketLock(String channelName, Object fallback) {
        return routeSocketLocks.getOrDefault(channelName, fallback);
    }

    Map<String, ZLinkBackendSpotRouteBridge> spotRouteBridges() {
        return spotRouteBridges;
    }

    ZLinkBackendSpotRouteBridge spotRouteBridge(String channelName) {
        return spotRouteBridges.get(channelName);
    }

    void registerSpotRouteBridge(String channelName, ZLinkBackendSpotRouteBridge bridge) {
        spotRouteBridges.put(channelName, bridge);
    }

    List<String> spotRouteBridgeChannelNames() {
        return List.copyOf(spotRouteBridges.keySet());
    }

    void registerSpotRouterNode(String channelName, ZLinkInternalSpotNode node) {
        spotRouterNodes.put(channelName, node);
    }

    ZLinkInternalSpotNode spotRouterNode(String channelName) {
        return spotRouterNodes.get(channelName);
    }

    Map<String, ZLinkBackendSocket> monitoringSocketSources() {
        Map<String, ZLinkBackendSocket> sources = new HashMap<>();
        sources.putAll(clients);
        sources.putAll(servers);
        sources.putAll(publishers);
        sources.putAll(subscribers);
        sources.putAll(routeRouters);
        return Map.copyOf(sources);
    }

    List<ZLinkChannelRuntime.AutoConnectSurface> autoConnectSurfaces() {
        List<ZLinkChannelRuntime.AutoConnectSurface> surfaces = new ArrayList<>();
        for (ChannelRegistration channel : registrations.values()) {
            addAutoConnectSurfaces(channel, surfaces);
        }
        return List.copyOf(surfaces);
    }

    void closeSpotRouteBridges() {
        closeAll(spotRouteBridges.values(), java.util.Collections.newSetFromMap(
            new IdentityHashMap<>()));
        spotRouteBridges.clear();
    }

    void closeAll() {
        for (ChannelRegistration registration : registrations.values()) {
            registration.detachRuntimeConnections();
        }
        closeAll(ownedSockets, java.util.Collections.newSetFromMap(new IdentityHashMap<>()));
        ownedSockets.clear();
        clientServerConnections.clear();
        clientServerServerDescriptors.clear();
    }

    private void addAutoConnectSurfaces(
        ChannelRegistration channel,
        List<ZLinkChannelRuntime.AutoConnectSurface> surfaces) {
        if (channel.kind() == ChannelKind.CLIENT_SERVER) {
            addClientServerSurfaces(channel, surfaces);
        } else if (channel.kind() == ChannelKind.FANOUT) {
            addFanoutSurfaces(channel, surfaces);
        } else if (channel.kind() == ChannelKind.ROUTE_MESH) {
            addRouteSurfaces(channel, surfaces);
        }
    }

    private void addClientServerSurfaces(
        ChannelRegistration channel,
        List<ZLinkChannelRuntime.AutoConnectSurface> surfaces) {
        ZLinkBackendRouterSocket server = servers.get(channel.name());
        if (server != null) {
            for (String endpoint : channel.serverBinds()) {
                surfaces.add(new ZLinkChannelRuntime.AutoConnectSurface(
                    ZLinkLocationAutoConnectType.CLIENT_SERVER,
                    channel.name(),
                    ZLinkLocationRole.ROUTER,
                    serverRoutingIds.get(channel.name()),
                    advertisedEndpoint(endpoint, server),
                    server.peerWeight(),
                    null,
                    List.of()));
            }
        }
        ZLinkBackendDealerSocket client = clients.get(channel.name());
        if (client != null) {
            surfaces.add(new ZLinkChannelRuntime.AutoConnectSurface(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                channel.name(),
                ZLinkLocationRole.DEALER,
                channel.routingId(),
                "",
                100,
                client,
                channel.clientManualEndpoints()));
        }
    }

    private void addFanoutSurfaces(
        ChannelRegistration channel,
        List<ZLinkChannelRuntime.AutoConnectSurface> surfaces) {
        if (publishers.containsKey(channel.name())) {
            for (String endpoint : channel.publisherBinds()) {
                surfaces.add(new ZLinkChannelRuntime.AutoConnectSurface(
                    ZLinkLocationAutoConnectType.FANOUT,
                    channel.name(),
                    ZLinkLocationRole.PUB,
                    channel.routingId(),
                    endpoint,
                    100,
                    null,
                    List.of()));
            }
        }
        ZLinkBackendSubscriberSocket subscriber = subscribers.get(channel.name());
        if (subscriber != null && channel.automaticSubscriberEnabled()) {
            surfaces.add(new ZLinkChannelRuntime.AutoConnectSurface(
                ZLinkLocationAutoConnectType.FANOUT,
                channel.name(),
                ZLinkLocationRole.SUB,
                channel.routingId(),
                "",
                100,
                subscriber,
                channel.subscriberManualEndpoints()));
        }
    }

    private void addRouteSurfaces(
        ChannelRegistration channel,
        List<ZLinkChannelRuntime.AutoConnectSurface> surfaces) {
        ZLinkBackendRouterSocket router = routeRouters.get(channel.name());
        if (router == null) {
            return;
        }
        for (String endpoint : channel.routeBinds()) {
            surfaces.add(new ZLinkChannelRuntime.AutoConnectSurface(
                ZLinkLocationAutoConnectType.ROUTE_MESH,
                channel.name(),
                ZLinkLocationRole.ROUTER,
                channel.routeRoutingId(),
                advertisedEndpoint(endpoint, router),
                router.peerWeight(),
                router,
                channel.routeManualEndpoints()));
        }
        if (channel.routeBinds().isEmpty()) {
            surfaces.add(new ZLinkChannelRuntime.AutoConnectSurface(
                ZLinkLocationAutoConnectType.ROUTE_MESH,
                channel.name(),
                ZLinkLocationRole.ROUTER,
                channel.routeRoutingId(),
                "",
                router.peerWeight(),
                router,
                channel.routeManualEndpoints()));
        }
    }

    private static String advertisedEndpoint(
        String configuredEndpoint,
        ZLinkBackendRouterSocket router) {
        if (!configuredEndpoint.endsWith(":0")) {
            return configuredEndpoint;
        }
        String boundEndpoint = router.lastEndpoint();
        return boundEndpoint == null || boundEndpoint.isBlank()
            ? configuredEndpoint
            : boundEndpoint;
    }

    private static void closeAll(
        Iterable<? extends ZLinkBackendObject> closeables,
        Set<ZLinkBackendObject> closed) {
        for (ZLinkBackendObject closeable : closeables) {
            if (closeable != null && closed.add(closeable)) {
                try {
                    closeable.close();
                } catch (systems.zlink.contracts.errors.ZlinkCloseException ignored) {
                }
            }
        }
    }

    private static int normalizedMessageLimit(long configured) {
        return configured > 0 && configured <= Integer.MAX_VALUE
            ? (int) configured
            : Integer.MAX_VALUE;
    }

    private record ClientServerConnection(
        String connectionId,
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkBackendDealerSocket dealer,
        boolean ready) {
    }
}
