package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;

final class SpotManagerTest {
    @Test
    void spotManager_createListRemoveAndPublish_workThroughFrameworkRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
        RoutingId spotRid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            assertTrue(runtime.spotManager()
                .createAsync(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join()
                .created());
            assertEquals(spotRid, runtime.spotManager()
                .findAsync(spotRid)
                .toCompletableFuture()
                .join()
                .orElseThrow()
                .spotRid());
            assertEquals(1, runtime.spotManager()
                .listAsync()
                .toCompletableFuture()
                .join()
                .size());
            assertTrue(runtime.spotManager()
                .removeAsync(spotRid)
                .toCompletableFuture()
                .join());
        }
    }

    @Test
    void spotManager_getOrCreate_createsOnceAndReusesExistingSpot() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
        RoutingId spotRid = RoutingId.from("game-once");

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            assertTrue(runtime.spotManager()
                .getOrCreateAsync(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join()
                .created());
            assertFalse(runtime.spotManager()
                .getOrCreateAsync(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join()
                .created());
        }
    }

    public static final class GameSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onInitializeAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }
}
