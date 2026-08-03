package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.spots.ZLinkInstanceSpot;
import systems.zlink.framework.spots.ZLinkInstanceSpotContext;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.actors.ZLinkActor;

final class InstanceSpotRuntimeIntegrationTest {
    @Test
    void publicRequestColdActivatesApplicationInstanceOnRemoteNode()
        throws Exception {
        Zlink.version();
        EchoInstanceSpot.initializations.set(0);
        EchoInstanceSpot.sends.set(0);
        EchoInstanceSpot.closes.set(null);
        SourceEntrySpot.reset();
        String suffix = Long.toUnsignedString(System.nanoTime(), 36);
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        var store = new ZLinkInMemoryLocationStore();

        var targetOptions = new DefaultZLinkFrameworkOptions();
        targetOptions.addLocationStore(store);
        targetOptions.configureLocations().setPollingInterval(
            Duration.ofMillis(20));
        var targetNode = targetOptions.addRouteMesh("game");
        targetNode.listen(targetEndpoint)
            .setRoutingId(RoutingId.from("instance-target-" + suffix));
        targetNode.objects().server().addInstanceSpotFactory(
            "EchoInstance",
            EchoInstanceSpot.class,
            factory -> factory.disableRelocation());

        var sourceOptions = new DefaultZLinkFrameworkOptions();
        sourceOptions.addLocationStore(store);
        sourceOptions.configureLocations().setPollingInterval(
            Duration.ofMillis(20));
        var sourceNode = sourceOptions.addRouteMesh("game");
        sourceNode.listen(sourceEndpoint)
            .setRoutingId(RoutingId.from("instance-source-" + suffix));
        sourceNode.objects().client();
        sourceNode.objects().server().addEntrySpot(SourceEntrySpot.class);

        try (ZLinkFrameworkRuntime target = RuntimeTestSupport.startFramework(
                 targetOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime source = RuntimeTestSupport.startFramework(
                 sourceOptions, new ZLinkJavaBackendAdapterFactory())) {
            SourceEntrySpot.request.set(new Request("echo-" + suffix));
            SourceEntrySpot.start.complete(null);
            String reply = SourceEntrySpot.reply.get();

            assertEquals("echo:hello|echo:again|echo:after-close", reply);
            assertEquals(2, EchoInstanceSpot.initializations.get());
            assertEquals(1, EchoInstanceSpot.sends.get());
            assertTrue(EchoInstanceSpot.closes.get());
        }
    }

    private record Request(String spotId) {}

    private record Warmup(String value) {}

    private record CloseInstance() {}

    public static final class SourceEntrySpot
        implements ZLinkEntrySpot<ZLinkActor> {
        static CompletableFuture<Void> start;
        static java.util.concurrent.atomic.AtomicReference<Request> request;
        static CompletableFuture<String> reply;
        private final ZLinkEntrySpotContext context;

        public SourceEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        static void reset() {
            start = new CompletableFuture<>();
            request = new java.util.concurrent.atomic.AtomicReference<>();
            reply = new CompletableFuture<>();
        }

        @Override public ZLinkEntrySpotContext context() { return context; }
        @Override public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
        @Override public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onInitialize() {
            return start.thenCompose(ignored -> {
                CompletionStage<Void> warmup = context.outbound()
                    .sendToSpot(request.get().spotId(), new Warmup("warmup"))
                    .instanceSpot("EchoInstance")
                    .inMesh("game")
                    .submit();
                CompletionStage<String> first = warmup.thenCompose(
                    sendCompleted -> context.outbound()
                        .requestToSpot(request.get().spotId(), "hello")
                        .instanceSpot("EchoInstance")
                        .inMesh("game")
                        .timeout(Duration.ofSeconds(5))
                        .submit(String.class));
                CompletionStage<String> firstAndSecond = first.thenCompose(
                    firstValue -> {
                        return context.outbound()
                            .requestToSpot(request.get().spotId(), "again")
                            .instanceSpot()
                            .inMesh("game")
                            .timeout(Duration.ofSeconds(5))
                            .submit(String.class)
                            .thenApply(secondValue -> firstValue + "|"
                                + secondValue);
                    });
                CompletionStage<String> beforeAfterClose =
                    firstAndSecond.thenCompose(value -> {
                        return context.outbound()
                            .sendToSpot(
                                request.get().spotId(),
                                new CloseInstance())
                            .instanceSpot()
                            .inMesh("game")
                            .submit()
                            .thenApply(ignoredClose -> value);
                    });
                CompletableFuture<String> completion =
                    beforeAfterClose.thenCompose(value ->
                        context.outbound()
                            .requestToSpot(
                                request.get().spotId(),
                                "after-close")
                            .instanceSpot()
                            .inMesh("game")
                            .timeout(Duration.ofSeconds(5))
                            .submit(String.class)
                            .thenApply(after -> value + "|" + after))
                        .toCompletableFuture();
                completion.whenComplete((value, failure) -> {
                    if (failure == null) {
                        reply.complete(value);
                    } else {
                        reply.completeExceptionally(failure);
                    }
                });
                return completion.thenApply(value -> null);
            });
        }
    }

    private static String tcpEndpoint() throws IOException {
        try (ServerSocket socket = new ServerSocket(0)) {
            return "tcp://127.0.0.1:" + socket.getLocalPort();
        }
    }

    public static final class EchoInstanceSpot implements ZLinkInstanceSpot {
        static final AtomicInteger initializations = new AtomicInteger();
        static final AtomicInteger sends = new AtomicInteger();
        static final java.util.concurrent.atomic.AtomicReference<Boolean> closes =
            new java.util.concurrent.atomic.AtomicReference<>();
        private final ZLinkInstanceSpotContext context;

        public EchoInstanceSpot(ZLinkInstanceSpotContext context) {
            this.context = context;
        }

        @Override public ZLinkInstanceSpotContext context() { return context; }

        @Override
        public void configure() {
            context.handlers().addPacket(EchoHandler.class);
            context.handlers().addPacket(EchoPacketHandler.class);
            context.handlers().addPacket(CloseHandler.class);
        }

        @Override
        public CompletionStage<Void> onInitialize() {
            initializations.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class EchoHandler
        implements ZLinkSpotRequestHandler<EchoInstanceSpot, String, String> {
        @Override
        public CompletionStage<String> handle(
            EchoInstanceSpot spot,
            String request) {
            return CompletableFuture.completedFuture("echo:" + request);
        }
    }

    public static final class EchoPacketHandler
        implements ZLinkSpotPacketHandler<EchoInstanceSpot, Warmup> {
        @Override
        public CompletionStage<Void> handle(
            EchoInstanceSpot spot,
            Warmup request) {
            EchoInstanceSpot.sends.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class CloseHandler
        implements ZLinkSpotPacketHandler<EchoInstanceSpot, CloseInstance> {
        @Override
        public CompletionStage<Void> handle(
            EchoInstanceSpot spot,
            CloseInstance request) {
            return spot.context().close().thenApply(closed -> {
                EchoInstanceSpot.closes.set(closed);
                return null;
            });
        }
    }
}
