package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;

final class ChannelMessagingTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(32_000 + (int) (ProcessHandle.current().pid() % 10_000));
    private static final AtomicReference<CountDownLatch> FANOUT_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> FANOUT_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> FANOUT_TOPIC = new AtomicReference<>();

    @Test
    void manualClientServer_requestReplySucceeds() {
        String endpoint = "inproc://zlink-java-profile-" + UUID.randomUUID();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(endpoint));
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", "hello")
                .packetName("Echo")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("hello", reply);
        }
    }

    @Test
    void discoveryClientServer_requestReplySucceeds() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String serverEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        DefaultZLinkFrameworkOptions serverOptions = new DefaultZLinkFrameworkOptions();
        serverOptions.useDiscovery(registry -> registry.add(registryRouter));
        serverOptions.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(serverEndpoint));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        clientOptions.setDefaultTimeout(Duration.ofMillis(100));
        clientOptions.useDiscovery(registry -> registry.add(registryRouter));
        clientOptions.addClientServerChannel("profile", channel -> channel.enableClient());

        try (ZLinkRegistryRuntime ignoredRegistry = new ZLinkRegistryRuntime(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime ignoredServer =
                 ZLinkFrameworkRuntime.start(serverOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime client =
                 ZLinkFrameworkRuntime.start(clientOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("hello", awaitDiscoveryReply(client));
        }
    }

    @Test
    void publisherAndSubscriber_workAcrossHosts() throws InterruptedException {
        String endpoint = tcpEndpoint();
        CountDownLatch latch = new CountDownLatch(1);
        FANOUT_LATCH.set(latch);
        FANOUT_MESSAGE.set(null);
        FANOUT_TOPIC.set(null);

        DefaultZLinkFrameworkOptions publisherOptions = new DefaultZLinkFrameworkOptions();
        publisherOptions.addFanoutChannel("events", channel ->
            channel.enablePublisher(publisher -> publisher.bind(endpoint)));

        DefaultZLinkFrameworkOptions subscriberOptions = new DefaultZLinkFrameworkOptions();
        subscriberOptions.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addPublishHandler(ScoreChangedHandler.class, String.class, "ScoreChanged");
        });

        try (ZLinkFrameworkRuntime ignoredPublisher =
                 ZLinkFrameworkRuntime.start(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime subscriber =
                 ZLinkFrameworkRuntime.start(subscriberOptions, new ZLinkJavaBackendAdapterFactory())) {
            publishUntilDelivered(ignoredPublisher);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "fanout publish was not delivered");
            assertEquals("home:1", FANOUT_MESSAGE.get());
            assertEquals("score", FANOUT_TOPIC.get());
        } finally {
            FANOUT_LATCH.set(null);
            FANOUT_MESSAGE.set(null);
            FANOUT_TOPIC.set(null);
        }
    }

    @Test
    void routeMesh_requestByRoutingIdSucceeds() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("source-node");
        RoutingId targetRid = RoutingId.from("target-node");

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        sourceOptions.addRouteMeshChannel("route", channel -> {
            channel.bind(sourceEndpoint);
            channel.configureRouting(route -> route.setRoutingId(sourceRid));
            channel.useManualConnections(endpoints -> endpoints.connect(targetEndpoint));
        });

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        targetOptions.addRouteMeshChannel("route", channel -> {
            channel.bind(targetEndpoint);
            channel.configureRouting(route -> route.setRoutingId(targetRid));
            channel.useManualConnections(endpoints -> endpoints.connect(sourceEndpoint));
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo");
        });

        try (ZLinkFrameworkRuntime ignoredSource =
                 ZLinkFrameworkRuntime.start(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime target =
                 ZLinkFrameworkRuntime.start(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("route:hello", awaitRouteReply(ignoredSource, targetRid));
        }
    }

    private static String awaitDiscoveryReply(ZLinkFrameworkRuntime client) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return client.client()
                    .requestToChannel("profile", "hello")
                    .packetName("Echo")
                    .timeout(Duration.ofMillis(100))
                    .submitAsync(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("discovery request did not succeed", lastFailure);
    }

    private static void publishUntilDelivered(ZLinkFrameworkRuntime publisher) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && FANOUT_LATCH.get().getCount() > 0) {
            publisher.fanout()
                .publish("events", "score", "home:1")
                .packetName("ScoreChanged")
                .submitAsync()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
    }

    private static String awaitRouteReply(ZLinkFrameworkRuntime source, RoutingId targetRid) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return source.route()
                    .requestTo("route", targetRid, "hello")
                    .packetName("Echo")
                    .timeout(Duration.ofMillis(100))
                    .submitAsync(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("route mesh request did not succeed", lastFailure);
    }

    private static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + nextPort();
    }

    private static int nextPort() {
        for (int attempt = 0; attempt < 200; attempt++) {
            int port = NEXT_PORT.getAndIncrement();
            if (isBindable(port)) {
                return port;
            }
        }
        throw new IllegalStateException("failed to allocate tcp port");
    }

    private static boolean isBindable(int port) {
        try (ServerSocket server = new ServerSocket(port)) {
            server.setReuseAddress(false);
            return true;
        } catch (IOException ignored) {
            return false;
        }
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class ScoreChangedHandler implements ZLinkPublishHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String message, ZLinkPublishContext context) {
            FANOUT_MESSAGE.set(message);
            FANOUT_TOPIC.set(context.topic());
            FANOUT_LATCH.get().countDown();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class RouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRouteRequestContext context) {
            return CompletableFuture.completedFuture("route:" + request);
        }
    }
}
