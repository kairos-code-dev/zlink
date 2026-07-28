package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.internal.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.testkit.TestZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class RemoteSessionRelayTest {
    @Test
    void sessionAndPlayServers_relaySucceeds() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options(), backend)) {
            ZLinkSessionActor actor = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bind(new ActorRef(
                    RoutingId.from("play-node"),
                    "player-1",
                    1))
                .toCompletableFuture()
                .join();

            assertEquals("player-1", actor.actorId());
        }

        assertTrue(backend.calls().contains("stream.bindActor.player-1"));
    }

    static DefaultZLinkFrameworkOptions options() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh;
                node.enableRouter("inproc://remote-session-play-node");
                node.setRoutingId(RoutingId.from("play-node"));
                node.objects().server().addSpotFactory("GameSpot", GameSpot.class, factory -> factory.disableRelocation()); node.objects().server().addActorFactory("player", PlayerActor.class, PlayerActorFactory.class, factory -> factory.recreateOnRelocation()); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.registerSession(GameSession.class); };
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
        public CompletionStage<ZLinkActor> create(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public static final class GameSpot extends TestZLinkSpot<ZLinkActor> {
        static int disconnectCount;

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept("joined"));
        }

        @Override
        public CompletionStage<Void> onDisconnectActor(
            ZLinkActor actor) {
            disconnectCount++;
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
