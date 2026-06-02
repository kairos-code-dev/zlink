package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class ActorSessionStateTest {
    @Test
    void actorSessionState_filtersStaleDisconnect_andOnlyDisconnectsCurrentStream() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(RemoteActorGatewayTest.options(), backend)) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            ZLinkSessionActor staleBinding = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bindAsync(actor)
                .toCompletableFuture()
                .join();
            ZLinkSessionActor currentBinding = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-2"))
                .bindAsync(actor)
                .toCompletableFuture()
                .join();

            staleBinding.notifyDisconnectedAsync().toCompletableFuture().join();
            actor.context()
                .boundSession()
                .send("push")
                .packetName("Push")
                .submitAsync()
                .toCompletableFuture()
                .join();

            currentBinding.notifyDisconnectedAsync().toCompletableFuture().join();
            assertThrows(
                ZLinkConfigurationException.class,
                () -> actor.context().boundSession());
        }

        assertTrue(backend.calls().contains("stream.unbindActor.player-1"));
        assertTrue(backend.calls().contains("stream.sendBoundActor.player-1"));
    }
}
