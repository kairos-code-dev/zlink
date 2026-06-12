package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionException;
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
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor created = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();
            ZLinkActor reused = runtime.actorManager()
                .getOrCreate("player-1", "player")
                .toCompletableFuture()
                .join();
            Optional<ZLinkActor> found = runtime.actorManager()
                .find("player-1")
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
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();

            var joined = actor.context()
                .joinEntrySpot(RoutingId.from("entry-node"))
                .submit()
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
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();

            var joined = actor.context()
                .joinSpot(spotRid, "join-request")
                .submit(String.class)
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
                .submit()
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
    void entrySpotDestroyActorRemovesEntryOwnedActorAfterLeftCallback() {
        EntrySpot.leftCount = 0;
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addEntrySpot(EntrySpot.class);
                node.addSpotFactory(GameSpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId spotRid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor entryOwned = runtime.actorManager()
                .create("player-destroy", "player")
                .toCompletableFuture()
                .join();

            EntrySpot.instance.context()
                .destroyActorAsync(entryOwned)
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActorAsync(entryOwned)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-destroy").toCompletableFuture().join());
            assertEquals(1, EntrySpot.leftCount);

            ZLinkActor recreated = runtime.actorManager()
                .create("player-destroy", "player")
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActorAsync(entryOwned)
                .toCompletableFuture()
                .join();
            assertSame(
                recreated,
                runtime.actorManager().find("player-destroy").toCompletableFuture().join().orElseThrow());
            assertEquals(1, EntrySpot.leftCount);

            PlayerActor reentrant = (PlayerActor) runtime.actorManager()
                .create("player-destroy-reentrant", "player")
                .toCompletableFuture()
                .join();
            reentrant.destroyAgainOnEntryLeft = true;
            EntrySpot.instance.context()
                .destroyActorAsync(reentrant)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-destroy-reentrant").toCompletableFuture().join());
            assertEquals(2, EntrySpot.leftCount);

            runtime.spotManager()
                .create(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor roomActor = runtime.actorManager()
                .create("player-room-destroy", "player")
                .toCompletableFuture()
                .join();
            roomActor.context()
                .joinSpot(spotRid, "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertThrows(
                CompletionException.class,
                () -> EntrySpot.instance.context()
                    .destroyActorAsync(roomActor)
                    .toCompletableFuture()
                    .join());

            roomActor.context()
                .joinEntrySpot(RoutingId.from("spot-node"))
                .submit()
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActorAsync(roomActor)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-room-destroy").toCompletableFuture().join());
            assertEquals(3, EntrySpot.leftCount);
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.destroyActor.player-destroy"));
        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.destroyActor.player-room-destroy"));
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
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        boolean destroyAgainOnEntryLeft;

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
        public ZLinkActor create(
            String actorId,
            ZLinkActorContext context) {
            return new PlayerActor(actorId, context);
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
        static EntrySpot instance;
        static int leftCount;
        private final ZLinkEntrySpotContext context;

        public EntrySpot(ZLinkEntrySpotContext context) {
            instance = this;
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void onActorLeft(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            leftCount++;
            if (actor instanceof PlayerActor player && player.destroyAgainOnEntryLeft) {
                context.destroyActorAsync(actor).toCompletableFuture().join();
            }
        }
    }
}
