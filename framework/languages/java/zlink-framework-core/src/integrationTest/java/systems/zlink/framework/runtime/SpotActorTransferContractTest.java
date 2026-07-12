package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;

final class SpotActorTransferContractTest {
    private static final RoutingId SOURCE_NODE = RoutingId.from("contract-source-node");
    private static final RoutingId TARGET_NODE = RoutingId.from("contract-target-node");
    private static final RoutingId TARGET_ROUTE = RoutingId.from("contract-target-route");
    private static final List<String> EVENTS = new CopyOnWriteArrayList<>();
    private static final ConcurrentHashMap<String, ContractActor> ACTORS =
        new ConcurrentHashMap<>();

    @BeforeEach
    void reset() {
        EVENTS.clear();
        ACTORS.clear();
        ContractTargetSpot.joinRelease = CompletableFuture.completedFuture(null);
    }

    @Test
    void localJoinAcceptOrderAndSuccessWaitForJoined() throws Exception {
        try (Harness harness = Harness.start(false)) {
            ContractActor actor = harness.createSourceActor("local-accept", "stateful", 1);
            harness.createSourceSpot(ContractSourceSpot.class, RoutingId.from("source-room"));
            harness.createSourceSpot(ContractTargetSpot.class, RoutingId.from("local-target"));
            actor.context().joinSpot(RoutingId.from("source-room"), "source").submit().toCompletableFuture().join();
            EVENTS.clear();
            ContractTargetSpot.joinRelease = new CompletableFuture<>();

            CompletableFuture<ZLinkActorJoinResult<String>> joining = actor.context()
                .joinSpot(RoutingId.from("local-target"), "accept")
                .submit(String.class)
                .toCompletableFuture();
            awaitEvent("target-admission");
            assertFalse(joining.isDone());
            ContractTargetSpot.joinRelease.complete(null);

            assertInstanceOf(ZLinkActorJoinResult.Accepted.class,
                joining.get(3, TimeUnit.SECONDS));
            assertOrder("target-admission", "source-leave", "target-joined");
        }
    }

    @Test
    void localJoinRejectHasNoSideEffect() throws Exception {
        try (Harness harness = Harness.start(false)) {
            ContractActor actor = harness.createSourceActor("local-reject", "stateful", 1);
            RoutingId sourceRoom = RoutingId.from("source-room");
            harness.createSourceSpot(ContractSourceSpot.class, sourceRoom);
            harness.createSourceSpot(ContractTargetSpot.class, RoutingId.from("local-target"));
            actor.context().joinSpot(sourceRoom, "source").submit().toCompletableFuture().join();
            EVENTS.clear();

            assertThrows(
                CompletionException.class,
                () -> actor.context().joinSpot(RoutingId.from("local-target"), "reject")
                    .submit(String.class).toCompletableFuture().join());

            assertEquals(Optional.of(sourceRoom), actor.context().spotRid());
            assertEquals(List.of("target-admission"), EVENTS);
        }
    }

    @Test
    void packetDuringMovingWaitsAndRunsOnlyOnCommittedTarget() throws Exception {
        try (Harness harness = Harness.start(false)) {
            ContractActor actor = harness.createSourceActor("local-moving", "stateful", 1);
            RoutingId sourceRoom = RoutingId.from("source-room");
            RoutingId targetRoom = RoutingId.from("local-target");
            harness.createSourceSpot(ContractSourceSpot.class, sourceRoom);
            harness.createSourceSpot(ContractTargetSpot.class, targetRoom);
            actor.context().joinSpot(sourceRoom, "source").submit().toCompletableFuture().join();
            ActorRef actorRef = harness.source.actorManager().find(actor.actorId())
                .toCompletableFuture().join().orElseThrow();
            EVENTS.clear();
            ContractTargetSpot.joinRelease = new CompletableFuture<>();

            CompletableFuture<ZLinkActorJoinResult<String>> joining = actor.context()
                .joinSpot(targetRoom, "accept")
                .submit(String.class)
                .toCompletableFuture();
            awaitEvent("target-admission");
            CompletableFuture<ProbeReply> probe = CompletableFuture.supplyAsync(() ->
                harness.source.actorClient()
                    .requestToActor(actorRef, new ProbeRequest("during-move"))
                    .submit(ProbeReply.class)
                    .toCompletableFuture()
                    .join());
            assertFalse(probe.isDone());

            ContractTargetSpot.joinRelease.complete(null);
            assertInstanceOf(ZLinkActorJoinResult.Accepted.class,
                joining.get(3, TimeUnit.SECONDS));
            assertEquals("local-target", probe.get(3, TimeUnit.SECONDS).spotRid());
            assertEquals(1, EVENTS.stream().filter("target-packet"::equals).count());
        }
    }

