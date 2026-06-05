package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;

final class SpotManagerTest {
    @Test
    void spotManager_createListRemoveAndPublish_workThroughFrameworkRuntime() {
        Zlink.version();
        String suffix = Long.toUnsignedString(System.nanoTime(), 36);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.setRoutingId(RoutingId.from("spot-manager-node-" + suffix));
                    router.bindRouter("inproc://spot-manager-router-" + suffix);
                });
                node.addSpotFactory(GameSpot.class);
            }));
        RoutingId spotRid = RoutingId.from("game-1-" + suffix);

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
        String suffix = Long.toUnsignedString(System.nanoTime(), 36);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.setRoutingId(RoutingId.from("spot-once-node-" + suffix));
                    router.bindRouter("inproc://spot-once-router-" + suffix);
                });
                node.addSpotFactory(GameSpot.class);
            }));
        RoutingId spotRid = RoutingId.from("game-once-" + suffix);

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

    @Test
    void spot_publishTimerAndRemove_stopCallbacksWork() throws InterruptedException {
        Zlink.version();
        PublishingSpot.reset();
        String suffix = Long.toUnsignedString(System.nanoTime(), 36);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.setRoutingId(RoutingId.from("spot-timer-router-node-" + suffix));
                    router.bindRouter("inproc://spot-timer-router-" + suffix);
                });
                node.enablePubSub(pubsub -> {
                    pubsub.setRoutingId(RoutingId.from("spot-pub-node-" + suffix));
                    pubsub.bindPubSub("inproc://spot-pub-" + suffix);
                });
                node.addSpotFactory(PublishingSpot.class);
            }));
        RoutingId spotRid = RoutingId.from("timer-room-" + suffix);

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            assertTrue(runtime.spotManager()
                .createAsync(PublishingSpot.class, spotRid)
                .toCompletableFuture()
                .join()
                .created());
            assertTrue(PublishingSpot.timerPublished.await(3, TimeUnit.SECONDS));

            assertTrue(runtime.spotManager()
                .removeAsync(spotRid)
                .toCompletableFuture()
                .join());
            assertTrue(PublishingSpot.closed.await(1, TimeUnit.SECONDS));

            int ticksAtRemove = PublishingSpot.ticks.get();
            PublishingSpot.removed.set(true);
            assertFalse(PublishingSpot.afterRemoveTick.await(100, TimeUnit.MILLISECONDS));
            assertEquals(ticksAtRemove, PublishingSpot.ticks.get());
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

    public static final class PublishingSpot implements ZLinkSpot {
        static final AtomicInteger ticks = new AtomicInteger();
        static final AtomicBoolean removed = new AtomicBoolean();
        static CountDownLatch timerPublished;
        static CountDownLatch closed;
        static CountDownLatch afterRemoveTick;

        private final ZLinkSpotContext context;

        public PublishingSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        static void reset() {
            ticks.set(0);
            removed.set(false);
            timerPublished = new CountDownLatch(1);
            closed = new CountDownLatch(1);
            afterRemoveTick = new CountDownLatch(1);
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onInitializeAsync() {
            return context.addTimer(
                    "heartbeat",
                    Duration.ofMillis(10),
                    HeartbeatTimerHandler.class,
                    null)
                .thenApply(timer -> null);
        }

        @Override
        public CompletionStage<Void> onClosingAsync() {
            closed.countDown();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class HeartbeatTimerHandler implements ZLinkSpotTimerHandler<PublishingSpot> {
        @Override
        public CompletionStage<Void> handleAsync(PublishingSpot spot, ZLinkTimerTick tick) {
            if (PublishingSpot.removed.get()) {
                PublishingSpot.afterRemoveTick.countDown();
            }
            PublishingSpot.ticks.incrementAndGet();
            return spot.context()
                .outbound()
                .publish("heartbeat", "tick")
                .packetName("Heartbeat")
                .submitAsync()
                .thenRun(PublishingSpot.timerPublished::countDown);
        }
    }
}
