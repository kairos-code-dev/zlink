package systems.zlink.framework.runtime.locations;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationResolver;
import systems.zlink.framework.runtime.backend.ZLinkBackendConnectableSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.spots.SpotNodeRegistration;

public final class ZLinkLocationAutoConnectHost implements AutoCloseable {
    public static final String ACTOR_HOST_CAPABILITY_METADATA_KEY =
        "zlink.framework.actor-host";
    public static final String SPOT_PUB_ENDPOINT_METADATA_KEY = "pub-endpoint";

    private final ZLinkLocationRuntime runtime;
    private final ZLinkPeerLocationResolver peers;
    private final ZLinkLocationOptions options;
    private final List<ZLinkAutoConnectLoop> loops = new ArrayList<>();

    public ZLinkLocationAutoConnectHost(
        ZLinkLocationRuntime runtime,
        ZLinkPeerLocationResolver peers,
        ZLinkLocationOptions options) {
        this.runtime = Objects.requireNonNull(runtime, "runtime");
        this.peers = Objects.requireNonNull(peers, "peers");
        this.options = Objects.requireNonNull(options, "options");
    }

    public CompletionStage<Void> start(
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        Map<String, ZLinkInternalSpotNode> spotNodesByName,
        ZLinkSpotRuntime spots) {
        Objects.requireNonNull(registration, "registration");
        Objects.requireNonNull(channels, "channels");
        Objects.requireNonNull(spotNodesByName, "spotNodesByName");

        List<ZLinkChannelRuntime.AutoConnectSurface> surfaces = channels.autoConnectSurfaces();
        for (ZLinkChannelRuntime.AutoConnectSurface surface : surfaces) {
            addChannelLoop(surface);
        }
        for (SpotNodeRegistration spot : registration.spotNodes()) {
            ZLinkInternalSpotNode node = spotNodesByName.get(spot.nodeName());
            if (node == null || !spot.routerEnabled()) {
                continue;
            }
            Map<String, String> metadata = new java.util.LinkedHashMap<>();
            if (spot.pubBind() != null) {
                metadata.put(SPOT_PUB_ENDPOINT_METADATA_KEY, spot.pubBind());
            }
            if (!spot.actorFactories().isEmpty()
                && !spot.entrySpots().isEmpty()
                && registration.streamNodes().isEmpty()) {
                metadata.put(ACTOR_HOST_CAPABILITY_METADATA_KEY, "true");
            }
            Set<String> manual = spot.routerManualConnections().stream()
                .map(SpotNodeRegistration.RouterManualConnection::endpoint)
                .collect(java.util.stream.Collectors.toUnmodifiableSet());
            addLoop(
                ZLinkLocationAutoConnectType.SPOT_MESH,
                spot.meshName(),
                ZLinkLocationRole.SPOT,
                node.routingId(),
                spot.routerBind() == null ? "" : spot.routerBind(),
                100,
                new SpotNodeExecutor(node, manual, spots),
                metadata.isEmpty() ? null : Map.copyOf(metadata));
        }

        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (ZLinkAutoConnectLoop loop : loops) {
            chain = chain.thenCompose(ignored -> loop.start());
        }
        return chain;
    }

    public CompletionStage<Void> stop() {
        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (ZLinkAutoConnectLoop loop : loops) {
            chain = chain.thenCompose(ignored -> loop.stop());
        }
        return chain.whenComplete((ignored, failure) -> loops.clear());
    }

    public CompletionStage<Void> markDraining() {
        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (ZLinkAutoConnectLoop loop : loops) {
            chain = chain.thenCompose(ignored -> loop.markDraining());
        }
        return chain;
    }

    private void addChannelLoop(ZLinkChannelRuntime.AutoConnectSurface surface) {
        ZLinkAutoConnectExecutor executor = surface.socket() == null
            ? ZLinkAutoConnectExecutor.NONE
            : new ConnectableSocketExecutor(surface.socket(), Set.copyOf(surface.manualEndpoints()));
        if (surface.socket() instanceof ZLinkBackendRouterSocket router
            && surface.type() == ZLinkLocationAutoConnectType.ROUTE_MESH) {
            executor = new RouteSocketExecutor(router, Set.copyOf(surface.manualEndpoints()));
        }
        addLoop(
            surface.type(),
            surface.meshName(),
            surface.role(),
            surface.nodeRid(),
            surface.endpoint(),
            surface.weight(),
            executor,
            null);
    }

    private void addLoop(
        ZLinkLocationAutoConnectType type,
        String meshName,
        ZLinkLocationRole role,
        RoutingId nodeRid,
        String endpoint,
        long weight,
        ZLinkAutoConnectExecutor executor,
        Map<String, String> metadata) {
        boolean advertisable = ZLinkAutoConnectPlanner.hasRid(nodeRid)
            || (endpoint != null && !endpoint.isBlank());
        if (!advertisable && executor == ZLinkAutoConnectExecutor.NONE) {
            return;
        }
        ZLinkAutoConnectPlanner.Local local =
            new ZLinkAutoConnectPlanner.Local(type, meshName, role, nodeRid, endpoint);
        ZLinkPeerLocation row = advertisable
            ? new ZLinkPeerLocation(
                type, meshName, nodeRid, role, endpoint, weight, false, 0,
                metadata, null, "", 0, Instant.EPOCH)
            : null;
        ZLinkAutoConnectReconciler reconciler = new ZLinkAutoConnectReconciler(
            local,
            row,
            runtime,
            peers,
            executor,
            options);
        loops.add(new ZLinkAutoConnectLoop(reconciler, options));
    }

