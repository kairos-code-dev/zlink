package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.runtime.internal.locations.ZLinkAutoConnectPeer;
import systems.zlink.framework.runtime.internal.locations.ZLinkAutoConnectType;

final class ZLinkAutoConnectPlannerTest {
    @Test
    void actorCapabilitiesUseExactConfiguredActorTypes() {
        assertEquals(
            List.of("actor:enemy", "actor:player"),
            ZLinkLocationAutoConnectHost.actorCapabilities(
                List.of("player", "enemy", "player")));
    }

    @Test
    void routeMeshUsesUnidirectionalInitiatorOrderingAndKeepsSelfExclusion() {
        var lower = local(
            ZLinkAutoConnectType.ROUTE_MESH,
            ZLinkLocationRole.ROUTER,
            "route-a-local",
            "inproc://route-a");
        var higher = peer(
            ZLinkAutoConnectType.ROUTE_MESH,
            ZLinkLocationRole.ROUTER,
            "route-z-remote",
            "inproc://route-z");

        assertTrue(hasTarget(lower, higher));

        var reverse = local(
            ZLinkAutoConnectType.ROUTE_MESH,
            ZLinkLocationRole.ROUTER,
            "route-z-local",
            "inproc://route-z");
        var lowerPeer = peer(
            ZLinkAutoConnectType.ROUTE_MESH,
            ZLinkLocationRole.ROUTER,
            "route-a-remote",
            "inproc://route-a");

        assertFalse(hasTarget(reverse, lowerPeer));
        assertFalse(ZLinkAutoConnectPlanner.computeDesired(lower, List.of(peer(
            ZLinkAutoConnectType.ROUTE_MESH,
            ZLinkLocationRole.ROUTER,
            "route-a-local",
            "inproc://other"))).containsKey(targetKey(ZLinkLocationRole.ROUTER, "route-a-local")));
        assertFalse(ZLinkAutoConnectPlanner.computeDesired(lower, List.of(peer(
            ZLinkAutoConnectType.ROUTE_MESH,
            ZLinkLocationRole.ROUTER,
            "route-other",
            "inproc://route-a"))).containsKey(targetKey(ZLinkLocationRole.ROUTER, "route-other")));
    }

    @Test
    void spotMeshDialsAllSpotPeersSoPubSubSubscriptionsPropagate() {
        var lower = local(
            ZLinkAutoConnectType.SPOT_MESH,
            ZLinkLocationRole.SPOT,
            "spot-a-local",
            "inproc://spot-a");
        var higher = peer(
            ZLinkAutoConnectType.SPOT_MESH,
            ZLinkLocationRole.SPOT,
            "spot-z-remote",
            "inproc://spot-z");
        var reverse = local(
            ZLinkAutoConnectType.SPOT_MESH,
            ZLinkLocationRole.SPOT,
            "spot-z-local",
            "inproc://spot-z");
        var lowerPeer = peer(
            ZLinkAutoConnectType.SPOT_MESH,
            ZLinkLocationRole.SPOT,
            "spot-a-remote",
            "inproc://spot-a");

        assertTrue(hasTarget(lower, higher));
        assertTrue(hasTarget(reverse, lowerPeer));
    }

    @Test
    void connectionIntentIdentityIncludesLifecycleGeneration() {
        var local = local(
            ZLinkAutoConnectType.CLIENT_SERVER,
            ZLinkLocationRole.DEALER,
            "client-local",
            "");
        var first = peer(
            ZLinkAutoConnectType.CLIENT_SERVER,
            ZLinkLocationRole.ROUTER,
            "server",
            "inproc://server");
        var replacement = new ZLinkAutoConnectPeer(
            first.autoConnectType(),
            first.meshName(),
            first.nodeRid(),
            first.role(),
            first.endpoint(),
            first.weight(),
            first.draining(),
            2,
            first.metadata(),
            first.capabilities(),
            "replacement-owner",
            2,
            first.updatedAt());

        var desired = ZLinkAutoConnectPlanner.computeDesired(
            local,
            List.of(first, replacement));

        assertEquals(2, desired.size());
        assertTrue(desired.containsKey(targetKey(
            ZLinkLocationRole.ROUTER,
            first.nodeRid(),
            1)));
        assertTrue(desired.containsKey(targetKey(
            ZLinkLocationRole.ROUTER,
            first.nodeRid(),
            2)));
    }

