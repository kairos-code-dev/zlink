package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.internal.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class ActorManagerTest {
    @Test
    void actorManager_createGetOrCreateFind_work() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh; node.enableRouter("inproc://play-router");
                node.addSpotFactory(GameSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            ActorRef created = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();
            ActorRef reused = runtime.actorManager()
                .getOrCreate("player-1", "player")
                .toCompletableFuture()
                .join();
            Optional<ActorRef> found = runtime.actorManager()
                .find("player-1")
                .toCompletableFuture()
                .join();

            assertEquals(created, reused);
            assertEquals(Optional.of(created), found);
            assertEquals("player-1", created.actorId());
        }
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

    public static final class GameSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override public CompletionStage<Void> onJoinedActor(ZLinkActor actor) { return CompletableFuture.completedFuture(null); }
        @Override public CompletionStage<Void> onLeaveActor(ZLinkActor actor) { return CompletableFuture.completedFuture(null); }
    }
}
