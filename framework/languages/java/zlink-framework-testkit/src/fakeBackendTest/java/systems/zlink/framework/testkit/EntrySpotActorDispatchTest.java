package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;

final class EntrySpotActorDispatchTest {

    @Test
    void entrySpotActorDispatch_usesActorMailboxWithoutEntrySpotWideSerialLine()
        throws Exception {
        EntryActorDispatchHandlers.reset();
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();
        ZLinkFrameworkRuntime runtime = RuntimeTestSupport.startFramework(options(), backendFactory);
        try {
            managedActor(runtime, "actor-a", "player");
            managedActor(runtime, "actor-b", "player");

            backendFactory.dispatchEntrySpotActorMessage("actor-a", "String", "\"block:first-a\"");
            assertTrue(
                EntryActorDispatchHandlers.actorAStarted.await(5, TimeUnit.SECONDS),
                () -> "actor-a first packet did not start; events="
                    + EntryActorDispatchHandlers.events
                    + ", calls="
                    + backendFactory.calls());

            backendFactory.dispatchEntrySpotActorMessage("actor-b", "String", "\"record:first-b\"");
            assertTrue(
                EntryActorDispatchHandlers.actorBStarted.await(5, TimeUnit.SECONDS),
                "actor-b was blocked by the Entry Spot-wide serial line");

            backendFactory.dispatchEntrySpotActorMessage("actor-a", "String", "\"record:second-a\"");
            Thread.sleep(300);
            assertFalse(
                EntryActorDispatchHandlers.actorASecondStarted.getCount() == 0,
                "same actor packet interleaved while actor-a first packet was still running");

            EntryActorDispatchHandlers.releaseActorA.countDown();
            assertTrue(
                EntryActorDispatchHandlers.actorASecondStarted.await(5, TimeUnit.SECONDS),
                "actor-a second packet did not run after actor-a first packet completed");

            List<String> events = List.copyOf(EntryActorDispatchHandlers.events);
            int actorAStart = events.indexOf("actor-a:first-a:start");
            int actorBStart = events.indexOf("actor-b:first-b:start");
            int actorAEnd = events.indexOf("actor-a:first-a:end");
            int actorASecond = events.indexOf("actor-a:second-a:start");
            assertTrue(actorAStart >= 0, "missing actor-a start event: " + events);
            assertTrue(actorBStart >= 0, "missing actor-b start event: " + events);
            assertTrue(actorAEnd >= 0, "missing actor-a end event: " + events);
            assertTrue(actorASecond >= 0, "missing actor-a second event: " + events);
            assertTrue(actorBStart < actorAEnd, "other actor waited for actor-a completion: " + events);
            assertTrue(actorAEnd < actorASecond, "same actor order was not preserved: " + events);
        } finally {
            EntryActorDispatchHandlers.releaseActorA.countDown();
            runtime.close();
        }
    }