    @Test
    void connectionIntentUsesDescriptorLifecycleInsteadOfStoreGeneration() {
        var local = local(
            ZLinkAutoConnectType.CLIENT_SERVER,
            ZLinkLocationRole.DEALER,
            "client-local",
            "");
        var descriptor = new ZLinkAutoConnectPeer(
            ZLinkAutoConnectType.CLIENT_SERVER,
            "mesh",
            RoutingId.from("server"),
            ZLinkLocationRole.ROUTER,
            "inproc://server",
            100,
            false,
            73,
            Map.of(),
            List.of(),
            "server-owner",
            900,
            Instant.EPOCH);

        var desired = ZLinkAutoConnectPlanner.computeDesired(
            local,
            List.of(descriptor));

        var target = desired.get(targetKey(
            ZLinkLocationRole.ROUTER,
            descriptor.nodeRid(),
            73));
        assertEquals(73, target.lifecycleGeneration());
    }

    @Test
    void asymmetricTopologiesOnlyDialFromOutboundRole() {
        assertTrue(hasTarget(
            local(ZLinkAutoConnectType.CLIENT_SERVER, ZLinkLocationRole.DEALER, "client", ""),
            peer(ZLinkAutoConnectType.CLIENT_SERVER, ZLinkLocationRole.ROUTER, "server", "inproc://server")));
        assertFalse(hasTarget(
            local(ZLinkAutoConnectType.CLIENT_SERVER, ZLinkLocationRole.ROUTER, "server", "inproc://server"),
            peer(ZLinkAutoConnectType.CLIENT_SERVER, ZLinkLocationRole.DEALER, "client", "")));
        assertTrue(hasTarget(
            local(ZLinkAutoConnectType.FANOUT, ZLinkLocationRole.SUB, "subscriber", ""),
            peer(ZLinkAutoConnectType.FANOUT, ZLinkLocationRole.PUB, "publisher", "inproc://publisher")));
        assertFalse(hasTarget(
            local(ZLinkAutoConnectType.FANOUT, ZLinkLocationRole.PUB, "publisher", "inproc://publisher"),
            peer(ZLinkAutoConnectType.FANOUT, ZLinkLocationRole.SUB, "subscriber", "")));
    }

    @Test
    void asymmetricTopologiesOnlyAdvertiseInboundRole() {
        RoutingId rid = RoutingId.from("node");
        assertTrue(ZLinkLocationAutoConnectHost.shouldAdvertise(
            ZLinkAutoConnectType.CLIENT_SERVER,
            ZLinkLocationRole.ROUTER,
            rid,
            "inproc://server"));
        assertFalse(ZLinkLocationAutoConnectHost.shouldAdvertise(
            ZLinkAutoConnectType.CLIENT_SERVER,
            ZLinkLocationRole.DEALER,
            rid,
            ""));
        assertTrue(ZLinkLocationAutoConnectHost.shouldAdvertise(
            ZLinkAutoConnectType.FANOUT,
            ZLinkLocationRole.PUB,
            rid,
            "inproc://publisher"));
        assertFalse(ZLinkLocationAutoConnectHost.shouldAdvertise(
            ZLinkAutoConnectType.FANOUT,
            ZLinkLocationRole.SUB,
            rid,
            ""));
    }

    private static boolean hasTarget(
        ZLinkAutoConnectPlanner.Local local,
        ZLinkAutoConnectPeer peer) {
        return ZLinkAutoConnectPlanner.computeDesired(local, List.of(peer))
            .containsKey(targetKey(
                peer.role(),
                peer.nodeRid(),
                peer.generation()));
    }

    private static String targetKey(ZLinkLocationRole role, String rid) {
        return targetKey(role, RoutingId.from(rid), 1);
    }

    private static String targetKey(
        ZLinkLocationRole role,
        RoutingId rid,
        long lifecycleGeneration) {
        return role.name().toLowerCase(java.util.Locale.ROOT)
            + "|"
            + rid.toHex()
            + "|"
            + lifecycleGeneration;
    }

    private static ZLinkAutoConnectPlanner.Local local(
        ZLinkAutoConnectType type,
        ZLinkLocationRole role,
        String rid,
        String endpoint) {
        return new ZLinkAutoConnectPlanner.Local(
            type,
            "mesh",
            role,
            RoutingId.from(rid),
            endpoint);
    }

    private static ZLinkAutoConnectPeer peer(
        ZLinkAutoConnectType type,
        ZLinkLocationRole role,
        String rid,
        String endpoint) {
        return new ZLinkAutoConnectPeer(
            type,
            "mesh",
            RoutingId.from(rid),
            role,
            endpoint,
            100,
            false,
            1,
            Map.of(),
            List.of(),
            "owner-" + rid,
            1,
            Instant.EPOCH);
    }
}
