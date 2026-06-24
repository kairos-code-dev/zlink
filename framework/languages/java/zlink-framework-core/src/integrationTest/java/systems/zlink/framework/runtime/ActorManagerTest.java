package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class ActorManagerTest {
    @Test
    void actorManager_createGetOrCreateFind_work() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");
                node.addSpotFactory(GameSpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            ZLinkActorRef created = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();
            ZLinkActorRef reused = runtime.actorManager()
                .getOrCreate("player-1", "player")
                .toCompletableFuture()
                .join();
            Optional<ZLinkActorRef> found = runtime.actorManager()
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
        public ZLinkActor create(
            String actorId,
            ZLinkActorContext context) {
            return new PlayerActor(actorId, context);
        }
    }

    public static final class GameSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }
}
