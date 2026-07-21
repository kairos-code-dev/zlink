package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.time.Instant;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.testkit.TestZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class BoundSessionTest {
    @Test
    void playActorPush_arrivesAtClientStream() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(RemoteSessionRelayTest.options(), backend)) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            actor.context()
                .boundSession()
                .send(new Push("push"))
                .submit();
            awaitCall(backend, "stream.send.session-1.Push.");
        }

        assertTrue(backend.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.session-1.Push.")));
    }

    @Test
    void boundSessionDisconnect_unbindsCurrentActorSession() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(RemoteSessionRelayTest.options(), backend)) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            actor.context()
                .boundSession()
                .disconnect()
                .toCompletableFuture()
                .join();

            org.junit.jupiter.api.Assertions.assertThrows(
                ZLinkConfigurationException.class,
                () -> actor.context().boundSession());
        }

        assertTrue(backend.calls().contains("stream.unbindActor.player-1"));
    }

    @Test
    void actorSendDoesNotReplaceExistingBoundStreamSession() throws Exception {
        ActorSendProbe.reset();
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(entrySpotOptions(), backend)) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            backend.dispatchEntrySpotActorMessage("player-1", "String", "\"status-updated\"");
            assertTrue(
                ActorSendProbe.received.await(5, TimeUnit.SECONDS),
                () -> "actor send was not dispatched; calls=" + backend.calls());

            actor.context()
                .boundSession()
                .send(new Push("push"))
                .submit();
            awaitCall(backend, "stream.send.session-1.Push.");
        }

        assertFalse(backend.calls().stream()
            .anyMatch(call -> call.startsWith(
                "spotNode.bindRemoteActorBoundSession.player-1.source-node.source-session")));
        assertTrue(backend.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.session-1.Push.")));
    }

    @Test
    void sessionActorRelay_keepsBoundActorWhileLocationPublicationIsPending() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();
        DefaultZLinkFrameworkOptions options = RemoteSessionRelayTest.options();
        ZLinkInMemoryLocationStore locations = new ZLinkInMemoryLocationStore();
        options.addLocationStore(locations);
        ActorRef remoteActor = new ActorRef(
            RoutingId.from("play-node"),
            "player-1",
            1);
        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backend)) {
            ZLinkSessionActor actor = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bind(remoteActor)
                .toCompletableFuture()
                .join();
            relayWithHeader(actor, "PlaceMark", ZLinkMessage.of("place"));
            assertTrue(backend.calls().contains(
                "stream.relayBoundActor.player-1.RAW.PlaceMark"),
                backend.calls().toString());
        }
    }

    private static ZLinkActor managedActor(
        ZLinkFrameworkRuntime runtime,
        String actorId,
        String actorType) {
        return ((ZLinkActorRuntime) runtime.actorManager())
            .getOrCreateManagedActor(actorId, actorType)
            .toCompletableFuture()
            .join();
    }

    private static void relayWithHeader(
        ZLinkSessionActor actor,
        String packetName,
        ZLinkMessage payload) {
        ZLinkSessionActorsRuntime.enterRelayDispatch(
            new ZLinkStreamHeader(packetName, Map.of(), Optional.empty()));
        try {
            actor.relay(payload).toCompletableFuture().join();
        } finally {
            ZLinkSessionActorsRuntime.exitRelayDispatch();
        }
    }

    private static DefaultZLinkFrameworkOptions entrySpotOptions() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(BoundSessionTest.class);
        var node = options.addSpotMesh("game");
        node.enableRouter("inproc://bound-session-entry-node");
        node.configureEntrySpot()
            .setRoutingId(RoutingId.from("entry-spot"));
        node.addEntrySpot(BoundSessionEntrySpot.class);
        node.addActorFactory("player", RemoteSessionRelayTest.PlayerActorFactory.class);
        var stream = options.addStreamNode("gateway");
        stream.bind("inproc://fake-gateway");
        stream.registerSession(RemoteSessionRelayTest.GameSession.class);
        return options;
    }

    private static void awaitCall(FakeZLinkBackendAdapterFactory backend, String prefix) {
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(2).toNanos();
        while (System.nanoTime() < deadline) {
            if (backend.calls().stream().anyMatch(call -> call.startsWith(prefix))) {
                return;
            }
            Thread.onSpinWait();
        }
        throw new AssertionError("missing call prefix " + prefix + " in " + backend.calls());
    }

    @ZLinkPacket("Push")
    public record Push(String value) {
    }

    public static final class BoundSessionEntrySpot extends TestZLinkEntrySpot<ZLinkActor> {
        private final ZLinkEntrySpotContext context;

        public BoundSessionEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorSendProbe {
        static volatile CountDownLatch received = new CountDownLatch(1);

        static void reset() {
            received = new CountDownLatch(1);
        }

        @ZLinkSpotActorSend
        public java.util.concurrent.CompletionStage<Void> handle(
            ZLinkActor actor,
            String message) {
            if ("player-1".equals(actor.actorId()) && "status-updated".equals(message)) {
                received.countDown();
            }
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
    }
}
