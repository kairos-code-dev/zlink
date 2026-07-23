package systems.zlink.framework.runtime.channels;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
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
    private final Map<String, ZLinkBackendPublisherSocket> publishers = new HashMap<>();
    private final Map<String, ZLinkBackendSubscriberSocket> subscribers = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> routeRouters = new HashMap<>();
    private final Map<String, Object> routeSocketLocks = new HashMap<>();
    private final Map<String, ZLinkBackendSpotRouteBridge> spotRouteBridges =
        new ConcurrentHashMap<>();
    private final Map<String, ZLinkInternalSpotNode> spotRouterNodes = new HashMap<>();
    private final List<ZLinkBackendObject> ownedSockets = new ArrayList<>();

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

    void registerServer(String channelName, ZLinkBackendRouterSocket socket) {
        servers.put(channelName, socket);
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
                    channel.routingId(),
                    endpoint,
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
}
