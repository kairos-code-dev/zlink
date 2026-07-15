package systems.zlink.framework.runtime.host;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.locations.ZLinkLocationAutoConnectHost;

final class ZLinkFrameworkRuntimeDrainRouteTest {
    @Test
    void drainWaiterTimeoutDoesNotCompleteSharedDrainState() {
        CompletableFuture<String> shared = new CompletableFuture<>();

        ZLinkFrameworkRuntime.independentWaiter(shared)
            .toCompletableFuture()
            .orTimeout(1, TimeUnit.MILLISECONDS)
            .exceptionally(ignored -> null)
            .join();

        assertFalse(shared.isDone());
        shared.complete("drained");
        assertEquals("drained", shared.join());
    }

    @Test
    void drainTransferUsesMappedRouteChannelWhenSpotMeshNameDiffers() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMeshChannel("route-a");
        options.addRouteMeshChannel("route-b");
        options.addSpotMesh("game-spots");
        options.configureLocations().setSpotRouterChannel("game-spots", "route-b");

        assertEquals(
            "route-b",
            ZLinkFrameworkRuntime.transferRouteChannelName(
                options.registration(), "game-spots"));
    }

    @Test
    void drainTransferRequiresActorHostCapabilityAndRejectsLocalNode() {
        RoutingId remote = RoutingId.from("play-b");
        ZLinkPeerLocation capable = peer(remote, List.of("actor:player"));
        ZLinkPeerLocation wrongType = peer(RoutingId.from("enemy-a"), List.of("actor:enemy"));
        ZLinkPeerLocation prefixOnly = peer(RoutingId.from("play-a"), List.of("actor:play"));

        assertTrue(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(capable, "player", Set.of()));
        assertFalse(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(wrongType, "player", Set.of()));
        assertFalse(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(prefixOnly, "player", Set.of()));
        assertFalse(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(
            capable, "player", Set.of(remote)));
    }

    private static ZLinkPeerLocation peer(RoutingId nodeRid, List<String> capabilities) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.SPOT_MESH, "game-spots", nodeRid,
            ZLinkLocationRole.SPOT, "", 100, false, 0, Map.of(), capabilities,
            "owner", 1, Instant.now());
    }
}
