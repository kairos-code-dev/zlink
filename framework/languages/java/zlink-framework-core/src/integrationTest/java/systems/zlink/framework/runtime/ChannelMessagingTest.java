package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

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
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendContext;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;

final class ChannelMessagingTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(32_000 + (int) (ProcessHandle.current().pid() % 10_000));
    private static final AtomicReference<CountDownLatch> FANOUT_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> FANOUT_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> FANOUT_TOPIC = new AtomicReference<>();
    private static final AtomicReference<CountDownLatch> SEND_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> SEND_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> SEND_PACKET = new AtomicReference<>();
    private static final AtomicReference<CountDownLatch> ROUTE_SEND_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_PACKET = new AtomicReference<>();
    private static final AtomicReference<RoutingId> ROUTE_SEND_SOURCE = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_REQUEST = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_PACKET = new AtomicReference<>();

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
    void handlerFiltersWrapChannelRequestDispatch() {
        String endpoint = "inproc://zlink-java-filtered-profile-" + UUID.randomUUID();
        FILTER_REQUEST.set(null);
        FILTER_PACKET.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useFilter(ReplyDecoratingFilter.class);
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

            assertEquals("filtered:hello", reply);
            assertEquals("hello", FILTER_REQUEST.get());
            assertEquals("Echo", FILTER_PACKET.get());
        } finally {
            FILTER_REQUEST.set(null);
            FILTER_PACKET.set(null);
        }
    }

    @Test
    void manualClientServer_sendDispatchesToHandler() throws InterruptedException {
        String endpoint = "inproc://zlink-java-notify-" + UUID.randomUUID();
        CountDownLatch latch = new CountDownLatch(1);
        SEND_LATCH.set(latch);
        SEND_MESSAGE.set(null);
        SEND_PACKET.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(endpoint));
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addSendHandler(ProfileChangedHandler.class, String.class, "ProfileChanged");
        });

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            sendUntilDelivered(runtime);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "client/server send was not delivered");
            assertEquals("changed", SEND_MESSAGE.get());
            assertEquals("ProfileChanged", SEND_PACKET.get());
        } finally {
            SEND_LATCH.set(null);
            SEND_MESSAGE.set(null);
            SEND_PACKET.set(null);
        }
    }

    @Test
    void scannedHandlerGroup_requestReplySucceeds() {
        String endpoint = "inproc://zlink-java-scanned-profile-" + UUID.randomUUID();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(ChannelMessagingTest.class);
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(endpoint));
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addHandlerGroup("scanned-profile");
        });

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", "hello")
                .packetName("String")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("scanned:hello", reply);
        }
    }

    @Test
    void scannedMethodHandlerGroup_requestAndSendDispatch() throws InterruptedException {
        String endpoint = "inproc://zlink-java-annotated-profile-" + UUID.randomUUID();
        CountDownLatch latch = new CountDownLatch(1);
        SEND_LATCH.set(latch);
        SEND_MESSAGE.set(null);
        SEND_PACKET.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(ChannelMessagingTest.class);
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(endpoint));
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addHandlerGroup("annotated-profile");
        });

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", "hello")
                .packetName("AnnotatedEcho")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();
            runtime.client()
                .sendToChannel("profile", "changed")
                .packetName("ProfileChanged")
                .submitAsync()
                .toCompletableFuture()
                .join();

            assertEquals("annotated:hello", reply);
            assertTrue(latch.await(1, TimeUnit.SECONDS), "annotated send was not delivered");
            assertEquals("changed", SEND_MESSAGE.get());
            assertEquals("ProfileChanged", SEND_PACKET.get());
        } finally {
            SEND_LATCH.set(null);
            SEND_MESSAGE.set(null);
            SEND_PACKET.set(null);
        }
    }

    @Test
    void scannedMethodHandlerGroup_publishDispatches() throws InterruptedException {
        String endpoint = tcpEndpoint();
        CountDownLatch latch = new CountDownLatch(1);
        FANOUT_LATCH.set(latch);
        FANOUT_MESSAGE.set(null);
        FANOUT_TOPIC.set(null);

        DefaultZLinkFrameworkOptions publisherOptions = new DefaultZLinkFrameworkOptions();
        publisherOptions.addFanoutChannel("events", channel ->
            channel.enablePublisher(publisher -> publisher.bind(endpoint)));

        DefaultZLinkFrameworkOptions subscriberOptions = new DefaultZLinkFrameworkOptions();
        subscriberOptions.addHandlersFromPackageOf(ChannelMessagingTest.class);
        subscriberOptions.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addHandlerGroup("annotated-events");
        });

        try (ZLinkFrameworkRuntime ignoredPublisher =
                 ZLinkFrameworkRuntime.start(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime subscriber =
                 ZLinkFrameworkRuntime.start(subscriberOptions, new ZLinkJavaBackendAdapterFactory())) {
            publishUntilDelivered(ignoredPublisher);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "annotated publish was not delivered");
            assertEquals("home:1", FANOUT_MESSAGE.get());
            assertEquals("score", FANOUT_TOPIC.get());
        } finally {
            FANOUT_LATCH.set(null);
            FANOUT_MESSAGE.set(null);
            FANOUT_TOPIC.set(null);
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
    void discoveryClientServer_clientStartedBeforeServerFindsLaterProvider() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String serverEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        clientOptions.setDefaultTimeout(Duration.ofMillis(100));
        clientOptions.useDiscovery(registry -> registry.add(registryRouter));
        clientOptions.addClientServerChannel("profile", channel -> channel.enableClient());

        DefaultZLinkFrameworkOptions serverOptions = new DefaultZLinkFrameworkOptions();
        serverOptions.useDiscovery(registry -> registry.add(registryRouter));
        serverOptions.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind(serverEndpoint));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        try (ZLinkRegistryRuntime ignoredRegistry = new ZLinkRegistryRuntime(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime client =
                 ZLinkFrameworkRuntime.start(clientOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredServer =
                 ZLinkFrameworkRuntime.start(serverOptions, new ZLinkJavaBackendAdapterFactory())) {
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

    @Test
    void routeMesh_matchesRepliesByRequestSequenceWhenPacketNameIsShared() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-seq-source");
        RoutingId targetRid = RoutingId.from("route-seq-target");

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
            channel.addRequestHandler(DelayedRouteEchoHandler.class, String.class, String.class, "SharedPacket");
        });

        try (ZLinkFrameworkRuntime source =
                 ZLinkFrameworkRuntime.start(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 ZLinkFrameworkRuntime.start(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("warmup", awaitSharedRouteReply(source, targetRid, "warmup:1"));

            CompletionStage<String> slow = source.route()
                .requestTo("route", targetRid, "slow:40")
                .packetName("SharedPacket")
                .timeout(Duration.ofSeconds(3))
                .submitAsync(String.class);
            CompletionStage<String> fast = source.route()
                .requestTo("route", targetRid, "fast:1")
                .packetName("SharedPacket")
                .timeout(Duration.ofSeconds(3))
                .submitAsync(String.class);

            assertEquals("slow", slow.toCompletableFuture().join());
            assertEquals("fast", fast.toCompletableFuture().join());
        }
    }

    @Test
    void routeMesh_sendByRoutingIdDispatchesToHandler() throws InterruptedException {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-send-source");
        RoutingId targetRid = RoutingId.from("route-send-target");
        CountDownLatch latch = new CountDownLatch(1);
        ROUTE_SEND_LATCH.set(latch);
        ROUTE_SEND_MESSAGE.set(null);
        ROUTE_SEND_PACKET.set(null);
        ROUTE_SEND_SOURCE.set(null);

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
            channel.addSendHandler(RouteNoticeHandler.class, String.class, "Notice");
        });

        try (ZLinkFrameworkRuntime source =
                 ZLinkFrameworkRuntime.start(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 ZLinkFrameworkRuntime.start(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            routeSendUntilDelivered(source, targetRid);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "route mesh send was not delivered");
            assertEquals("ping", ROUTE_SEND_MESSAGE.get());
            assertEquals("Notice", ROUTE_SEND_PACKET.get());
            assertEquals(sourceRid, ROUTE_SEND_SOURCE.get());
        } finally {
            ROUTE_SEND_LATCH.set(null);
            ROUTE_SEND_MESSAGE.set(null);
            ROUTE_SEND_PACKET.set(null);
            ROUTE_SEND_SOURCE.set(null);
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

    private static void sendUntilDelivered(ZLinkFrameworkRuntime runtime) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && SEND_LATCH.get().getCount() > 0) {
            runtime.client()
                .sendToChannel("profile", "changed")
                .packetName("ProfileChanged")
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

    private static String awaitSharedRouteReply(
        ZLinkFrameworkRuntime source,
        RoutingId targetRid,
        String message) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return source.route()
                    .requestTo("route", targetRid, message)
                    .packetName("SharedPacket")
                    .timeout(Duration.ofMillis(100))
                    .submitAsync(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("shared packet route request did not succeed", lastFailure);
    }

    private static void routeSendUntilDelivered(ZLinkFrameworkRuntime source, RoutingId targetRid) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && ROUTE_SEND_LATCH.get().getCount() > 0) {
            source.route()
                .sendTo("route", targetRid, "ping")
                .packetName("Notice")
                .submitAsync()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
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

    public static final class ReplyDecoratingFilter implements ZLinkHandlerFilter {
        @Override
        public <T> CompletionStage<T> invokeAsync(
            ZLinkInvocationContext context,
            ZLinkNext<T> next) {
            FILTER_REQUEST.set((String) context.request().orElse(""));
            FILTER_PACKET.set(context.packetName().orElse(""));
            return next.invokeAsync().thenApply(reply -> {
                @SuppressWarnings("unchecked")
                T decorated = (T) ("filtered:" + reply);
                return decorated;
            });
        }
    }

    @ZLinkHandlerGroup("scanned-profile")
    public static final class ScannedEchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture("scanned:" + request);
        }
    }

    public static final class ProfileChangedHandler implements ZLinkSendHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String message, ZLinkSendContext context) {
            SEND_MESSAGE.set(message);
            SEND_PACKET.set(context.packetName().orElse(""));
            SEND_LATCH.get().countDown();
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("annotated-profile")
    public static final class AnnotatedProfileHandlers {
        @ZLinkRequest(packetName = "AnnotatedEcho")
        public String echo(String request) {
            return "annotated:" + request;
        }

        @ZLinkSend(packetName = "ProfileChanged")
        public void profileChanged(String message) {
            SEND_MESSAGE.set(message);
            SEND_PACKET.set("ProfileChanged");
            SEND_LATCH.get().countDown();
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

    @ZLinkHandlerGroup("annotated-events")
    public static final class AnnotatedEventHandlers {
        @ZLinkPublish(packetName = "ScoreChanged")
        public void scoreChanged(String message) {
            FANOUT_MESSAGE.set(message);
            FANOUT_TOPIC.set("score");
            FANOUT_LATCH.get().countDown();
        }
    }

    public static final class RouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRouteRequestContext context) {
            return CompletableFuture.completedFuture("route:" + request);
        }
    }

    public static final class DelayedRouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRouteRequestContext context) {
            String[] parts = request.split(":", 2);
            String value = parts[0];
            long delayMillis = parts.length == 2 ? Long.parseLong(parts[1]) : 0;
            return CompletableFuture.supplyAsync(
                () -> value,
                CompletableFuture.delayedExecutor(delayMillis, TimeUnit.MILLISECONDS));
        }
    }

    public static final class RouteNoticeHandler implements ZLinkRouteSendHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String message, ZLinkRouteSendContext context) {
            ROUTE_SEND_MESSAGE.set(message);
            ROUTE_SEND_PACKET.set(context.packetName().orElse(""));
            ROUTE_SEND_SOURCE.set(context.routingId());
            ROUTE_SEND_LATCH.get().countDown();
            return CompletableFuture.completedFuture(null);
        }
    }
}
