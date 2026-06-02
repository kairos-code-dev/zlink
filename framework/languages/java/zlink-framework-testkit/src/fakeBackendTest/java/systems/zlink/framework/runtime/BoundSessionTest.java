package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class BoundSessionTest {
    @Test
    void playActorPush_arrivesAtClientStream() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(RemoteActorGatewayTest.options(), backend)) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bindAsync(actor)
                .toCompletableFuture()
                .join();

            actor.context()
                .boundSession()
                .send("push")
                .packetName("Push")
                .submitAsync()
                .toCompletableFuture()
                .join();
        }

        assertTrue(backend.calls().contains("stream.sendBoundActor.player-1"));
    }
}