    @Test
    void entrySpotActorDispatch_workerCompletionStageCompletesInsideActorHandler()
        throws Exception {
        EntryActorDispatchHandlers.reset();
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();
        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options(), backendFactory)) {
            managedActor(runtime, "actor-yield", "player");

            backendFactory.dispatchEntrySpotActorMessage("actor-yield", "String", "\"yield\"");

            assertTrue(
                EntryActorDispatchHandlers.yieldObserved.await(5, TimeUnit.SECONDS),
                () -> "yield misuse was not observed; events="
                    + EntryActorDispatchHandlers.events
                    + ", calls="
                    + backendFactory.calls());
            Throwable error = EntryActorDispatchHandlers.yieldError.get();
            assertEquals(null, error);
        }
    }

    @Test
    void entrySpotRequestHandlerCanSynchronouslyAwaitActorCreation() throws Exception {
        EntryActorDispatchHandlers.reset();
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();
        CountDownLatch replied = new CountDownLatch(1);
        backendFactory.onSpotReply(reply -> replied.countDown());
        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options(), backendFactory)) {
            backendFactory.dispatchEntrySpotRequest(
                "EnsureActorRequest",
                "{\"actorId\":\"actor-created-from-request\"}",
                7);

            assertTrue(
                EntryActorDispatchHandlers.actorCreated.await(5, TimeUnit.SECONDS),
                () -> "actor create callback did not run; events="
                    + EntryActorDispatchHandlers.events
                    + ", calls="
                    + backendFactory.calls());
            assertTrue(
                replied.await(5, TimeUnit.SECONDS),
                () -> "entry spot request did not reply; events="
                    + EntryActorDispatchHandlers.events
                    + ", calls="
                    + backendFactory.calls());
        }

        assertEquals(
            List.of("{\"actorId\":\"actor-created-from-request\"}"),
            backendFactory.spotReplies());
        assertTrue(EntryActorDispatchHandlers.events.contains("created:actor-created-from-request"));
    }

    private static DefaultZLinkFrameworkOptions options() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(EntrySpotActorDispatchTest.class);
        var node = options.addSpotMesh("entry-actor-dispatch");
        var entry = node.configureEntrySpot();
        entry.setRoutingId(RoutingId.from("entry-actor-dispatch-entry"));
        node.addEntrySpot(EntryActorDispatchSpot.class);
        node.addActorFactory("player", PlayerActorFactory.class);
        return options;
    }

    private static ZLinkActor managedActor(
        ZLinkFrameworkRuntime runtime,
        String actorId,
        String actorType) {
        return ((ZLinkActorRuntime) runtime.actorManager())
            .getOrCreateManagedActor(actorId, actorType)
            .toCompletableFuture()
            .join();
    }

    public static final class EntryActorDispatchSpot extends TestZLinkEntrySpot<ZLinkActor> {
        private final ZLinkEntrySpotContext context;

        public EntryActorDispatchSpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addHandler(EnsureActorRequestHandler.class);
        }

        @Override
        public CompletionStage<Void> onCreateActor(
            ZLinkActor actor,
            ZLinkMessage createRequest) {
            EntryActorDispatchHandlers.events.add("created:" + actor.actorId());
            EntryActorDispatchHandlers.actorCreated.countDown();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class EnsureActorRequestHandler
        implements ZLinkSpotRequestHandler<EntryActorDispatchSpot, EnsureActorRequest, EnsureActorReply> {
        private final ZLinkActorManager actors;

        public EnsureActorRequestHandler(ZLinkActorManager actors) {
            this.actors = actors;
        }

        @Override
        public CompletionStage<EnsureActorReply> handle(
            EntryActorDispatchSpot spot,
            EnsureActorRequest request) {
            return actors
                .getOrCreate(request.actorId(), "player")
                .thenApply(actor -> new EnsureActorReply(actor.actorId()));
        }
    }

    public record EnsureActorRequest(String actorId) {
    }

    public record EnsureActorReply(String actorId) {
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
        public CompletionStage<ZLinkActor> create(String actorId, ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class EntryActorDispatchHandlers {
        static volatile CountDownLatch actorAStarted = new CountDownLatch(1);
        static volatile CountDownLatch actorBStarted = new CountDownLatch(1);
        static volatile CountDownLatch actorASecondStarted = new CountDownLatch(1);
        static volatile CountDownLatch actorCreated = new CountDownLatch(1);
        static volatile CountDownLatch releaseActorA = new CountDownLatch(1);
        static volatile CountDownLatch yieldObserved = new CountDownLatch(1);
        static final ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();
        static final AtomicReference<Throwable> yieldError = new AtomicReference<>();

        static void reset() {
            actorAStarted = new CountDownLatch(1);
            actorBStarted = new CountDownLatch(1);
            actorASecondStarted = new CountDownLatch(1);
            actorCreated = new CountDownLatch(1);
            releaseActorA = new CountDownLatch(1);
            yieldObserved = new CountDownLatch(1);
            events.clear();
            yieldError.set(null);
        }

        @ZLinkSpotActorSend
        public CompletionStage<Void> handle(
            EntryActorDispatchSpot entrySpot,
            ZLinkActor actor,
            ZLinkSpotActorSendContext context,
            String message) throws InterruptedException {
            assertEquals(Optional.of("String"), context.packetName());
            if (message.startsWith("block:")) {
                String value = message.substring("block:".length());
                events.add(actor.actorId() + ":" + value + ":start");
                actorAStarted.countDown();
                releaseActorA.await();
                events.add(actor.actorId() + ":" + value + ":end");
                return CompletableFuture.completedFuture(null);
            }
            if (message.startsWith("record:")) {
                String value = message.substring("record:".length());
                events.add(actor.actorId() + ":" + value + ":start");
                if ("actor-b".equals(actor.actorId())) {
                    actorBStarted.countDown();
                }
                if ("actor-a".equals(actor.actorId())) {
                    actorASecondStarted.countDown();
                }
                return CompletableFuture.completedFuture(null);
            }
            if ("yield".equals(message)) {
                return entrySpot.context().runCpuWorker(() -> message).submit()
                    .thenAccept(ignored -> yieldObserved.countDown())
                    .exceptionally(error -> {
                        yieldError.set(error);
                        yieldObserved.countDown();
                        return null;
                    });
            }
            return CompletableFuture.completedFuture(null);
        }
    }
}
