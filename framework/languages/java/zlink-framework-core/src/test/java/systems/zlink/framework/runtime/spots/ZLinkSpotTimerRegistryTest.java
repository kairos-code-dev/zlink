package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkTimerTick;

final class ZLinkSpotTimerRegistryTest {
    @Test
    void dispatchesTimerHandlerThroughSpotExecutionPolicy() throws Exception {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        CountDownLatch handled = new CountDownLatch(1);
        AtomicBoolean enteredDispatch = new AtomicBoolean();
        TimerHandler handler = new TimerHandler(handled, enteredDispatch);
        ZLinkSpotTimerRegistry registry = new ZLinkSpotTimerRegistry(
            RoutingId.from("spot"),
            executor,
            ignored -> handler,
            List.of(),
            null,
            "test",
            operation -> {
                enteredDispatch.set(true);
                return operation.get();
            });
        registry.setSpot(new TestSpot());

        try {
            registry.add("timer", Duration.ofMillis(1), TimerHandler.class, null);
            assertTrue(handled.await(2, TimeUnit.SECONDS));
            assertTrue(enteredDispatch.get());
        } finally {
            registry.close();
            executor.shutdownNow();
        }
    }

    public static final class TimerHandler {
        private final CountDownLatch handled;
        private final AtomicBoolean enteredDispatch;

        TimerHandler(CountDownLatch handled, AtomicBoolean enteredDispatch) {
            this.handled = handled;
            this.enteredDispatch = enteredDispatch;
        }

        public void handle(ZLinkSpot<?> spot, ZLinkTimerTick tick) {
            if (enteredDispatch.get()) {
                handled.countDown();
            }
        }
    }

    private static final class TestSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override public void onJoinedActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) { }
        @Override public void onLeaveActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) { }
    }
}