    @Test
    void remoteJoinSuccessOrderAndStateTransfer() throws Exception {
        try (Harness harness = Harness.start(true)) {
            ContractActor actor = harness.createSourceActor("remote-state", "stateful", 17);
            RoutingId sourceRoom = RoutingId.from("source-room");
            RoutingId targetRoom = RoutingId.from("target-room");
            harness.createSourceSpot(ContractSourceSpot.class, sourceRoom);
            harness.createTargetSpot(targetRoom);
            actor.context().joinSpot(sourceRoom, "source").submit().toCompletableFuture().join();
            EVENTS.clear();

            ZLinkActorJoinResult<String> result = actor.context()
                .joinSpot(targetRoom, "accept")
                .timeout(Duration.ofSeconds(3))
                .submit(String.class)
                .toCompletableFuture()
                .get(4, TimeUnit.SECONDS);

            ZLinkActorJoinResult.Accepted<String> accepted = assertInstanceOf(
                ZLinkActorJoinResult.Accepted.class, result);
            assertEquals(TARGET_NODE, accepted.actor().nodeRid());
            ContractActor transferred = harness.targetActor("remote-state");
            assertEquals(17, transferred.stateVersion);
            assertOrder(
                "target-admission",
                "transfer-out",
                "source-leave",
                "transfer-in",
                "target-joined");
        }
    }

    @Test
    void remoteCustomEmptyStateTransferSucceeds() throws Exception {
        try (Harness harness = Harness.start(true)) {
            ContractActor transferred = harness.remoteJoin("remote-empty", "empty", 23);

            assertEquals(0, transferred.stateVersion);
            assertOrder("target-admission", "transfer-out-empty", "source-leave",
                "transfer-in-empty", "target-joined");
        }
    }

    @Test
    void remoteMissingAdapterUsesFactoryAndEmptyState() throws Exception {
        try (Harness harness = Harness.start(true)) {
            ContractActor transferred = harness.remoteJoin("remote-default", "stateless", 0);

            assertEquals(0, transferred.stateVersion);
            assertFalse(EVENTS.contains("transfer-out"));
            assertFalse(EVENTS.contains("transfer-in"));
            assertOrder("target-admission", "source-leave", "target-joined");
        }
    }

    @Test
    void joinedCallbackFailureDoesNotReturnSuccess() throws Exception {
        try (Harness harness = Harness.start(true)) {
            ContractActor actor = harness.prepareRemote("fail-joined", "stateful", 31);

            assertThrows(
                Exception.class,
                () -> actor.context().joinSpot(RoutingId.from("target-room"), "accept")
                    .timeout(Duration.ofSeconds(3))
                    .submit(String.class)
                    .toCompletableFuture()
                    .get(4, TimeUnit.SECONDS));
            assertTrue(EVENTS.contains("target-joined-failed"));
        }
    }

    @Test
    void committedTargetSurvivesSourceRuntimeClose() throws Exception {
        Harness harness = Harness.start(true);
        try {
            ContractActor transferred = harness.remoteJoin("source-close", "stateful", 37);
            harness.closeSource();

            assertEquals("source-close", transferred.actorId());
            assertEquals(37, harness.targetActor("source-close").stateVersion);
        } finally {
            harness.close();
        }
    }

    private static void awaitEvent(String event) throws Exception {
        long deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos();
        while (!EVENTS.contains(event) && System.nanoTime() < deadline) {
            Thread.onSpinWait();
        }
        assertTrue(EVENTS.contains(event), () -> "missing event " + event + " in " + EVENTS);
    }

    private static void assertOrder(String... expected) {
        int cursor = -1;
        for (String event : expected) {
            int relative = EVENTS.subList(cursor + 1, EVENTS.size()).indexOf(event);
            assertTrue(relative >= 0, () -> "missing event " + event + " in " + EVENTS);
            cursor += relative + 1;
        }
    }

    public static final class ContractActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;
        private int stateVersion;

        ContractActor(String actorId, ZLinkActorContext context) {
            this.actorId = actorId;
            this.context = context;
        }

