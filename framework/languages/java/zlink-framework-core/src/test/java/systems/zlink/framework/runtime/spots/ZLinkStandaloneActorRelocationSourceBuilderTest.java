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
import systems.zlink.framework.actors.*;
import systems.zlink.framework.configuration.ZLinkActorFactoryOptions;
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

final class ZLinkStandaloneActorRelocationSourceBuilderTest {
    private static final ZLinkStoreCancellation NEVER = () -> false;
    private static final String MESH = "actor-retire-source";
    private static final String ACTOR_TYPE = "player";
    private static final RoutingId SOURCE_RID =
        RoutingId.from("actor-source");
    private static final RoutingId TARGET_RID =
        RoutingId.from("actor-target");

    @Test
    void capturesEntryActorAndRestoresSourceQueueAfterPrecommitAbort()
        throws Exception {
        SnapshotAdapter.captured.set(null);
        var locations = new ZLinkInMemoryLocationStore();
        var relocations = new InMemoryRelocationStore();
        DefaultZLinkFrameworkOptions options = options(
            locations, relocations);
        var registration = options.registration();
        var nodeRegistration = registration.meshNodes().getFirst();
        try (ZLinkFrameworkRuntime host =
                ZLinkFrameworkRuntimeTestAccess.start(options)) {
            ZLinkSpotRuntime runtime =
                (ZLinkSpotRuntime) host.spotManager();
            var created = host.actorManager()
                .create("actor-a", ACTOR_TYPE)
                .toCompletableFuture().get();
            assertInstanceOf(ZLinkActorCreateResult.Created.class, created);
            ZLinkMeshNodeDescriptor source = locations.listMeshNodes(
                    MESH, ZLinkPageRequest.firstPage())
                .toCompletableFuture().get().items().stream()
                .filter(value -> value.rid().equals(
                    nodeRegistration.routingId()))
                .findFirst().orElseThrow();
            ZLinkLocationOwnerToken targetOwner = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                locations.claimOwnerLease(
                        "actor-target-owner", Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            locations.updateMeshNode(
                    descriptor(targetOwner),
                    ZLinkLocationWriteIntent.NEW_CLAIM)
                .toCompletableFuture().get();

            var coordinator = new ZLinkAggregateRelocationCoordinator(
                locations, relocations);
            var permits = new ZLinkRelocationPermitPool(
                new ZLinkLocationOptions());
            var builder = new ZLinkStandaloneActorRelocationSourceBuilder(
                MESH,
                nodeRegistration.routingId(),
                source.lifecycleGeneration(),
                locations,
                coordinator,
                permits,
                runtime.actorSessions(),
                new ZLinkRelocationAdapterRegistry(
                    registration,
                    ZLinkHandlerActivator.reflection()),
                nodeRegistration.relocatableActorFactories());

            var prepared = builder.prepare("actor-a", NEVER)
                .toCompletableFuture().get();

            assertNotNull(SnapshotAdapter.captured.get());
            assertEquals(1, permits.snapshot().outboundUnits());
            assertTrue(runtime.actorSessions()
                .localActor("actor-a").isPresent());
            var root = coordinator.readRoot(
                    prepared.initialRoot().stored().reference(),
                    prepared.initialRoot().stored().checksumCrc32c(),
                    NEVER)
                .toCompletableFuture().get();
            var decoded = ZLinkCanonicalActorRelocationEnvelope.decode(
                root.payload(),
                prepared.targetRequest().relocationId(),
                "actor-a",
                true);
            assertArrayEquals(new byte[] {7, 2, 6}, decoded.state());
            assertEquals(
                prepared.targetRequest().objectGeneration(),
                decoded.objectGeneration());

            prepared.freezeAndPrepare(NEVER)
                .toCompletableFuture().get();
            prepared.abort().toCompletableFuture().get();

            assertEquals(0, permits.snapshot().outboundUnits());
            assertTrue(runtime.actorSessions()
                .localActor("actor-a").isPresent());
        }
    }

    private static DefaultZLinkFrameworkOptions options(
        ZLinkLocationStore locations,
        ZLinkRelocationStore relocations) {
        var options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(locations);
        options.addRelocationStore(relocations);
        var mesh = options.addRouteMesh(MESH)
            .setRoutingIdPrefix(SOURCE_RID.toString())
            .listen("inproc://actor-retire-source");
        mesh.channelName(MESH);
        mesh.objects().server().addActorFactory(
            ACTOR_TYPE,
            TestActor.class,
            TestActorFactory.class,
            new ZLinkActorFactoryOptions(),
            ZLinkRelocationPolicy.snapshot(SnapshotAdapter.class));
        options.validate();
        return options;
    }

    private static ZLinkMeshNodeDescriptor descriptor(
        ZLinkLocationOwnerToken owner) {
        return new ZLinkMeshNodeDescriptor(
            MESH,
            TARGET_RID,
            9,
            1,
            "inproc://actor-retire-target",
            Map.of(MESH, 100),
            1,
            List.of(new ZLinkObjectCapability(
                ZLinkPlacementObjectKind.ACTOR,
                ACTOR_TYPE,
                ZLinkObjectMaintenancePolicyKind.SNAPSHOT,
                true,
                0)),
            ZLinkMeshNodeObjectRole.SERVER,
            Optional.of(
                "actor-target-entry-00000000-0000-4000-8000-000000000001"),
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

    public static final class TestActor implements ZLinkActor {
        private final ZLinkActorContext context;

        public TestActor(ZLinkActorContext context) {
            this.context = context;
        }

        @Override public ZLinkActorContext context() {
            return context;
        }
    }

    public static final class TestActorFactory implements ZLinkActorFactory {
        @Override
        public CompletionStage<ZLinkActor> create(
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(
                new TestActor(context));
        }
    }

    public static final class SnapshotAdapter
        implements ZLinkActorRelocationAdapter<TestActor> {
        private static final AtomicReference<TestActor> captured =
            new AtomicReference<>();

        @Override
        public CompletionStage<byte[]> capture(
            TestActor actor,
            ZLinkRelocationCancellation cancellation) {
            captured.set(actor);
            return CompletableFuture.completedFuture(
                new byte[] {7, 2, 6});
        }

        @Override
        public CompletionStage<Void> restore(
            TestActor actor,
            byte[] state,
            ZLinkRelocationCancellation cancellation) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
