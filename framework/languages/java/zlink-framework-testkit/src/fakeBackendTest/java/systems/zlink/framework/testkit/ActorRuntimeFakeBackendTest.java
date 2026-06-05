package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.actors.ZLinkActorEntrySpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class ActorRuntimeFakeBackendTest {
    @Test
    void actorManagerCreateGetOrCreateAndFindUseRegisteredFactory() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            ZLinkActor created = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            ZLinkActor reused = runtime.actorManager()
                .getOrCreateAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            Optional<ZLinkActor> found = runtime.actorManager()
                .findAsync("player-1")
                .toCompletableFuture()
                .join();

            assertSame(created, reused);
            assertEquals(Optional.of(created), found);
            assertEquals("player-1", created.actorId());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.createActor.player-1",
                "close.context",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void actorContextJoinEntrySpotUsesBackendSpotNodeJoinOperation() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addEntrySpot(EntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            var joined = actor.context()
                .joinEntrySpot(RoutingId.from("entry-node"))
                .submitAsync()
                .toCompletableFuture()
                .join();

            assertEquals("player-1", joined.actorId());
            assertEquals(RoutingId.from("entry-node"), joined.nodeRid());
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.joinActorEntrySpot.player-1.entry-node"));
    }

    @Test
    void actorContextJoinSpotUsesBackendSpotNodeJoinOperationAndUpdatesContext() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId spotRid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            var joined = actor.context()
                .joinSpot(spotRid, "join-request")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals(0, joined.resultCode());
            assertEquals("joined", joined.reply());
            assertEquals("player-1", joined.actor().actorId());
            assertEquals(RoutingId.from("spot-node"), joined.actor().nodeRid());
            assertEquals(Optional.of(spotRid), actor.context().spotRid());
            assertSame(GameSpot.instance, actor.context().getSpot(GameSpot.class));

            actor.context()
                .joinEntrySpot(RoutingId.from("entry-node"))
                .submitAsync()
                .toCompletableFuture()
                .join();
            assertThrows(
                ZLinkConfigurationException.class,
                actor.context()::getSpot);
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.joinActor.player-1.spot-node.game-1"));
        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.joinActorEntrySpot.player-1.entry-node"));
    }

    @Test
    void actorEntrySpotRouteJoinHandlerCreatesLocalActorAndReturnsActorRefReply() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.enableRouter()));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            ZLinkActorRuntime actors = (ZLinkActorRuntime) runtime.actorManager();
            Message request = ZLinkActorEntrySpotRoutePackets.encodeJoinRequest(
                "player-remote",
                "player",
                RoutingId.from("source-node"),
                7);

            Message reply = actors.handleEntrySpotRouteJoin(
                    RoutingId.from("source-route"),
                    request)
                .toCompletableFuture()
                .join();
            var decoded = ZLinkActorEntrySpotRoutePackets.decodeJoinReply(reply);

            assertEquals("player-remote", decoded.actorId());
            assertEquals("player", decoded.actorType());
            assertEquals(RoutingId.from("spot-node"), decoded.targetNodeRid());
            assertEquals(0, decoded.actorGeneration());
            request.close();
            reply.close();
        }

        assertEquals(true, backendFactory.calls().contains("spotNode.createActor.player-remote"));
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
        static GameSpot instance;

        public GameSpot() {
            instance = this;
        }

        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class EntrySpot implements ZLinkEntrySpot {
        private final ZLinkEntrySpotContext context;

        public EntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }
    }
}
