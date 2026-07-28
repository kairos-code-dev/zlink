package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.*;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.configuration.ZLinkUserSpotFactoryOptions;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.InMemoryRelocationStore;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeTestAccess;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkRelocationPermitPool;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkRelocationAdapterRegistry;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotRelocationAdapter;

final class ZLinkUserSpotRetireSourceBuilderTest {
    private static final ZLinkStoreCancellation NEVER = () -> false;
    private static final String MESH = "retire-source";
    private static final String STABLE_TYPE = "room";
    private static final String SPOT_ID = "room-a";
    private static final RoutingId SOURCE_RID = RoutingId.from("source-node");
    private static final RoutingId TARGET_RID = RoutingId.from("target-node");

    @Test
    void capturesLiveSpotAndPreparesVerifiedImmutableRootBeforeSourceCommit()
        throws Exception {
        SnapshotAdapter.captured.set(null);
        ZLinkInMemoryLocationStore locations = new ZLinkInMemoryLocationStore();
        InMemoryRelocationStore relocations = new InMemoryRelocationStore();
        DefaultZLinkFrameworkOptions options = options(locations, relocations);
        var registration = options.registration();
        var nodeRegistration = registration.meshNodes().getFirst();
        try (ZLinkFrameworkRuntime host =
                ZLinkFrameworkRuntimeTestAccess.start(options)) {
            ZLinkSpotRuntime runtime = (ZLinkSpotRuntime) host.spotManager();
            ZLinkMeshNodeDescriptor source = locations.listMeshNodes(
                    MESH,
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture().get().items().stream()
                .filter(value -> value.rid().equals(nodeRegistration.routingId()))
                .findFirst().orElseThrow();
            long sourceGeneration = source.lifecycleGeneration();
            ZLinkLocationOwnerToken targetOwner = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                locations.claimOwnerLease("target-owner", Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            var created = host.spotManager().getOrCreate(SPOT_ID, STABLE_TYPE)
                .submit()
                .toCompletableFuture().get();
            assertNotNull(created.spot());
            assertSame(LiveSpot.last.get(), runtime.spotFor(SPOT_ID));
            locations.updateMeshNode(
                    descriptor(TARGET_RID, 9, targetOwner,
                        "inproc://retire-target"),
                    ZLinkLocationWriteIntent.NEW_CLAIM)
                .toCompletableFuture().get();

            ZLinkAggregateRelocationCoordinator coordinator =
                new ZLinkAggregateRelocationCoordinator(locations, relocations);
            ZLinkRelocationPermitPool permits =
                new ZLinkRelocationPermitPool(new ZLinkLocationOptions());
            ZLinkUserSpotRetireSourceBuilder builder =
                new ZLinkUserSpotRetireSourceBuilder(
                    MESH,
                    nodeRegistration.routingId(),
                    sourceGeneration,
                    locations,
                    coordinator,
                    permits,
                    runtime.spotLifecycle(),
                    runtime.actorSessions(),
                    new ZLinkRelocationAdapterRegistry(
                        registration,
                        ZLinkHandlerActivator.reflection()),
                    nodeRegistration.relocatableSpotFactories(),
                    nodeRegistration.relocatableActorFactories());

            ZLinkUserSpotRetireSourceBuilder.PreparedSource prepared =
                builder.prepare(SPOT_ID, NEVER).toCompletableFuture().get();

            assertSame(LiveSpot.last.get(), SnapshotAdapter.captured.get());
            assertEquals(1, permits.snapshot().outboundUnits());
            assertSame(LiveSpot.last.get(), runtime.spotFor(SPOT_ID));
            var root = coordinator.readRoot(
                    prepared.stagedRoot().stored().reference(),
                    prepared.stagedRoot().stored().checksumCrc32c(),
                    NEVER)
                .toCompletableFuture().get();
            var envelope = ZLinkCanonicalUserSpotRelocationEnvelope.decode(
                root.payload(),
                TARGET_RID,
                ignored -> LiveSpot.class,
                prepared.stageRequest());
            assertEquals(SPOT_ID, envelope.spotId());
            assertArrayEquals(new byte[] {7, 4, 1}, envelope.spotState());
            assertTrue(envelope.restoreSpotSnapshot());
            assertTrue(envelope.actors().isEmpty());

            var finalPrepared = prepared.freezeAndPrepareFinal(NEVER)
                .toCompletableFuture().get();
            assertEquals(
                prepared.stageRequest().fence().aggregateId(),
                finalPrepared.fence().aggregateId());
            assertSame(LiveSpot.last.get(), runtime.spotFor(SPOT_ID),
                "authority prepare must not publish target-local Ready");

            prepared.abortPrecommit().toCompletableFuture().get();
            assertEquals(0, permits.snapshot().outboundUnits());
            assertSame(LiveSpot.last.get(), runtime.spotFor(SPOT_ID));

            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                locations.removeMeshNode(
                        new ZLinkMeshNodeDescriptorKey(MESH, TARGET_RID),
                        targetOwner)
                    .toCompletableFuture().get());
            SnapshotAdapter.captured.set(null);
            assertThrows(
                java.util.concurrent.CompletionException.class,
                () -> builder.prepare(SPOT_ID, NEVER)
                    .toCompletableFuture().join());
            assertNull(SnapshotAdapter.captured.get(),
                "target admission must finish before sealing and Capture");
            assertEquals(0, permits.snapshot().outboundUnits());
            assertSame(LiveSpot.last.get(), runtime.spotFor(SPOT_ID));
        }
    }

    private static DefaultZLinkFrameworkOptions options(
        ZLinkLocationStore locations,
        ZLinkRelocationStore relocations) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(locations);
        options.addRelocationStore(relocations);
        var mesh = options.addRouteMesh(MESH)
            .setRoutingIdPrefix(SOURCE_RID.toString())
            .listen("inproc://retire-source");
        mesh.channelName(MESH).server();
        mesh.objects().server().addSpotFactory(
            STABLE_TYPE,
            LiveSpot.class,
            new ZLinkUserSpotFactoryOptions(0),
            ZLinkRelocationPolicy.snapshot(SnapshotAdapter.class));
        options.validate();
        return options;
    }