    @Override
    public void close() {
        stop();
    }

    private static final class ConnectableSocketExecutor implements ZLinkAutoConnectExecutor {
        private final ZLinkBackendConnectableSocket socket;
        private final Set<String> manualEndpoints;

        ConnectableSocketExecutor(
            ZLinkBackendConnectableSocket socket,
            Set<String> manualEndpoints) {
            this.socket = socket;
            this.manualEndpoints = manualEndpoints;
        }

        @Override
        public boolean connect(ZLinkAutoConnectPlanner.Target target) {
            if (!manualEndpoints.contains(target.endpoint())) {
                try {
                    socket.connect(target.endpoint());
                } catch (RuntimeException failure) {
                    return false;
                }
            }
            return true;
        }

        @Override
        public boolean disconnect(ZLinkAutoConnectPlanner.Target target) {
            if (!manualEndpoints.contains(target.endpoint())) {
                try {
                    socket.disconnect(target.endpoint());
                } catch (RuntimeException failure) {
                    return false;
                }
            }
            return true;
        }

    }

    private static final class RouteSocketExecutor implements ZLinkAutoConnectExecutor {
        private final ZLinkBackendRouterSocket socket;
        private final Set<String> manualEndpoints;
        private final Object connectGate = new Object();

        RouteSocketExecutor(
            ZLinkBackendRouterSocket socket,
            Set<String> manualEndpoints) {
            this.socket = socket;
            this.manualEndpoints = manualEndpoints;
        }

        @Override
        public boolean isManual(ZLinkAutoConnectPlanner.Target target) {
            return manualEndpoints.contains(target.endpoint());
        }

        @Override
        public boolean connect(ZLinkAutoConnectPlanner.Target target) {
            if (manualEndpoints.contains(target.endpoint())) {
                return true;
            }
            try {
                if (ZLinkAutoConnectPlanner.hasRid(target.nodeRid())) {
                    synchronized (connectGate) {
                        socket.setConnectRoutingId(target.nodeRid());
                        socket.setProbe(true);
                        socket.connect(target.endpoint());
                    }
                    return true;
                }
                socket.connect(target.endpoint());
                return true;
            } catch (RuntimeException failure) {
                return false;
            }
        }

        @Override
        public boolean disconnect(ZLinkAutoConnectPlanner.Target target) {
            if (!manualEndpoints.contains(target.endpoint())) {
                try {
                    socket.disconnect(target.endpoint());
                } catch (RuntimeException failure) {
                    return false;
                }
            }
            return true;
        }

        @Override
        public boolean replace(
            ZLinkAutoConnectPlanner.Target current,
            ZLinkAutoConnectPlanner.Target replacement) {
            try {
                socket.disconnect(current.endpoint());
                connectReplacement(replacement);
                return true;
            } catch (RuntimeException failure) {
                return false;
            }
        }

        private void connectReplacement(ZLinkAutoConnectPlanner.Target target) {
            if (ZLinkAutoConnectPlanner.hasRid(target.nodeRid())) {
                synchronized (connectGate) {
                    socket.setConnectRoutingId(target.nodeRid());
                    socket.setProbe(true);
                    socket.connect(target.endpoint());
                }
                return;
            }
            socket.connect(target.endpoint());
        }
    }

    private static final class SpotNodeExecutor implements ZLinkAutoConnectExecutor {
        private final ZLinkInternalSpotNode node;
        private final Set<String> manualEndpoints;
        private final ZLinkSpotRuntime spots;

        SpotNodeExecutor(
            ZLinkInternalSpotNode node,
            Set<String> manualEndpoints,
            ZLinkSpotRuntime spots) {
            this.node = node;
            this.manualEndpoints = manualEndpoints;
            this.spots = spots;
        }

        @Override
        public boolean connect(ZLinkAutoConnectPlanner.Target target) {
            if (manualEndpoints.contains(target.endpoint())) {
                return true;
            }
            try {
                if (ZLinkAutoConnectPlanner.hasRid(target.nodeRid())) {
                    node.connectPeer(target.nodeRid(), target.endpoint());
                    if (spots != null) {
                        spots.markAutoConnectedRouterPeer(target.nodeRid());
                    }
                } else {
                    node.connectPeer(target.endpoint());
                }
                String pubEndpoint = pubEndpointOf(target);
                if (pubEndpoint != null) {
                    node.connectPeer(pubEndpoint);
                }
                return true;
            } catch (RuntimeException failure) {
                return false;
            }
        }

        @Override
        public boolean disconnect(ZLinkAutoConnectPlanner.Target target) {
            if (manualEndpoints.contains(target.endpoint())) {
                return true;
            }
            try {
                if (ZLinkAutoConnectPlanner.hasRid(target.nodeRid())) {
                    node.disconnectPeer(target.nodeRid());
                    if (spots != null) {
                        spots.unmarkAutoConnectedRouterPeer(target.nodeRid());
                    }
                } else {
                    node.disconnectPeer(target.endpoint());
                }
                String pubEndpoint = pubEndpointOf(target);
                if (pubEndpoint != null) {
                    node.disconnectPeer(pubEndpoint);
                }
                return true;
            } catch (RuntimeException failure) {
                return false;
            }
        }

        private static String pubEndpointOf(ZLinkAutoConnectPlanner.Target target) {
            if (target.metadata() == null) {
                return null;
            }
            String endpoint = target.metadata().get(SPOT_PUB_ENDPOINT_METADATA_KEY);
            return endpoint == null || endpoint.isBlank() ? null : endpoint;
        }
    }

}
