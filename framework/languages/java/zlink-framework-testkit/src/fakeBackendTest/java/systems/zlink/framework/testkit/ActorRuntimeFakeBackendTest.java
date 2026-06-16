package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

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
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

final class ActorRuntimeFakeBackendTest {
    @Test
    void actorManagerCreateGetOrCreateAndFindUseRegisteredFactory() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(GameSpot.class); }; };
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
                "spotNode.destroyActor.player-1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void actorContextJoinEntrySpotUsesBackendSpotNodeJoinOperation() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.addEntrySpot(EntrySpot.class); }; };
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(GameSpot.class); }; };
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
                .joinSpot(spotRid, Message.from("join-request").withPacketName("Join"))
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
    void entrySpotDestroyActorRemovesEntryOwnedActorWithoutLeftCallback() {
        EntrySpot.createCount = 0;
        EntrySpot.leftCount = 0;
        EntrySpot.disconnectCount = 0;
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.addEntrySpot(EntrySpot.class);
                node.addSpotFactory(GameSpot.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.attachActorGateway("play");
            stream.registerSession(DestroySession.class); };
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
            assertEquals(1, EntrySpot.createCount);
            ((ZLinkActorRuntime) runtime.actorManager())
                .notifyDisconnected(entryOwned)
                .toCompletableFuture()
                .join();
            assertEquals(1, EntrySpot.disconnectCount);
            assertSame(
                entryOwned,
                runtime.actorManager().find("player-destroy").toCompletableFuture().join().orElseThrow());
            var entrySessionActors = runtime.sessionActors(
                "gateway",
                RoutingId.from("session-destroy"));
            entrySessionActors.bind(entryOwned).toCompletableFuture().join();
            assertTrue(entrySessionActors.find("player-destroy").isPresent());

            EntrySpot.instance.context()
                .destroyActor(entryOwned)
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActor(entryOwned)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-destroy").toCompletableFuture().join());
            assertThrows(
                ZLinkConfigurationException.class,
                () -> entryOwned.context().boundSession());
            assertEquals(Optional.empty(), entrySessionActors.find("player-destroy"));
            boolean[] staleDispatchRan = {false};
            assertThrows(
                CompletionException.class,
                () -> ((ZLinkActorRuntime) runtime.actorManager())
                    .submitActorDispatch(
                        "player-destroy",
                        () -> {
                            staleDispatchRan[0] = true;
                            return java.util.concurrent.CompletableFuture.completedFuture(null);
                        })
                    .toCompletableFuture()
                    .join());
            assertEquals(false, staleDispatchRan[0]);
            assertEquals(0, EntrySpot.leftCount);

            ZLinkActor recreated = runtime.actorManager()
                .create("player-destroy", "player")
                .toCompletableFuture()
                .join();
            assertEquals(2, EntrySpot.createCount);
            EntrySpot.instance.context()
                .destroyActor(entryOwned)
                .toCompletableFuture()
                .join();
            assertSame(
                recreated,
                runtime.actorManager().find("player-destroy").toCompletableFuture().join().orElseThrow());
            assertEquals(0, EntrySpot.leftCount);

            PlayerActor reentrant = (PlayerActor) runtime.actorManager()
                .create("player-destroy-reentrant", "player")
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActor(reentrant)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-destroy-reentrant").toCompletableFuture().join());
            assertEquals(0, EntrySpot.leftCount);

            runtime.spotManager()
                .create(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor roomActor = runtime.actorManager()
                .create("player-room-destroy", "player")
                .toCompletableFuture()
                .join();
            roomActor.context()
                .joinSpot(spotRid, Message.from("join-request").withPacketName("Join"))
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertThrows(
                CompletionException.class,
                () -> EntrySpot.instance.context()
                    .destroyActor(roomActor)
                    .toCompletableFuture()
                    .join());

            roomActor.context()
                .joinEntrySpot(RoutingId.from("spot-node"))
                .submit()
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActor(roomActor)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-room-destroy").toCompletableFuture().join());
            assertEquals(0, EntrySpot.leftCount);
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.destroyActor.player-destroy"));
        assertEquals(
            true,
            backendFactory.calls().contains("stream.unbindActor.player-destroy"));
        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.destroyActor.player-room-destroy"));
    }

    @Test
    void actorCreateDoesNotNotifyEntrySpotOwnedByDifferentNode() {
        SecondEntrySpot.createCount = 0;
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("first"); node.setRouterRoutingId(RoutingId.from("first-node")); };
            { var node = mesh.addNode("second"); node.setRouterRoutingId(RoutingId.from("second-node"));
                node.addEntrySpot(SecondEntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.actorManager()
                .create("player-owned-by-second", "player")
                .toCompletableFuture()
                .join();

            assertEquals(0, SecondEntrySpot.createCount);
        }
    }

    @Test
    void actorEntrySpotRouteJoinHandlerCreatesLocalActorAndReturnsActorRefReply() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router"); }; };
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
        static int createCount;
        static int leftCount;
        static int disconnectCount;
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
        public void onCreateActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            createCount++;
        }

        @Override
        public void onLeaveActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            leftCount++;
        }

        @Override
        public void onDisconnectActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            disconnectCount++;
        }
    }

    public static final class SecondEntrySpot implements ZLinkEntrySpot {
        static int createCount;
        private final ZLinkEntrySpotContext context;

        public SecondEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void onCreateActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            createCount++;
        }
    }

    public static final class DestroySession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public void onConnected() {
        }

        @Override
        public void onDisconnected() {
        }

        @Override
        public void onError(ZLinkStreamError error) {
        }
    }
}