    private static ZLinkMeshNodeDescriptor descriptor(
        RoutingId rid,
        long lifecycleGeneration,
        ZLinkLocationOwnerToken owner,
        String endpoint) {
        return new ZLinkMeshNodeDescriptor(
            MESH,
            rid,
            lifecycleGeneration,
            1,
            endpoint,
            Map.of(MESH, 100),
            1,
            List.of(new ZLinkObjectCapability(
                ZLinkPlacementObjectKind.USER_SPOT,
                STABLE_TYPE,
                ZLinkObjectMaintenancePolicyKind.SNAPSHOT,
                true,
                0)),
            ZLinkMeshNodeObjectRole.SERVER,
            Optional.of("node-entry-00000000-0000-4000-8000-000000000001"),
            100,
            new ZLinkPlacementCapacity(
                new ZLinkCapacityUsage(0, 0, 64),
                new ZLinkCapacityUsage(0, 0, 64),
                List.of()),
            new ZLinkActivationConcurrency(0, 64),
            Optional.empty(),
            ZLinkFrameworkRuntimeState.SERVING,
            "security",
            owner.ownerId(),
            owner.leaseGeneration(),
            Instant.now());
    }

    public static final class LiveSpot implements ZLinkSpot<ZLinkActor> {
        private static final AtomicReference<LiveSpot> last =
            new AtomicReference<>();
        private final ZLinkSpotContext context;

        public LiveSpot(ZLinkSpotContext context) {
            this.context = context;
            last.set(this);
        }

        @Override public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            systems.zlink.framework.messaging.ZLinkMessage request) {
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept());
        }

        @Override public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class SnapshotAdapter
        implements ZLinkSpotRelocationAdapter<LiveSpot> {
        private static final AtomicReference<LiveSpot> captured =
            new AtomicReference<>();

        @Override
        public CompletionStage<byte[]> capture(
            LiveSpot spot,
            ZLinkRelocationCancellation cancellation) {
            captured.set(spot);
            return CompletableFuture.completedFuture(new byte[] {7, 4, 1});
        }

        @Override
        public CompletionStage<Void> restore(
            LiveSpot spot,
            byte[] state,
            ZLinkRelocationCancellation cancellation) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
