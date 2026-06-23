package systems.zlink.framework.runtime.spots;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ConnectResult;
import systems.zlink.contracts.errors.ZlinkConnectException;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.framework.runtime.backend.ZLinkBackendDiscovery;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryMemberPeerEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;

final class SpotDiscoveryReconciler {
    private static final long ROUTER_ENDPOINT_ROUTE_KIND = 0x5a4c5245L;
    private static final int ROUTER_PEER_CONVERGENCE_SNAPSHOTS = 10;

    private final List<Binding> bindings = new ArrayList<>();
    private final Set<String> discoveredPubSubPeers = new HashSet<>();
    private final Set<String> discoveredRouterPeers = new HashSet<>();
    private final Map<String, Integer> discoveredRouterPeerSightings = new HashMap<>();
    private final Map<String, Set<String>> discoveredRouterPeerRidKeys = new HashMap<>();
    private final Set<RoutingId> connectedRouterPeerRids = ConcurrentHashMap.newKeySet();

    void addBinding(
        String meshName,
        ZLinkBackendSpotNode node,
        ZLinkBackendDiscovery discovery,
        boolean routerEnabled,
        boolean pubSubEnabled,
        String routerBind,
        String pubBind) {
        bindings.add(new Binding(
            meshName,
            node,
            discovery,
            routerEnabled,
            pubSubEnabled,
            routerBind,
            pubBind));
    }

    void markRouterPeerReady(RoutingId peerRid) {
        if (hasRoutingId(peerRid)) {
            connectedRouterPeerRids.add(peerRid);
        }
    }

    boolean isRouterPeerReady(RoutingId peerRid) {
        return connectedRouterPeerRids.contains(peerRid);
    }

    void start(ScheduledExecutorService executor) {
        if (bindings.isEmpty()) {
            return;
        }
        executor.scheduleWithFixedDelay(
            this::reconcile,
            0,
            100,
            TimeUnit.MILLISECONDS);
    }

    private void reconcile() {
        for (Binding binding : bindings) {
            try {
                reconcile(binding);
            } catch (RuntimeException ignored) {
                // Discovery can be temporarily unavailable while registry peers are starting.
                // Keep the reconciler alive so the next tick can converge.
            }
        }
    }

    private void reconcile(Binding binding) {
        bindLocalRouterEndpoint(binding);
        List<ZLinkBackendRegistryMemberPeerEntry> peers = binding.discovery().memberPeers();
        Map<RoutingId, String> routerEndpointsByRid =
            routerEndpointsByRid(peers, binding.meshName());
        for (ZLinkBackendRegistryMemberPeerEntry peer : peers) {
            if (peer.autoConnectType() != AutoConnectType.SPOT_MESH
                || !binding.meshName().equals(peer.channelName())
                || peer.endpoint() == null
                || peer.endpoint().isBlank()
                || peer.endpoint().equals(binding.routerBind())
                || peer.endpoint().equals(binding.pubBind())) {
                continue;
            }
            if (peer.serviceRole() == ServiceRole.ROUTER) {
                if (binding.routerEnabled() && !binding.pubSubEnabled()) {
                    connectDiscoveredRouterPeer(binding, peer.routingId(), peer.endpoint());
                }
                continue;
            }
            if (peer.serviceRole() != ServiceRole.SPOT) {
                continue;
            }
            if (binding.pubSubEnabled()) {
                connectDiscoveredPubSubPeer(binding, peer.endpoint());
            }
            if (!binding.routerEnabled()) {
                continue;
            }
            RoutingId peerRoutingId = peer.routingId();
            String routerEndpoint = resolveRouterEndpoint(
                binding,
                peerRoutingId,
                peer.endpoint(),
                routerEndpointsByRid,
                binding.discovery());
            if (routerEndpoint == null
                || routerEndpoint.isBlank()
                || routerEndpoint.equals(binding.routerBind())
                || routerEndpoint.equals(binding.pubBind())) {
                continue;
            }
            connectDiscoveredRouterPeer(binding, peerRoutingId, routerEndpoint);
        }
    }

    private static Map<RoutingId, String> routerEndpointsByRid(
        List<ZLinkBackendRegistryMemberPeerEntry> peers,
        String meshName) {
        Map<RoutingId, String> endpoints = new HashMap<>();
        for (ZLinkBackendRegistryMemberPeerEntry peer : peers) {
            if (peer.autoConnectType() == AutoConnectType.SPOT_MESH
                && peer.serviceRole() == ServiceRole.ROUTER
                && meshName.equals(peer.channelName())
                && peer.endpoint() != null
                && !peer.endpoint().isBlank()
                && hasRoutingId(peer.routingId())) {
                endpoints.put(peer.routingId(), peer.endpoint());
            }
        }
        return endpoints;
    }

    private static String resolveRouterEndpoint(
        Binding binding,
        RoutingId peerRoutingId,
        String peerEndpoint,
        Map<RoutingId, String> routerEndpointsByRid,
        ZLinkBackendDiscovery discovery) {
        if (!hasRoutingId(peerRoutingId)) {
            return binding.pubSubEnabled() ? null : peerEndpoint;
        }
        String discoveredEndpoint = routerEndpointsByRid.get(peerRoutingId);
        if (discoveredEndpoint != null) {
            return discoveredEndpoint;
        }
        String routeEndpoint = resolveRouterEndpointRoute(discovery, peerRoutingId);
        if (routeEndpoint != null && !routeEndpoint.isBlank()) {
            return routeEndpoint;
        }
        return binding.pubSubEnabled() ? null : peerEndpoint;
    }