        @Override public String actorId() { return actorId; }
        @Override public ZLinkActorContext context() { return context; }
    }

    public static final class ContractActorFactory implements ZLinkActorFactory {
        @Override
        public java.util.concurrent.CompletionStage<ZLinkActor> create(
            String actorId, ZLinkActorContext context) {
            ContractActor actor = new ContractActor(actorId, context);
            ACTORS.put(actorId, actor);
            return CompletableFuture.completedFuture(actor);
        }
    }

    public static final class StatefulAdapter
        implements ZLinkActorTransferAdapter<ContractActor> {
        @Override
        public java.util.concurrent.CompletionStage<ZLinkMessage> transferOut(
            ContractActor actor) {
            EVENTS.add("transfer-out");
            return CompletableFuture.completedFuture(ZLinkMessage.of(actor.stateVersion));
        }

        @Override
        public java.util.concurrent.CompletionStage<ContractActor> transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state) {
            EVENTS.add("transfer-in");
            ContractActor actor = new ContractActor(actorId, context);
            actor.stateVersion = state.decode(Integer.class);
            ACTORS.put(actorId, actor);
            return CompletableFuture.completedFuture(actor);
        }
    }

    public static final class EmptyAdapter
        implements ZLinkActorTransferAdapter<ContractActor> {
        @Override
        public java.util.concurrent.CompletionStage<ZLinkMessage> transferOut(
            ContractActor actor) {
            EVENTS.add("transfer-out-empty");
            return CompletableFuture.completedFuture(ZLinkMessage.empty());
        }

        @Override
        public java.util.concurrent.CompletionStage<ContractActor> transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state) {
            EVENTS.add("transfer-in-empty");
            ContractActor actor = new ContractActor(actorId, context);
            ACTORS.put(actorId, actor);
            return CompletableFuture.completedFuture(actor);
        }
    }

    public static final class ContractSourceSpot implements ZLinkSpot<ContractActor> {
        private final ZLinkSpotContext context;
        public ContractSourceSpot(ZLinkSpotContext context) { this.context = context; }
        @Override public ZLinkSpotContext context() { return context; }
        @Override public java.util.concurrent.CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId, ZLinkMessage request) {
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept("source"));
        }
        @Override public java.util.concurrent.CompletionStage<Void> onJoinedActor(ContractActor actor) {
            return CompletableFuture.completedFuture(null);
        }
        @Override public java.util.concurrent.CompletionStage<Void> onLeaveActor(ContractActor actor) {
            EVENTS.add("source-leave");
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class ContractTargetSpot implements ZLinkSpot<ContractActor> {
        static CompletableFuture<Void> joinRelease = CompletableFuture.completedFuture(null);
        private final ZLinkSpotContext context;
        public ContractTargetSpot(ZLinkSpotContext context) { this.context = context; }
        @Override public ZLinkSpotContext context() { return context; }
        @Override public void configure() {
            context.handlers().addHandler(ProbeHandler.class);
        }
        @Override public java.util.concurrent.CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
            return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
        }
        @Override public java.util.concurrent.CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId, ZLinkMessage request) {
            EVENTS.add("target-admission");
            return CompletableFuture.completedFuture("reject".equals(request.decode(String.class))
                ? ZLinkSpotActorJoinResponse.reject("rejected")
                : ZLinkSpotActorJoinResponse.accept("accepted"));
        }
        @Override public java.util.concurrent.CompletionStage<Void> onJoinedActor(ContractActor actor) {
            if (actor.actorId().startsWith("fail-joined")) {
                EVENTS.add("target-joined-failed");
                throw new IllegalStateException("injected joined failure");
            }
            joinRelease.join();
            EVENTS.add("target-joined");
            return CompletableFuture.completedFuture(null);
        }
        @Override public java.util.concurrent.CompletionStage<Void> onLeaveActor(ContractActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public record ProbeRequest(String marker) { }
    public record ProbeReply(String marker, String spotRid) { }

    public static final class ProbeHandler implements ZLinkSpotActorRequestHandler<
        ContractTargetSpot,
        ContractActor,
        ProbeRequest,
        ProbeReply> {
        @Override
        public ProbeReply handle(
            ContractTargetSpot spot,
            ContractActor actor,
            ZLinkSpotActorRequestContext context,
            ProbeRequest request) {
            EVENTS.add("target-packet");
            return new ProbeReply(request.marker(), spot.context().spotRid().toString());
        }
    }


    private static final class Harness implements AutoCloseable {
        private ZLinkFrameworkRuntime source;
        private final ZLinkFrameworkRuntime target;

        private Harness(ZLinkFrameworkRuntime source, ZLinkFrameworkRuntime target) {
            this.source = source;
            this.target = target;
        }

        static Harness start(boolean remote) throws IOException {
            String sourceSpotEndpoint = tcpEndpoint();
            DefaultZLinkFrameworkOptions sourceOptions = options(
                SOURCE_NODE, sourceSpotEndpoint, true);
            if (!remote) {
                return new Harness(
                    RuntimeTestSupport.startFramework(
                        sourceOptions,
                        new ZLinkJavaBackendAdapterFactory()),
                    null);
            }
            String targetSpotEndpoint = tcpEndpoint();
            String sourceRouteEndpoint = tcpEndpoint();
            String targetRouteEndpoint = tcpEndpoint();
            var sourceRoute = sourceOptions.addRouteMeshChannel("contract-route");
            sourceRoute.enableServer(sourceRouteEndpoint);
            sourceRoute.enableClient(targetRouteEndpoint);
            sourceRoute.setRoutingId(RoutingId.from("contract-source-route"));

            DefaultZLinkFrameworkOptions targetOptions = options(
                TARGET_NODE, targetSpotEndpoint, false);
            var targetRoute = targetOptions.addRouteMeshChannel("contract-route");
            targetRoute.enableServer(targetRouteEndpoint);
            targetRoute.enableClient(sourceRouteEndpoint);
            targetRoute.setRoutingId(TARGET_ROUTE);

            ZLinkFrameworkRuntime source = RuntimeTestSupport.startFramework(
                sourceOptions,
                new ZLinkJavaBackendAdapterFactory());
            try {
                ZLinkFrameworkRuntime target = RuntimeTestSupport.startFramework(
                    targetOptions,
                    new ZLinkJavaBackendAdapterFactory());
                return new Harness(source, target);
            } catch (RuntimeException error) {
                source.close();
                throw error;
            }
        }

        private static DefaultZLinkFrameworkOptions options(
            RoutingId nodeRid,
            String spotEndpoint,
            boolean sourceNode) {
            DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
            options.addLocationStore(new ZLinkInMemoryLocationStore());
            var node = options.addSpotMesh("contract-mesh");
            node.enableRouter(spotEndpoint).setRoutingId(nodeRid);
            node.addActorFactory("stateful", ContractActorFactory.class);
            node.addActorTransferAdapter("stateful", StatefulAdapter.class);
            node.addActorFactory("empty", ContractActorFactory.class);
            node.addActorTransferAdapter("empty", EmptyAdapter.class);
            node.addActorFactory("stateless", ContractActorFactory.class);
            node.addSpotFactory(ContractTargetSpot.class);
            if (sourceNode) {
                node.addSpotFactory(ContractSourceSpot.class);
            }
            return options;
        }

        ContractActor createSourceActor(String actorId, String actorType, int state) {
            ContractActor actor = (ContractActor) ((ZLinkActorRuntime) source.actorManager())
                .getOrCreateManagedActor(actorId, actorType)
                .toCompletableFuture().join();
            actor.stateVersion = state;
            return actor;
        }

        void createSourceSpot(Class<? extends ZLinkSpot<?>> type, RoutingId rid) {
            source.spotManager().create(type, rid).toCompletableFuture().join();
        }

        void createTargetSpot(RoutingId rid) {
            target.spotManager().create(ContractTargetSpot.class, rid).toCompletableFuture().join();
        }

        ContractActor prepareRemote(String actorId, String actorType, int state) {
            ContractActor actor = createSourceActor(actorId, actorType, state);
            RoutingId sourceRoom = RoutingId.from("source-room");
            createSourceSpot(ContractSourceSpot.class, sourceRoom);
            createTargetSpot(RoutingId.from("target-room"));
            actor.context().joinSpot(sourceRoom, "source").submit().toCompletableFuture().join();
            EVENTS.clear();
            return actor;
        }

        ContractActor remoteJoin(String actorId, String actorType, int state) throws Exception {
            ContractActor actor = prepareRemote(actorId, actorType, state);
            ZLinkActorJoinResult<String> result = actor.context()
                .joinSpot(RoutingId.from("target-room"), "accept")
                .timeout(Duration.ofSeconds(3))
                .submit(String.class)
                .toCompletableFuture().get(4, TimeUnit.SECONDS);
            assertInstanceOf(ZLinkActorJoinResult.Accepted.class, result);
            return targetActor(actorId);
        }

        ContractActor targetActor(String actorId) {
            return Optional.ofNullable(ACTORS.get(actorId)).orElseThrow();
        }

        void closeSource() {
            if (source != null) {
                source.close();
                source = null;
            }
        }

        @Override
        public void close() {
            closeSource();
            if (target != null) {
                target.close();
            }
        }
    }

    private static String tcpEndpoint() throws IOException {
        try (ServerSocket socket = new ServerSocket(0)) {
            return "tcp://127.0.0.1:" + socket.getLocalPort();
        }
    }
}
