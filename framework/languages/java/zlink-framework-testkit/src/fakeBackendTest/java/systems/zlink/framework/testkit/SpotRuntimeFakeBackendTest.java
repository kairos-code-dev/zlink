package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class SpotRuntimeFakeBackendTest {
    @Test
    void spotManagerCreateListFindAndRemoveUseBackendSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> router.setRouterBind("inproc://spot-router"));
                node.enablePubSub(pubsub -> pubsub.setPubBind("inproc://spot-pub"));
                node.addSpotFactory(GameSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId rid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            assertEquals(true, runtime.spotManager()
                .createAsync(GameSpot.class, rid)
                .toCompletableFuture()
                .join()
                .created());
            assertEquals(false, runtime.spotManager()
                .getOrCreateAsync(GameSpot.class, rid)
                .toCompletableFuture()
                .join()
                .created());
            assertEquals(Optional.of(rid), runtime.spotManager()
                .findAsync(rid)
                .toCompletableFuture()
                .join()
                .map(info -> info.spotRid()));
            assertEquals(List.of(rid), runtime.spotManager()
                .listAsync()
                .toCompletableFuture()
                .join()
                .stream()
                .map(info -> info.spotRid())
                .toList());
            assertEquals(true, runtime.spotManager()
                .removeAsync(rid)
                .toCompletableFuture()
                .join());
            assertEquals(false, runtime.spotManager()
                .removeAsync(rid)
                .toCompletableFuture()
                .join());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://spot-router",
                "spotNode.setPubBind.inproc://spot-pub",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "close.spot.1",
                "close.context",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
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
