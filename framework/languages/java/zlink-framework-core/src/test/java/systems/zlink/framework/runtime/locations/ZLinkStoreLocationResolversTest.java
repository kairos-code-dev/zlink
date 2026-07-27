package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.*;

import java.lang.reflect.Proxy;
import java.time.Duration;
import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.*;

final class ZLinkStoreLocationResolversTest {
    private static final Instant NOW =
        Instant.parse("2026-07-27T00:00:00Z");
    private static final RoutingId NODE = RoutingId.from("node-a");

    @Test
    void positiveReadyAuthorityIsCachedButMissingAuthorityIsNot() {
        AtomicInteger reads = new AtomicInteger();
        AtomicReference<Object> current = new AtomicReference<>(
            readySpotSnapshot());
        ZLinkLocationStore store = (ZLinkLocationStore)
            Proxy.newProxyInstance(
                getClass().getClassLoader(),
                new Class<?>[] {ZLinkLocationStore.class},
                (proxy, method, arguments) -> switch (method.getName()) {
                    case "read" -> {
                        reads.incrementAndGet();
                        yield CompletableFuture.completedFuture(current.get());
                    }
                    case "readOwnerLease" ->
                        CompletableFuture.completedFuture(
                            new ZLinkOwnerLeaseFound(
                                new ZLinkLocationOwnerToken("owner-a", 7),
                                NOW.plusSeconds(30),
                                NOW));
                    default -> throw new UnsupportedOperationException(
                        method.getName());
                });
        ZLinkLocationOptions cachedOptions = new ZLinkLocationOptions();
        cachedOptions.setRouteCacheMaxAge(Duration.ofSeconds(10));
        var cached = new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            cachedOptions);

        assertEquals("room-a", cached.resolveSpot("room-a")
            .toCompletableFuture().join().spotId());
        assertEquals("room-a", cached.resolveSpot("room-a")
            .toCompletableFuture().join().spotId());
        assertEquals(1, reads.get());

        ZLinkLocationOptions uncachedOptions = new ZLinkLocationOptions();
        uncachedOptions.setRouteCacheMaxAge(Duration.ZERO);
        current.set(new ZLinkAuthorityMissing(NOW));
        var misses = new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            uncachedOptions);
        assertNull(misses.resolveSpot("room-a").toCompletableFuture().join());
        assertNull(misses.resolveSpot("room-a").toCompletableFuture().join());
        assertEquals(3, reads.get());
    }

    @Test
    void expiredExactOwnerLeaseRejectsAnOtherwiseReadyRoute() {
        ZLinkLocationStore store = (ZLinkLocationStore)
            Proxy.newProxyInstance(
                getClass().getClassLoader(),
                new Class<?>[] {ZLinkLocationStore.class},
                (proxy, method, arguments) -> switch (method.getName()) {
                    case "read" -> CompletableFuture.completedFuture(
                        readySpotSnapshot());
                    case "readOwnerLease" ->
                        CompletableFuture.completedFuture(
                            new ZLinkOwnerLeaseMissing());
                    default -> throw new UnsupportedOperationException(
                        method.getName());
                });
        var resolvers = new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            new ZLinkLocationOptions());

        assertNull(
            resolvers.resolveSpot("room-a").toCompletableFuture().join());
    }

    private static ZLinkAuthoritySnapshot readySpotSnapshot() {
        byte[] payload = new ZLinkServiceAuthorityPayloadCodec().encodeUser(
            ZLinkServiceAuthorityPayloadCodec.State.READY,
            "RoomSpot",
            "room-a",
            "owner-a",
            7,
            "game",
            NODE,
            11);
        return new ZLinkAuthoritySnapshot(
            "v1",
            payload,
            5,
            13,
            "owner-a",
            7,
            new ZLinkPlacementAllocation(
                ZLinkPlacementAllocationState.ACTIVE,
                ZLinkPlacementObjectKind.USER_SPOT,
                "RoomSpot",
                new ZLinkMeshNodeDescriptorKey("game", NODE),
                11,
                ZLinkPlacementCapacityBundle.spot(
                    ZLinkPlacementObjectKind.USER_SPOT,
                    "RoomSpot",
                    1)),
            Optional.empty(),
            NOW);
    }
}
