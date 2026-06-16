package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
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
    private static final AtomicReference<String> FANOUT_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<CountDownLatch> SEND_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> SEND_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> SEND_PACKET = new AtomicReference<>();
    private static final AtomicReference<String> SEND_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<CountDownLatch> ROUTE_SEND_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_PACKET = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<RoutingId> ROUTE_SEND_SOURCE = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_REQUEST_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_REQUEST = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_PACKET = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_CHANNEL = new AtomicReference<>();

    @Test
    void manualClientServer_requestReplySucceeds() {
        String endpoint = "inproc://zlink-java-profile-" + UUID.randomUUID();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("Echo")
                .submit(String.class)
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
        FILTER_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useFilter(ReplyDecoratingFilter.class);
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("filtered:hello", reply);
            assertEquals("hello", FILTER_REQUEST.get());
            assertEquals("Echo", FILTER_PACKET.get());
            assertEquals("profile", FILTER_CHANNEL.get());
        } finally {
            FILTER_REQUEST.set(null);
            FILTER_PACKET.set(null);
            FILTER_CHANNEL.set(null);
        }
    }

    @Test
    void manualClientServer_sendDispatchesToHandler() throws InterruptedException {
        String endpoint = "inproc://zlink-java-notify-" + UUID.randomUUID();
        CountDownLatch latch = new CountDownLatch(1);
        SEND_LATCH.set(latch);
        SEND_MESSAGE.set(null);
        SEND_PACKET.set(null);
        SEND_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addSendHandler(ProfileChangedHandler.class, String.class, "ProfileChanged"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            sendUntilDelivered(runtime);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "client/server send was not delivered");
            assertEquals("changed", SEND_MESSAGE.get());
            assertEquals("ProfileChanged", SEND_PACKET.get());
            assertEquals("profile", SEND_CHANNEL.get());
        } finally {
            SEND_LATCH.set(null);
            SEND_MESSAGE.set(null);
            SEND_PACKET.set(null);
            SEND_CHANNEL.set(null);
        }
    }

    @Test
    void scannedHandlerGroup_requestReplySucceeds() {
        String endpoint = "inproc://zlink-java-scanned-profile-" + UUID.randomUUID();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(ChannelMessagingTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addHandlerGroup("scanned-profile"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("String")
                .submit(String.class)
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
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addHandlerGroup("annotated-profile"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("AnnotatedEcho")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            runtime.client()
                .sendToChannel("profile", message("changed"))
                .packetName("ProfileChanged")
                .submit()
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
        FANOUT_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions publisherOptions = new DefaultZLinkFrameworkOptions();
        { var channel = publisherOptions.addFanoutChannel("events").enablePublisher(endpoint); };

        DefaultZLinkFrameworkOptions subscriberOptions = new DefaultZLinkFrameworkOptions();
        subscriberOptions.addHandlersFromPackageOf(ChannelMessagingTest.class);
        { var channel = subscriberOptions.addFanoutChannel("events"); channel.enableSubscriber(endpoint);
            channel.addHandlerGroup("annotated-events"); };

        try (ZLinkFrameworkRuntime ignoredPublisher =
                 RuntimeTestSupport.startFramework(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime subscriber =
                 RuntimeTestSupport.startFramework(subscriberOptions, new ZLinkJavaBackendAdapterFactory())) {
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
        { var discovery = serverOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = serverOptions.addClientServerChannel("profile").enableServer(serverEndpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        clientOptions.setDefaultTimeout(Duration.ofMillis(100));
        { var discovery = clientOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = clientOptions.addClientServerChannel("profile"); channel.enableClient(); };

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime ignoredServer =
                 RuntimeTestSupport.startFramework(serverOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions, new ZLinkJavaBackendAdapterFactory())) {
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
        { var discovery = clientOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = clientOptions.addClientServerChannel("profile"); channel.enableClient(); };

        DefaultZLinkFrameworkOptions serverOptions = new DefaultZLinkFrameworkOptions();
        { var discovery = serverOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = serverOptions.addClientServerChannel("profile").enableServer(serverEndpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredServer =
                 RuntimeTestSupport.startFramework(serverOptions, new ZLinkJavaBackendAdapterFactory())) {
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
        { var channel = publisherOptions.addFanoutChannel("events").enablePublisher(endpoint); };

        DefaultZLinkFrameworkOptions subscriberOptions = new DefaultZLinkFrameworkOptions();
        { var channel = subscriberOptions.addFanoutChannel("events"); channel.enableSubscriber(endpoint);
            channel.addPublishHandler(ScoreChangedHandler.class, String.class, "ScoreChanged"); };

        try (ZLinkFrameworkRuntime ignoredPublisher =
                 RuntimeTestSupport.startFramework(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime subscriber =
                 RuntimeTestSupport.startFramework(subscriberOptions, new ZLinkJavaBackendAdapterFactory())) {
            publishUntilDelivered(ignoredPublisher);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "fanout publish was not delivered");
            assertEquals("home:1", FANOUT_MESSAGE.get());
            assertEquals("score", FANOUT_TOPIC.get());
            assertEquals("events", FANOUT_CHANNEL.get());
        } finally {
            FANOUT_LATCH.set(null);
            FANOUT_MESSAGE.set(null);
            FANOUT_TOPIC.set(null);
            FANOUT_CHANNEL.set(null);
        }
    }

    @Test
    void routeMesh_requestByRoutingIdSucceeds() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("source-node");
        RoutingId targetRid = RoutingId.from("target-node");
        ROUTE_REQUEST_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMeshChannel("route"); channel.enableServer(sourceEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(sourceRid); };
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        { var channel = targetOptions.addRouteMeshChannel("route"); channel.enableServer(targetEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(targetRid); };
            channel.enableClient(sourceEndpoint);
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime ignoredSource =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime target =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("route:hello", awaitRouteReply(ignoredSource, targetRid));
            assertEquals("route", ROUTE_REQUEST_CHANNEL.get());
        } finally {
            ROUTE_REQUEST_CHANNEL.set(null);
        }
    }

    @Test
    void routeMesh_scannedHandlerGroupRequestSucceeds() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-scanned-source");
        RoutingId targetRid = RoutingId.from("route-scanned-target");

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMeshChannel("route"); channel.enableServer(sourceEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(sourceRid); };
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        targetOptions.addHandlersFromPackageOf(ChannelMessagingTest.class);
        { var channel = targetOptions.addRouteMeshChannel("route"); channel.enableServer(targetEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(targetRid); };
            channel.enableClient(sourceEndpoint);
            channel.addHandlerGroup("route-shared"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("scanned-route:hello", awaitScannedRouteReply(source, targetRid));
        }
    }

    @Test
    void handlerFiltersDoNotWrapRouteMeshRequestDispatch() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-filter-source");
        RoutingId targetRid = RoutingId.from("route-filter-target");
        FILTER_REQUEST.set(null);
        FILTER_PACKET.set(null);

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMeshChannel("route"); channel.enableServer(sourceEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(sourceRid); };
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        targetOptions.useFilter(ReplyDecoratingFilter.class);
        { var channel = targetOptions.addRouteMeshChannel("route"); channel.enableServer(targetEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(targetRid); };
            channel.enableClient(sourceEndpoint);
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
            ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("route:hello", awaitRouteReply(source, targetRid));
            assertNull(FILTER_REQUEST.get());
            assertNull(FILTER_PACKET.get());
        } finally {
            FILTER_REQUEST.set(null);
            FILTER_PACKET.set(null);
        }
    }

    @Test
    void routeMesh_matchesRepliesByRequestSequenceWhenPacketNameIsShared() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-seq-source");
        RoutingId targetRid = RoutingId.from("route-seq-target");

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMeshChannel("route"); channel.enableServer(sourceEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(sourceRid); };
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        { var channel = targetOptions.addRouteMeshChannel("route"); channel.enableServer(targetEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(targetRid); };
            channel.enableClient(sourceEndpoint);
            channel.addRequestHandler(DelayedRouteEchoHandler.class, String.class, String.class, "SharedPacket"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("warmup", awaitSharedRouteReply(source, targetRid, "warmup:1"));

            CompletionStage<String> slow = source.route()
                .requestTo("route", targetRid, message("slow:40"))
                .packetName("SharedPacket")
                .timeout(Duration.ofSeconds(3))
                .submit(String.class);
            CompletionStage<String> fast = source.route()
                .requestTo("route", targetRid, message("fast:1"))
                .packetName("SharedPacket")
                .timeout(Duration.ofSeconds(3))
                .submit(String.class);

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
        ROUTE_SEND_CHANNEL.set(null);
        ROUTE_SEND_SOURCE.set(null);

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMeshChannel("route"); channel.enableServer(sourceEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(sourceRid); };
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        { var channel = targetOptions.addRouteMeshChannel("route"); channel.enableServer(targetEndpoint);
            { var route = channel.configureRouting(); route.setRoutingId(targetRid); };
            channel.enableClient(sourceEndpoint);
            channel.addSendHandler(RouteNoticeHandler.class, String.class, "Notice"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            routeSendUntilDelivered(source, targetRid);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "route mesh send was not delivered");
            assertEquals("ping", ROUTE_SEND_MESSAGE.get());
            assertEquals("Notice", ROUTE_SEND_PACKET.get());
            assertEquals("route", ROUTE_SEND_CHANNEL.get());
            assertEquals(sourceRid, ROUTE_SEND_SOURCE.get());
        } finally {
            ROUTE_SEND_LATCH.set(null);
            ROUTE_SEND_MESSAGE.set(null);
            ROUTE_SEND_PACKET.set(null);
            ROUTE_SEND_CHANNEL.set(null);
            ROUTE_SEND_SOURCE.set(null);
        }
    }

    private static String awaitDiscoveryReply(ZLinkFrameworkRuntime client) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return client.client()
                    .requestToChannel("profile", message("hello"))
                    .packetName("Echo")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
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
                .publish("events", "score", message("home:1"))
                .packetName("ScoreChanged")
                .submit()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
    }

    private static void sendUntilDelivered(ZLinkFrameworkRuntime runtime) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && SEND_LATCH.get().getCount() > 0) {
            runtime.client()
                .sendToChannel("profile", message("changed"))
                .packetName("ProfileChanged")
                .submit()
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
                    .requestTo("route", targetRid, message("hello"))
                    .packetName("Echo")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("route mesh request did not succeed", lastFailure);
    }

    private static String awaitScannedRouteReply(ZLinkFrameworkRuntime source, RoutingId targetRid) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return source.route()
                    .requestTo("route", targetRid, message("hello"))
                    .packetName("String")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("scanned route mesh request did not succeed", lastFailure);
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
                    .requestTo("route", targetRid, message(message))
                    .packetName("SharedPacket")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
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
                .sendTo("route", targetRid, message("ping"))
                .packetName("Notice")
                .submit()
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

    private static String message(String value) {
        return value;
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
        public String handle(String request, ZLinkRequestContext context) {
            return request;
        }
    }

    public static final class ReplyDecoratingFilter implements ZLinkHandlerFilter {
        @Override
        public <T> CompletionStage<T> invokeAsync(
            ZLinkInvocationContext context,
            ZLinkNext<T> next) {
            FILTER_REQUEST.set((String) context.request().orElse(""));
            FILTER_PACKET.set(context.packetName().orElse(""));
            FILTER_CHANNEL.set(context.channelName().orElse(""));
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
        public String handle(String request, ZLinkRequestContext context) {
            return "scanned:" + request;
        }
    }

    public static final class ProfileChangedHandler implements ZLinkSendHandler<String> {
        @Override
        public void handle(String message, ZLinkSendContext context) {
            SEND_MESSAGE.set(message);
            SEND_PACKET.set(context.packetName().orElse(""));
            SEND_CHANNEL.set(context.channelName().orElse(""));
            SEND_LATCH.get().countDown();
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
        public void handle(String message, ZLinkPublishContext context) {
            FANOUT_MESSAGE.set(message);
            FANOUT_TOPIC.set(context.topic());
            FANOUT_CHANNEL.set(context.channelName().orElse(""));
            FANOUT_LATCH.get().countDown();
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
        public String handle(String request, ZLinkRouteRequestContext context) {
            ROUTE_REQUEST_CHANNEL.set(context.channelName().orElse(""));
            return "route:" + request;
        }
    }

    @ZLinkHandlerGroup("route-shared")
    public static final class ScannedRouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRouteRequestContext context) {
            return "scanned-route:" + request;
        }
    }

    public static final class DelayedRouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRouteRequestContext context) {
            String[] parts = request.split(":", 2);
            String value = parts[0];
            long delayMillis = parts.length == 2 ? Long.parseLong(parts[1]) : 0;
            return CompletableFuture.supplyAsync(
                () -> value,
                CompletableFuture.delayedExecutor(delayMillis, TimeUnit.MILLISECONDS))
                .join();
        }
    }

    public static final class RouteNoticeHandler implements ZLinkRouteSendHandler<String> {
        @Override
        public void handle(String message, ZLinkRouteSendContext context) {
            ROUTE_SEND_MESSAGE.set(message);
            ROUTE_SEND_PACKET.set(context.packetName().orElse(""));
            ROUTE_SEND_CHANNEL.set(context.channelName().orElse(""));
            ROUTE_SEND_SOURCE.set(context.routingId());
            ROUTE_SEND_LATCH.get().countDown();
                    }
    }
}
