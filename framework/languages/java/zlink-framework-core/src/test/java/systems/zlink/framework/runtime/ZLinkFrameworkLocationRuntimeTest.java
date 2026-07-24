package systems.zlink.framework.runtime.host;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.configuration.ZLinkObjectPlacementOptions;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkAuthorityMissing;
import systems.zlink.framework.locations.ZLinkAuthoritySnapshot;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendPublisherSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSubscriberSocket;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkSpotBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkStreamBackendAdapter;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;
import systems.zlink.framework.runtime.InMemoryRelocationStore;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotKind;

class ZLinkFrameworkLocationRuntimeTest {
    @Test
    void configuredLocationStoreStartsLeaseAndCloseRemovesOwnerRows() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(store);
        options.addRelocationStore(new InMemoryRelocationStore());

        ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, new MinimalBackend());
        runtime.closeAsync().toCompletableFuture().get();

        assertEquals(List.of(), store.listPeerLocations(ZLinkPeerLocationFilter.all()).toCompletableFuture().get());
    }

    @Test
    void userSpotCreationClaimsLocationRowAndCloseRemovesIt() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(store);
        RoutingId nodeRid = RoutingId.from("spot-node");
        RoutingId spotRid = RoutingId.from("room-1");
        var mesh = options.addRouteMesh("location-game")
            .setRoutingId(nodeRid)
            .listen("inproc://location-user-spot");
        mesh.channelName("location-game");
        mesh.objects().server().addSpotFactory(
            "location-spot",
            LocationSpot.class,
            new ZLinkObjectPlacementOptions(Set.of(), null, null),
            ZLinkRelocationPolicy.disabled());

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            var created = runtime.spotManager()
                .getOrCreate(spotRid, "location-spot")
                .submit()
                .toCompletableFuture()
                .get();

            assertInstanceOf(
                ZLinkAuthoritySnapshot.class,
                store.read(
                        ZLinkAuthorityKeyCodec.spot(spotRid),
                        () -> false)
                    .toCompletableFuture()
                    .get());
            assertTrue(runtime.spotManager().close(created.spot())
                .toCompletableFuture().get());
            assertInstanceOf(
                ZLinkAuthorityMissing.class,
                store.read(
                        ZLinkAuthorityKeyCodec.spot(spotRid),
                        () -> false)
                    .toCompletableFuture()
                    .get());
        }
    }

    @Test
    void userSpotCloseRejectsMovingAuthorityBeforeLocalClose() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(store);
        RoutingId nodeRid = RoutingId.from("moving-spot-node");
        RoutingId spotRid = RoutingId.from("moving-room");
        var mesh = options.addRouteMesh("moving-game")
            .setRoutingId(nodeRid)
            .listen("inproc://moving-user-spot");
        mesh.channelName("moving-game");
        mesh.objects().server().addSpotFactory(
            "location-spot",
            LocationSpot.class,
            new ZLinkObjectPlacementOptions(Set.of(), null, null),
            ZLinkRelocationPolicy.disabled());

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(
                     options, new ZLinkJavaBackendAdapterFactory())) {
            var created = runtime.spotManager()
                .getOrCreate(spotRid, "location-spot")
                .submit()
                .toCompletableFuture()
                .get();
            String key = ZLinkAuthorityKeyCodec.spot(spotRid);
            var snapshot = assertInstanceOf(
                ZLinkAuthoritySnapshot.class,
                store.read(key, () -> false).toCompletableFuture().get());
            var authority = new systems.zlink.framework.runtime.locations
                .ZLinkServiceAuthorityPayloadCodec()
                .decode(snapshot.payload())
                .orElseThrow();
            byte[] closing = new systems.zlink.framework.runtime.locations
                .ZLinkServiceAuthorityPayloadCodec()
                .encodeUser(
                    systems.zlink.framework.runtime.locations
                        .ZLinkServiceAuthorityPayloadCodec.State.CLOSING,
                    authority.stableType(),
                    authority.spotRid(),
                    authority.ownerId(),
                    authority.ownerLeaseGeneration(),
                    authority.meshName(),
                    authority.nodeRid(),
                    authority.nodeGeneration());
            store.compareExchange(
                    key,
                    new systems.zlink.framework.locations
                        .ZLinkAuthorityExpectFound(snapshot.storeVersion()),
                    new systems.zlink.framework.locations.ZLinkAuthorityPut(
                        closing,
                        systems.zlink.framework.locations
                            .ZLinkAuthorityGenerationTransition.PRESERVE,
                        Optional.empty(),
                        Optional.empty()),
                    () -> false)
                .toCompletableFuture()
                .get();

            assertThrows(CompletionException.class, () ->
                runtime.spotManager().close(created.spot())
                    .toCompletableFuture()
                    .join());
        }
    }

    @Test
    void actorCreationClaimsLocationRowAndRuntimeCloseRemovesIt() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(store);
        options.addRelocationStore(new InMemoryRelocationStore());
        RoutingId nodeRid = RoutingId.from("actor-node");
        var mesh = options.addRouteMesh("actors")
            .setRoutingId(nodeRid)
            .listen("inproc://location-actor-create");
        mesh.channelName("actors");
        mesh.addActorFactory("player", LocationActorFactory.class);

        ZLinkFrameworkRuntime runtime =
            ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory());
        runtime.actorManager()
            .create("player-1", "player")
            .toCompletableFuture()
            .get();

        ZLinkLocationPage<ZLinkActorLocation> rows = store.listActorLocations(
                new ZLinkActorLocationFilter("player", nodeRid, null, ZLinkSpotKind.ENTRY),
                ZLinkPageRequest.firstPage())
            .toCompletableFuture()
            .get();
        assertEquals(1, rows.items().size());
        assertEquals("player-1", rows.items().get(0).actorId());
        assertTrue(rows.items().get(0).actorRef() != null);

        runtime.closeAsync().toCompletableFuture().get();

        assertEquals(
            List.of(),
            store.listActorLocations(ZLinkActorLocationFilter.all(), ZLinkPageRequest.firstPage())
                .toCompletableFuture()
                .get()
                .items());
    }

    @Test
    void actorCreationConflictThrowsCreateRejectedKind() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        store.claimOwnerLease("owner-b", Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();
        store.updateActor(
                new ZLinkActorLocation(
                    "player-conflict",
                    "player",
                    null,
                    RoutingId.from("other-node"),
                    ZLinkSpotKind.ENTRY,
                    "",
                    null,
                    "owner-b",
                    0,
                    Instant.EPOCH),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(store);
        var mesh = options.addRouteMesh("actors-conflict")
            .setRoutingId(RoutingId.from("actor-node"))
            .listen("inproc://location-actor-conflict");
        mesh.channelName("actors-conflict");
        mesh.addActorFactory("player", LocationActorFactory.class);

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            CompletionException error = assertThrows(
                CompletionException.class,
                () -> runtime.actorManager()
                    .create("player-conflict", "player")
                    .toCompletableFuture()
                    .join());
            ZLinkFrameworkException frameworkError = (ZLinkFrameworkException) error.getCause();

            assertEquals(ZLinkFrameworkErrorKind.ACTOR_CREATE_REJECTED, frameworkError.kind());
        }
    }

    @Test
    void actorJoinAndLeaveRenewLocationRow() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(store);
        options.addRelocationStore(new InMemoryRelocationStore());
        RoutingId nodeRid = RoutingId.from("join-node");
        RoutingId spotRid = RoutingId.from("join-room");
        LocationActorFactory.last.set(null);
        LocationSpot.last.set(null);
        var mesh = options.addRouteMesh("rooms")
            .setRoutingId(nodeRid)
            .listen("inproc://location-actor-join");
        mesh.channelName("rooms");
        mesh.addActorFactory("player", LocationActorFactory.class);
        mesh.objects().server().addSpotFactory(
            "location-spot",
            LocationSpot.class,
            new ZLinkObjectPlacementOptions(Set.of(), null, null),
            ZLinkRelocationPolicy.disabled());

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            runtime.actorManager()
                .create("player-join", "player")
                .toCompletableFuture()
                .get();
            runtime.spotManager()
                .getOrCreate(spotRid, "location-spot")
                .submit()
                .toCompletableFuture()
                .get();
            LocationActor actor = LocationActorFactory.last.get();

            actor.context()
                .joinSpot(spotRid, systems.zlink.framework.messaging.ZLinkMessage.empty())
                .submit()
                .toCompletableFuture()
                .get();

            ZLinkActorLocation joined = store.listActorLocations(
                    new ZLinkActorLocationFilter("player", nodeRid, spotRid, ZLinkSpotKind.USER),
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture()
                .get()
                .items()
                .get(0);
            assertEquals("rooms", joined.spotMeshName());

            LocationSpot.last.get()
                .context()
                .leaveActor(actor)
                .toCompletableFuture()
                .get();

            ZLinkActorLocation left = store.listActorLocations(
                    new ZLinkActorLocationFilter("player", nodeRid, null, ZLinkSpotKind.ENTRY),
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture()
                .get()
                .items()
                .get(0);
            assertEquals("player-join", left.actorId());
        }
    }

    private static final class MinimalBackend implements ZLinkBackendAdapterProvider, ZLinkChannelBackendAdapter {
        @Override
        public ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options) {
            return this;
        }

        @Override
        public ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkMonitoringBackendAdapter createMonitoringAdapter(ZLinkBackendAdapterOptions options) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendContext createContext() {
            return new ZLinkBackendContext() {
                @Override
                public String name() {
                    return "context";
                }

                @Override
                public void shutdown() {
                }

                @Override
                public void close() {
                }
            };
        }

        @Override
        public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendRouterSocket createRouterSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendPublisherSocket createPublisherSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendSubscriberSocket createSubscriberSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }
    }

    public static final class LocationSpot implements ZLinkSpot<ZLinkActor> {
        static final AtomicReference<LocationSpot> last = new AtomicReference<>();
        private final ZLinkSpotContext context;

        public LocationSpot(ZLinkSpotContext context) {
            this.context = context;
            last.set(this);
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            systems.zlink.framework.messaging.ZLinkMessage request) {
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept());
        }

        @Override
        public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class LocationActorFactory implements ZLinkActorFactory {
        static final AtomicReference<LocationActor> last = new AtomicReference<>();

        @Override
        public CompletionStage<ZLinkActor> create(String actorId, ZLinkActorContext context) {
            LocationActor actor = new LocationActor(actorId, context);
            last.set(actor);
            return CompletableFuture.completedFuture(actor);
        }
    }

    public record LocationActor(String actorId, ZLinkActorContext context) implements ZLinkActor {
    }
}
