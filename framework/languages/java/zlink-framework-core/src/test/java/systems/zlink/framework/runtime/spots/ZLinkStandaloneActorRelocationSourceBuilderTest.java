package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.*;

import java.lang.reflect.Proxy;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
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
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkRelocationPermitPool;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkRelocationAdapterRegistry;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;

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

    @Test
    void productionTargetKeepsGenerationAndOpensAfterCompletedAuthority()
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
            assertInstanceOf(
                ZLinkActorCreateResult.Created.class,
                host.actorManager().create("actor-b", ACTOR_TYPE)
                    .toCompletableFuture().get());
            ZLinkMeshNodeDescriptor source = locations.listMeshNodes(
                    MESH, ZLinkPageRequest.firstPage())
                .toCompletableFuture().get().items().getFirst();
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
            var adapters = new ZLinkRelocationAdapterRegistry(
                registration,
                ZLinkHandlerActivator.reflection());
            var builder = new ZLinkStandaloneActorRelocationSourceBuilder(
                MESH,
                nodeRegistration.routingId(),
                source.lifecycleGeneration(),
                locations,
                coordinator,
                permits,
                runtime.actorSessions(),
                adapters,
                nodeRegistration.relocatableActorFactories());
            var prepared = builder.prepare("actor-b", NEVER)
                .toCompletableFuture().get();
            long objectGeneration =
                prepared.targetRequest().objectGeneration();
            long sourceOwnerGeneration =
                prepared.targetRequest().sourceAuthorityOwnerGeneration();
            ActorTargetBackend targetBackend = new ActorTargetBackend();
            var actorStaging =
                new ZLinkStandaloneActorRelocationStagingOwner(targetBackend);
            var endpoint = new ZLinkUserSpotRetireTargetEndpoint(
                TARGET_RID,
                9,
                coordinator,
                unusedSpotStaging(),
                ignored -> null,
                (lane, record) -> CompletableFuture.completedFuture(null),
                null,
                Duration.ZERO,
                request -> coordinator.normalizeCompletedAggregate(
                    request.participants().stream()
                        .map(value -> new ZLinkAggregateRelocationCoordinator
                            .ExpectedParticipant(
                                value.authorityKey(),
                                value.objectGeneration(),
                                value.sourceAuthorityOwnerGeneration()))
                        .toList(),
                    new ZLinkAggregateFence(
                        request.fence().aggregateId(),
                        request.fence().aggregateGeneration()),
                    new ZLinkLocationOwnerToken(
                        request.targetOwnerId(),
                        request.targetOwnerLeaseGeneration()),
                    NEVER),
                null,
                null,
                locations,
                actorStaging);

            AtomicReference<ZLinkInternalMeshNode.RelocationControlHandler>
                control = new AtomicReference<>();
            ZLinkInternalMeshNode transport = relocationTransport(
                nodeRegistration.routingId(), control);
            ZLinkSpotRetireControl.install(transport, endpoint);
            new ZLinkStandaloneActorRelocationScheduler()
                .executeRemote(
                    prepared,
                    ZLinkSpotRetireControl.client(transport),
                    Duration.ofSeconds(5),
                    NEVER)
                .toCompletableFuture().get();

            assertTrue(targetBackend.published.get());
            assertTrue(targetBackend.admitted.get());
            ZLinkAuthoritySnapshot authority = assertInstanceOf(
                ZLinkAuthoritySnapshot.class,
                locations.read(
                        ZLinkAuthorityKeyCodec.actor("actor-b"), NEVER)
                    .toCompletableFuture().get());
            assertEquals(objectGeneration, authority.objectGeneration());
            assertEquals(
                sourceOwnerGeneration + 1,
                authority.authorityOwnerGeneration());
            assertEquals(0, permits.snapshot().outboundUnits());
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

    private static ZLinkUserSpotAggregateStagingOwner unusedSpotStaging() {
        return new ZLinkUserSpotAggregateStagingOwner(
            new ZLinkUserSpotAggregateStagingOwner.StagingBackend() {
                private AssertionError unused() {
                    return new AssertionError(
                        "User Spot staging must not handle an Actor root");
                }

                @Override public CompletionStage<Object> prepareSpot(
                    ZLinkUserSpotAggregateStagingOwner.Request request) {
                    return CompletableFuture.failedFuture(unused());
                }

                @Override public CompletionStage<Void> restoreSpot(
                    Object spot,
                    ZLinkUserSpotAggregateStagingOwner.Request request,
                    ZLinkRelocationCancellation cancellation) {
                    return CompletableFuture.failedFuture(unused());
                }

                @Override public CompletionStage<Object> prepareActor(
                    ZLinkUserSpotAggregateStagingOwner.ActorParticipant actor,
                    ZLinkRelocationCancellation cancellation) {
                    return CompletableFuture.failedFuture(unused());
                }

                @Override public void publishSpot(Object spot) {
                    throw unused();
                }

                @Override public void publishActor(Object actor) {
                    throw unused();
                }

                @Override public void completeActor(Object actor) {
                    throw unused();
                }

                @Override public void publishTimers(Object spot) {
                    throw unused();
                }

                @Override public CompletionStage<Void> discardActor(
                    Object actor) {
                    return CompletableFuture.failedFuture(unused());
                }

                @Override public void discardSpot(Object spot) {
                    throw unused();
                }
            });
    }

    private static ZLinkInternalMeshNode relocationTransport(
        RoutingId source,
        AtomicReference<ZLinkInternalMeshNode.RelocationControlHandler>
            handler) {
        return (ZLinkInternalMeshNode) Proxy.newProxyInstance(
            ZLinkInternalMeshNode.class.getClassLoader(),
            new Class<?>[] {ZLinkInternalMeshNode.class},
            (proxy, method, arguments) -> {
                if (method.getName().equals("setRelocationControlHandler")) {
                    handler.set(
                        (ZLinkInternalMeshNode.RelocationControlHandler)
                            arguments[0]);
                    return null;
                }
                if (method.getName().equals("requestRelocationControl")) {
                    return handler.get().handle(
                        source,
                        ((byte[]) arguments[1]).clone());
                }
                if (method.getDeclaringClass() == Object.class) {
                    return method.invoke(proxy, arguments);
                }
                throw new UnsupportedOperationException(method.getName());
            });
    }

    private static final class ActorTargetBackend
        implements ZLinkStandaloneActorRelocationStagingOwner.Backend {
        private final AtomicBoolean published = new AtomicBoolean();
        private final AtomicBoolean admitted = new AtomicBoolean();

        @Override public CompletionStage<Object> prepare(
            ZLinkStandaloneActorRelocationStagingOwner.Request request,
            byte[] state,
            ZLinkRelocationCancellation cancellation) {
            assertArrayEquals(new byte[] {7, 2, 6}, state);
            return CompletableFuture.completedFuture(new Object());
        }

        @Override public CompletionStage<Optional<byte[]>> replay(
            Object actor,
            ZLinkStandaloneActorRelocationStagingOwner.Request request,
            ZLinkActorAcceptedJournal.Record record) {
            return CompletableFuture.completedFuture(Optional.empty());
        }

        @Override public void publish(Object actor) {
            assertTrue(published.compareAndSet(false, true));
        }

        @Override public void openAdmission(Object actor) {
            assertTrue(published.get());
            assertTrue(admitted.compareAndSet(false, true));
        }

        @Override public CompletionStage<Void> discard(Object actor) {
            return CompletableFuture.completedFuture(null);
        }
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
