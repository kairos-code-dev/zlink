package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;

/**
 * Entry Spot serial dispatch regression: every Entry Spot callback — actor
 * packets across different actors, lifecycle callbacks, and worker completions —
 * runs on the one Entry Spot serial line, never overlapping.
 */
final class EntrySpotSerialDispatchTest {

    @Test
    void entrySpotActorPacketsAreSerializedAcrossActors() throws Exception {
        SerialDispatchHandlers.reset();
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();
        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options(), backendFactory)) {
            runtime.actorManager().create("serial-a", "player").toCompletableFuture().join();
            runtime.actorManager().create("serial-b", "player").toCompletableFuture().join();

            backendFactory.dispatchEntrySpotActorMessage("serial-a", "SerialBlock", "block-a");
            assertTrue(
                SerialDispatchHandlers.blockStarted.await(5, TimeUnit.SECONDS),
                "blocking handler did not start");

            backendFactory.dispatchEntrySpotActorMessage("serial-b", "SerialRecord", "record-b");
            backendFactory.dispatchEntrySpotActorMessage("serial-a", "SerialRecord", "after-a");

            // The blocking handler holds the Entry Spot line: no other packet
            // may run, regardless of which actor it targets.
            Thread.sleep(300);
            assertTrue(
                SerialDispatchHandlers.events.stream().noneMatch(e -> e.startsWith("record:")),
                "a packet ran while the Entry Spot line was held: "
                    + SerialDispatchHandlers.events);

            SerialDispatchHandlers.releaseBlock.countDown();
            assertTrue(
                SerialDispatchHandlers.allRecorded.await(5, TimeUnit.SECONDS),
                "queued packets did not run after release");

            List<String> events = List.copyOf(SerialDispatchHandlers.events);
            int blockEnd = events.indexOf("block-end:block-a");
            int recordB = events.indexOf("record:record-b");
            int afterA = events.indexOf("record:after-a");
            assertTrue(blockEnd >= 0, "missing block-end event: " + events);
            assertTrue(blockEnd < recordB, "other-actor packet overtook the line: " + events);
            assertTrue(recordB < afterA, "dispatch order was not preserved: " + events);
        }
    }

    @Test
    void runWorkerCompletionRunsOnEntryLineAndObservesMutatedState() throws Exception {
        SerialDispatchHandlers.reset();
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();
        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options(), backendFactory)) {
            runtime.actorManager().create("worker-a", "player").toCompletableFuture().join();

            ZLinkEntrySpotContext context = SerialEntrySpot.capturedContext.get();
            assertTrue(context != null, "entry spot context was not captured");

            CountDownLatch releaseWork = new CountDownLatch(1);
            AtomicReference<String> workerThread = new AtomicReference<>();
            AtomicReference<String> entryState = new AtomicReference<>("initial");
            CompletableFuture<String> observedState = new CompletableFuture<>();
            CompletableFuture<String> callbackThread = new CompletableFuture<>();

            context.runWorker(token -> {
                workerThread.set(Thread.currentThread().getName());
                releaseWork.await();
                return 1;
            }).submit(
                (value, token) -> {
                    callbackThread.complete(Thread.currentThread().getName());
                    observedState.complete(entryState.get());
                },
                (error, token) -> {
                    callbackThread.completeExceptionally(error);
                    observedState.completeExceptionally(error);
                });

            // Mutate the entry state from an Entry Spot packet handler while
            // the worker is still running (detached interleaving opt-in).
            SerialDispatchHandlers.stateRef = entryState;
            backendFactory.dispatchEntrySpotActorMessage("worker-a", "SerialMutate", "mutated");
            awaitCondition(() -> "mutated".equals(entryState.get()));
            releaseWork.countDown();

            assertEquals("mutated", observedState.get(5, TimeUnit.SECONDS));
            assertNotEquals(
                workerThread.get(),
                callbackThread.get(5, TimeUnit.SECONDS),
                "completion callback ran on the worker thread");
        }
    }

    private static DefaultZLinkFrameworkOptions options() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(EntrySpotSerialDispatchTest.class);
        { var mesh = options.addSpotMesh("serial"); { var node = mesh.addNode("serial-node"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("serial-entry-spot")); };
                node.addEntrySpot(SerialEntrySpot.class); }; };
        options.addActorFactory("player", SpotRuntimeFakeBackendTest.PlayerActorFactory.class);
        return options;
    }

    private static void awaitCondition(java.util.function.Supplier<Boolean> condition)
        throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!condition.get()) {
            if (System.nanoTime() > deadline) {
                throw new AssertionError("condition was not reached in time");
            }
            Thread.sleep(10);
        }
    }

    public static final class SerialEntrySpot
        implements systems.zlink.framework.spots.ZLinkEntrySpot {
        static final AtomicReference<ZLinkEntrySpotContext> capturedContext =
            new AtomicReference<>();
        private final ZLinkEntrySpotContext context;

        public SerialEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
            capturedContext.set(context);
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void configure() {
        }
    }

    @ZLinkHandlerGroup("serial")
    public static final class SerialDispatchHandlers {
        static volatile CountDownLatch blockStarted = new CountDownLatch(1);
        static volatile CountDownLatch releaseBlock = new CountDownLatch(1);
        static volatile CountDownLatch allRecorded = new CountDownLatch(2);
        static final ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();
        static volatile AtomicReference<String> stateRef = new AtomicReference<>();

        static void reset() {
            blockStarted = new CountDownLatch(1);
            releaseBlock = new CountDownLatch(1);
            allRecorded = new CountDownLatch(2);
            events.clear();
            stateRef = new AtomicReference<>();
        }

        @ZLinkSpotActorSend(packetName = "SerialBlock")
        public void block(SpotRuntimeFakeBackendTest.PlayerActor actor, String message)
            throws InterruptedException {
            events.add("block-start:" + message);
            blockStarted.countDown();
            releaseBlock.await();
            events.add("block-end:" + message);
        }

        @ZLinkSpotActorSend(packetName = "SerialRecord")
        public void record(SpotRuntimeFakeBackendTest.PlayerActor actor, String message) {
            events.add("record:" + message);
            allRecorded.countDown();
        }

        @ZLinkSpotActorSend(packetName = "SerialMutate")
        public void mutate(SpotRuntimeFakeBackendTest.PlayerActor actor, String message) {
            stateRef.set(message);
        }
    }
}
