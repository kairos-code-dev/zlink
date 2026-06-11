package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorDisconnected;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class ActorSessionStateTest {
    @Test
    void actorSessionState_filtersStaleDisconnect_andOnlyDisconnectsCurrentStream() {
        DisconnectedHandler.disconnectCount = 0;
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();
        DefaultZLinkFrameworkOptions options = RemoteActorGatewayTest.options();
        options.addHandlersFromPackageOf(ActorSessionStateTest.class);

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backend)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
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
            assertTrue(DisconnectedHandler.disconnectCount == 0);
            actor.context()
                .boundSession()
                .send("push")
                .packetName("Push")
                .submit()
                .toCompletableFuture()
                .join();

            currentBinding.notifyDisconnected().toCompletableFuture().join();
            assertThrows(
                ZLinkConfigurationException.class,
                () -> actor.context().boundSession());
            assertTrue(DisconnectedHandler.disconnectCount == 1);
        }

        assertTrue(backend.calls().contains("stream.unbindActor.player-1"));
        assertTrue(backend.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.session-2.Push.")
                && call.endsWith(".push")));
    }

    @ZLinkHandlerGroup("entry")
    public static final class DisconnectedHandler {
        static int disconnectCount;

        @ZLinkSpotActorDisconnected
        public CompletableFuture<Void> disconnected(RemoteActorGatewayTest.PlayerActor actor) {
            disconnectCount++;
            return CompletableFuture.completedFuture(null);
        }
    }
}
