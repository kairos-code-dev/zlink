package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;

class ZLinkInMemoryLocationStoreTest {
    private static final Instant NOW = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE_A = RoutingId.from(new byte[] {0x01});

    @Test
    void newClaimRejectsWhenExistingOwnerLeaseIsLive() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        store.renewOwnerLeaseAsync("owner-a", NODE_A, Duration.ofSeconds(30)).toCompletableFuture().get();
        var first = store.updatePeerAsync(peer("owner-a", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        var conflict = store.updatePeerAsync(peer("owner-b", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(ZLinkLocationWriteStatus.STORED, first.status());
        assertEquals(1, first.generation());
        assertEquals(ZLinkLocationWriteStatus.REJECTED_CONFLICT, conflict.status());
    }

    @Test
    void expiredOwnerLeaseAllowsNewClaimWithNewGeneration() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        store.updatePeerAsync(peer("owner-a", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        var replacement = store.updatePeerAsync(peer("owner-b", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(ZLinkLocationWriteStatus.STORED, replacement.status());
        assertEquals(2, replacement.generation());
    }

    @Test
    void renewAndRemoveRequireCurrentOwnerToken() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        var claim = store.updatePeerAsync(peer("owner-a", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        var staleRenew = store.updatePeerAsync(peer("owner-a", NODE_A, 99), ZLinkLocationWriteIntent.RENEW)
            .toCompletableFuture()
            .get();
        var renew = store.updatePeerAsync(peer("owner-a", NODE_A, claim.generation()), ZLinkLocationWriteIntent.RENEW)
            .toCompletableFuture()
            .get();
        var staleRemove = store.removePeerAsync(
                peerKey(NODE_A),
                new ZLinkLocationOwnerToken("owner-a", 99))
            .toCompletableFuture()
            .get();
        var remove = store.removePeerAsync(
                peerKey(NODE_A),
                new ZLinkLocationOwnerToken("owner-a", claim.generation()))
            .toCompletableFuture()
            .get();

        assertEquals(ZLinkLocationWriteStatus.IGNORED_STALE, staleRenew.status());
        assertEquals(ZLinkLocationWriteStatus.STORED, renew.status());
        assertEquals(ZLinkLocationWriteStatus.IGNORED_STALE, staleRemove.status());
        assertEquals(ZLinkLocationWriteStatus.STORED, remove.status());
        assertEquals(List.of(), store.listPeersAsync(ZLinkPeerLocationFilter.all()).toCompletableFuture().get());
    }

    @Test
    void changeStampTracksMeshSpecificAndKindWideScopes() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));

        store.updatePeerAsync(peer("owner-a", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(1, store.getChangeStampAsync(new ZLinkLocationChangeStampScope(ZLinkLocationKind.PEER, "mesh"))
            .toCompletableFuture()
            .get());
        assertEquals(1, store.getChangeStampAsync(new ZLinkLocationChangeStampScope(ZLinkLocationKind.PEER, null))
            .toCompletableFuture()
            .get());
        assertEquals(0, store.getChangeStampAsync(new ZLinkLocationChangeStampScope(ZLinkLocationKind.SPOT, null))
            .toCompletableFuture()
            .get());
    }

    private static ZLinkPeerLocation peer(String ownerId, RoutingId nodeRid, long generation) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh",
            nodeRid,
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:6000",
            1,
            0,
            Map.of(),
            List.of(),
            ownerId,
            generation,
            NOW);
    }

    private static ZLinkPeerLocationKey peerKey(RoutingId nodeRid) {
        return new ZLinkPeerLocationKey(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh",
            ZLinkLocationRole.ROUTER,
            nodeRid,
            null);
    }
}