    private static void bindLocalRouterEndpoint(Binding binding) {
        if (!binding.routerEnabled()
            || binding.routerBind() == null
            || binding.routerBind().isBlank()
            || !hasRoutingId(binding.node().routingId())) {
            return;
        }
        try {
            binding.discovery().bindRoute(
                ROUTER_ENDPOINT_ROUTE_KIND,
                binding.node().routingId().toBytes(),
                binding.routerBind().getBytes(StandardCharsets.UTF_8));
        } catch (RuntimeException ignored) {
            // Route publication converges through the next discovery tick.
        }
    }

    private static String resolveRouterEndpointRoute(
        ZLinkBackendDiscovery discovery,
        RoutingId peerRoutingId) {
        try {
            return discovery.resolveRoute(
                    ROUTER_ENDPOINT_ROUTE_KIND,
                    peerRoutingId.toBytes())
                .endpoint()
                .orElse(null);
        } catch (RuntimeException ex) {
            return null;
        }
    }

    private void connectDiscoveredPubSubPeer(Binding binding, String endpoint) {
        String key = binding.meshName() + "|pubsub|" + endpoint;
        if (!discoveredPubSubPeers.add(key)) {
            return;
        }
        try {
            binding.node().connectPeer(endpoint);
        } catch (ZlinkConnectException ex) {
            if (isIdempotentConnectFailure(ex)) {
                return;
            }
            discoveredPubSubPeers.remove(key);
        } catch (RuntimeException ex) {
            discoveredPubSubPeers.remove(key);
        }
    }

    private void connectDiscoveredRouterPeer(
        Binding binding,
        RoutingId peerRoutingId,
        String endpoint) {
        String key = binding.meshName() + "|router|" + endpoint;
        String ridKey = hasRoutingId(peerRoutingId) ? peerRoutingId.toString() : null;
        if (!discoveredRouterPeers.add(key)) {
            int sightings = discoveredRouterPeerSightings.merge(key, 1, Integer::sum);
            if (ridKey != null
                && discoveredRouterPeerRidKeys
                    .computeIfAbsent(key, ignored -> new HashSet<>())
                    .add(ridKey)) {
                try {
                    binding.node().connectPeer(peerRoutingId, endpoint);
                } catch (ZlinkConnectException ex) {
                    if (!isIdempotentConnectFailure(ex)) {
                        removeDiscoveredRouterPeerRidKey(key, ridKey);
                        return;
                    }
                } catch (RuntimeException ex) {
                    removeDiscoveredRouterPeerRidKey(key, ridKey);
                    return;
                }
            }
            if (hasRoutingId(peerRoutingId)
                && sightings >= ROUTER_PEER_CONVERGENCE_SNAPSHOTS) {
                connectedRouterPeerRids.add(peerRoutingId);
            }
            return;
        }
        discoveredRouterPeerSightings.put(key, 1);
        try {
            if (hasRoutingId(peerRoutingId)) {
                binding.node().connectPeer(peerRoutingId, endpoint);
                discoveredRouterPeerRidKeys
                    .computeIfAbsent(key, ignored -> new HashSet<>())
                    .add(ridKey);
            } else {
                binding.node().connectPeer(endpoint);
            }
            if (hasRoutingId(peerRoutingId)) {
                connectedRouterPeerRids.add(peerRoutingId);
            }
        } catch (ZlinkConnectException ex) {
            if (isIdempotentConnectFailure(ex)) {
                int sightings = discoveredRouterPeerSightings.merge(key, 1, Integer::sum);
                if (hasRoutingId(peerRoutingId)
                    && sightings >= ROUTER_PEER_CONVERGENCE_SNAPSHOTS) {
                    connectedRouterPeerRids.add(peerRoutingId);
                }
                return;
            }
            discoveredRouterPeers.remove(key);
            discoveredRouterPeerSightings.remove(key);
            discoveredRouterPeerRidKeys.remove(key);
        } catch (RuntimeException ex) {
            discoveredRouterPeers.remove(key);
            discoveredRouterPeerSightings.remove(key);
            discoveredRouterPeerRidKeys.remove(key);
        }
    }

    private void removeDiscoveredRouterPeerRidKey(String key, String ridKey) {
        Set<String> ridKeys = discoveredRouterPeerRidKeys.get(key);
        if (ridKeys == null) {
            return;
        }
        ridKeys.remove(ridKey);
        if (ridKeys.isEmpty()) {
            discoveredRouterPeerRidKeys.remove(key);
        }
    }

    static boolean hasRoutingId(RoutingId routingId) {
        return routingId != null && routingId.size() > 0;
    }

    private static boolean isIdempotentConnectFailure(ZlinkConnectException ex) {
        int errno = ex.getNativeErrno();
        return ex.getResult() == ConnectResult.BUSY
            || errno == 16
            || errno == 106
            || errno == 114
            || errno == 115;
    }

    private record Binding(
        String meshName,
        ZLinkBackendSpotNode node,
        ZLinkBackendDiscovery discovery,
        boolean routerEnabled,
        boolean pubSubEnabled,
        String routerBind,
        String pubBind) {
    }
}
