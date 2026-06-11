package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

final class NodesAndServicesTest {
    @Test
    void addZLinkFramework_throws_whenSpotFactoryTypeIsDuplicatedAcrossNodes() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            options.addSpotMesh("game", mesh -> {
                mesh.addNode("left", node -> node.addSpotFactory(GameSpot.class));
                mesh.addNode("right", node -> node.addSpotFactory(GameSpot.class));
            }));
    }

    @Test
    void addZLinkFramework_throws_whenSpotNodeRegistersMultipleEntrySpots() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.addEntrySpot(EntrySpotA.class);
                node.addEntrySpot(EntrySpotB.class);
            }));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void addZLinkFramework_throws_whenActorFactoryWithoutSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addActorFactory("player", PlayerActorFactory.class);

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void addZLinkFramework_registersActorManager_whenSpotNodeAndActorFactoryExist() {
        DefaultZLinkFrameworkOptions options = optionsWithSpotNodeAndActorFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();

            assertTrue(actor instanceof PlayerActor);
        }
    }

    @Test
    void addZLinkFramework_allowsStandaloneLocalSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));

        assertDoesNotThrow(options::validate);
    }

    @Test
    void addZLinkFramework_throws_whenStreamNodeRegistersMultipleSessions() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            options.addStreamNode("gateway", stream -> {
                stream.bind("inproc://gateway");
                stream.registerSession(GameSession.class);
                stream.registerSession(GameSession.class);
            }));
    }

    private static DefaultZLinkFrameworkOptions optionsWithSpotNodeAndActorFactory() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addSpotFactory(GameSpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        return options;
    }

    public static final class GameSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class EntrySpotA implements ZLinkEntrySpot {
        @Override
        public ZLinkEntrySpotContext context() {
            return null;
        }
    }

    public static final class EntrySpotB implements ZLinkEntrySpot {
        @Override
        public ZLinkEntrySpotContext context() {
            return null;
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
