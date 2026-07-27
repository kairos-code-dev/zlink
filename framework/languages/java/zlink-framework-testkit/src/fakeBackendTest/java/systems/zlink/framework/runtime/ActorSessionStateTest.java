package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.internal.backend.*;

import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class ActorSessionStateTest {
    @Test
    void actorSessionState_filtersStaleDisconnect_andOnlyDisconnectsCurrentStream() {
        RemoteSessionRelayTest.GameSpot.disconnectCount = 0;
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();
        DefaultZLinkFrameworkOptions options = RemoteSessionRelayTest.options();
        String spotId = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backend)) {
            runtime.spotManager()
                .create(RemoteSessionRelayTest.GameSpot.class, spotId)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            actor.context()
                .joinSpot(spotId, "join")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            ZLinkSessionActor staleBinding = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();
            ZLinkSessionActor currentBinding = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-2"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            staleBinding.notifyDisconnected().toCompletableFuture().join();
            assertTrue(RemoteSessionRelayTest.GameSpot.disconnectCount == 0);
            actor.context()
                .boundSession()
                .send(new Push("push"))
                .submit();
            awaitCall(backend, "stream.send.session-2.Push.");

            currentBinding.notifyDisconnected().toCompletableFuture().join();
            assertThrows(
                ZLinkConfigurationException.class,
                () -> actor.context().boundSession());
            assertTrue(RemoteSessionRelayTest.GameSpot.disconnectCount == 1);
            assertTrue(actor.context().spotId().isPresent());
        }

        assertTrue(backend.calls().contains("stream.unbindActor.player-1"));
        assertTrue(backend.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.session-2.Push.")));
    }

    @Test
    void remoteSessionActorNotifyDisconnected_routesDisconnectPacketBeforeUnbind() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(RemoteSessionRelayTest.options(), backend)) {
            ZLinkSessionActor remoteBinding = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bind(new systems.zlink.framework.actors.ActorRef(
                    RoutingId.from("play-node"),
                    "player-remote",
                    1))
                .toCompletableFuture()
                .join();

            remoteBinding.notifyDisconnected().toCompletableFuture().join();
        }

        assertTrue(backend.calls().contains(
            "stream.relayBoundActor.player-remote.JSON."
                + ZLinkActorSpotRoutePackets.SESSION_DISCONNECTED_PACKET_NAME));
        assertTrue(backend.calls().contains("stream.unbindActor.player-remote"));
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
}
