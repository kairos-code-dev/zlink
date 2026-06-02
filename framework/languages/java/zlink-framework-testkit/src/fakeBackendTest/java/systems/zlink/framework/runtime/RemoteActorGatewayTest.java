package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class RemoteActorGatewayTest {
    @Test
    void sessionAndPlayServers_relaySucceeds() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options(), backend)) {
            ZLinkSessionActor actor = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bindAsync(new ZLinkActorRef(
                    RoutingId.from("play-node"),
                    "player-1",
                    1))
                .toCompletableFuture()
                .join();

            assertEquals("player-1", actor.actorId());
        }

        assertTrue(backend.calls().contains("stream.attachActorGateway.spotNode"));
        assertTrue(backend.calls().contains("stream.bindActor.player-1"));
    }

    static DefaultZLinkFrameworkOptions options() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
        options.addActorFactory("player", PlayerActorFactory.class);
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://fake-gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });
        return options;
    }

    public static final class PlayerActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;

        PlayerActor(String actorId, ZLinkActorContext context) {
            this.actorId = actorId;
            this.context = context;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public ZLinkActorContext context() {
            return context;
        }
    }

    public static final class PlayerActorFactory implements ZLinkActorFactory {
        @Override
        public CompletionStage<ZLinkActor> createAsync(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public static final class GameSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
