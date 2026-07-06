package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.runtime.backend.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkClientServerChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteSendContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSocketRuntimeOptions;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkDispatchFailure;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkSpotAddress;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.handlers.ZLinkFilterPipeline;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;

public final class ZLinkChannelRuntime
    implements ZLinkClient, ZLinkFanoutClient, ZLinkRouteClient, ZLinkChannelRuntimeOptions,
        AutoCloseable {
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final String FRAMEWORK_ERROR_REPLY_MARKER = "ZLinkFrameworkError";
    private static final String SPOT_ROUTE_BRIDGE_SEND_PACKET_NAME =
        "__zlink.routed_spot.egress.send";
    private static final String SPOT_ROUTE_BRIDGE_REQUEST_PACKET_NAME =
        "__zlink.routed_spot.egress.request";
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_ENETUNREACH = 101;
    private static final int ERRNO_ENOTCONN = 107;
    private static final int ERRNO_ECONNREFUSED = 111;
    private static final int ERRNO_EHOSTUNREACH = 113;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int ERRNO_ENETUNREACH_WIN = 10051;
    private static final int ERRNO_ENOTCONN_WIN = 10057;
    private static final int ERRNO_ECONNREFUSED_WIN = 10061;
    private static final int ERRNO_EHOSTUNREACH_WIN = 10065;
    private static final boolean STREAM_TRACE =
        "1".equals(System.getenv("ZLINK_JAVA_STREAM_TRACE"));

    private final ZLinkBackendContext context;
    private final boolean ownsContext;
    private final Map<String, ZLinkBackendDealerSocket> clients = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> servers = new HashMap<>();
    private final Map<String, ZLinkBackendSpotRouteBridge> spotRouteBridges = new HashMap<>();
    private final Map<String, ZLinkBackendSpotNode> spotRouterNodes = new HashMap<>();
    private final Set<String> spotRouteBridgeDrainScheduled = ConcurrentHashMap.newKeySet();
    private final Map<String, ZLinkBackendPublisherSocket> publishers = new HashMap<>();
    private final Map<String, ZLinkBackendSubscriberSocket> subscribers = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> routeRouters = new HashMap<>();
    private final Map<String, Object> routeSocketLocks = new HashMap<>();
    private final Map<String, ChannelRegistration> registrationsByName = new HashMap<>();
    private final List<ZLinkBackendDealerSocket> manualClients = new ArrayList<>();
    private final List<ZLinkBackendRouterSocket> manualServers = new ArrayList<>();
    private final List<ZLinkBackendPublisherSocket> manualPublishers = new ArrayList<>();
    private final List<ZLinkBackendSubscriberSocket> manualSubscribers = new ArrayList<>();
    private final List<ZLinkBackendRouterSocket> manualRouteRouters = new ArrayList<>();
    private final Map<String, Map<String, ChannelRequestHandlerRegistration>> requestHandlers =
        new HashMap<>();
    private final Map<String, Map<String, ChannelSendHandlerRegistration>> sendHandlers =
        new HashMap<>();
    private final Map<String, Map<String, ChannelPublishHandlerRegistration>> publishHandlers =
        new HashMap<>();
    private final Map<String, Map<String, ChannelRouteRequestHandlerRegistration>> routeRequestHandlers =
        new HashMap<>();
    private final Map<String, Map<String, ChannelRouteSendHandlerRegistration>> routeSendHandlers =
        new HashMap<>();
    private final Map<String, RouteInternalRequestHandler> routeInternalRequestHandlers = new HashMap<>();
    private record PendingRawSpotRouteBridgeReply(
        RoutingId expectedPeerRid,
        List<byte[]> requestParts,
        CompletableFuture<List<Message>> reply) {
    }

    private final Map<String, ArrayDeque<PendingRawSpotRouteBridgeReply>> pendingRawSpotRouteBridgeReplies =
        new HashMap<>();
    private final Map<String, Map<RoutingId, Long>> recentlyCompletedRawSpotRouteBridgeReplies =
        new HashMap<>();
    private final Map<String, ZLinkAsyncSerialQueue> sendDispatchQueues = new HashMap<>();
    private final Map<String, ZLinkAsyncSerialQueue> requestDispatchQueues = new HashMap<>();
    private final Map<String, ZLinkAsyncSerialQueue> publishDispatchQueues = new HashMap<>();
    private final Map<String, ZLinkAsyncSerialQueue> routeRequestDispatchQueues = new HashMap<>();
    private final Map<String, ZLinkAsyncSerialQueue> routeSendDispatchQueues = new HashMap<>();
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final Executor handlerExecutor;
    private final List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers;
    private final List<Class<? extends ZLinkHandlerFilter>> filterTypes;
    private final Duration defaultRequestTimeout;
    private final ZLinkBackendAdapterFactory backendFactory;
    private final ZLinkBackendAdapterOptions adapterOptions;
    private final ZLinkDispatchErrorReporter dispatchErrors;
    private Supplier<ZLinkBackendSpotNode> spotRouteBridgeOwner;
    private Runnable spotRouteBridgeDispatchDrainer;
    private final ExecutorService receiveExecutor = Executors.newCachedThreadPool(task -> {
        Thread thread = new Thread(task, "zlink-java-channel-runtime");
        thread.setDaemon(true);
        return thread;
    });
    private final ExecutorService spotRouteBridgeExecutor = Executors.newSingleThreadExecutor(task -> {
        Thread thread = new Thread(task, "zlink-java-spot-route-bridge");
        thread.setDaemon(true);
        return thread;
    });
    private final ScheduledExecutorService timeoutExecutor = Executors.newSingleThreadScheduledExecutor(task -> {
        Thread thread = new Thread(task, "zlink-java-channel-timeout");
        thread.setDaemon(true);
        return thread;
    });
    private final java.util.Set<CompletableFuture<?>> pendingRequests = ConcurrentHashMap.newKeySet();
    private volatile boolean running = true;

    public record AutoConnectSurface(
        ZLinkLocationAutoConnectType type,
        String meshName,
        ZLinkLocationRole role,
        RoutingId nodeRid,
        String endpoint,
        int weight,
        ZLinkBackendConnectableSocket socket,
        List<String> manualEndpoints) {
    }

    @Override
    public ZLinkClientServerChannelRuntimeOptions clientServerChannel(String channelName) {
        ChannelRegistration registration = requireChannel(channelName, ChannelKind.CLIENT_SERVER);
        return new ClientServerRuntimeOptions(registration.name());
    }

    private ChannelRegistration requireChannel(String channelName, ChannelKind kind) {
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException("channel name is required");
        }
        ChannelRegistration registration = registrationsByName.get(channelName);
        if (registration == null) {
            throw new ZLinkConfigurationException("channel is not configured: " + channelName);
        }
        if (registration.kind() != kind) {
            throw new ZLinkConfigurationException(
                "channel has incompatible kind: " + channelName);
        }
        return registration;
    }

    private static String requireRouterChannelId(String routerChannelId) {
        if (routerChannelId == null || routerChannelId.isBlank()) {
            throw new ZLinkConfigurationException("router channel id is required");
        }
        return routerChannelId;
    }

    private ZLinkBackendRouterSocket requireServerSocket(String channelName) {
        ZLinkBackendRouterSocket socket = servers.get(channelName);
        if (socket == null) {
            throw new ZLinkConfigurationException(
                "client/server channel has no server socket: " + channelName);
        }
        return socket;
    }

    private static void validatePeerWeight(int value) {
        if (value < 0 || value > 100) {
            throw new ZLinkConfigurationException("Weight must be between 0 and 100.");
        }
    }

    private final class ClientServerRuntimeOptions
        implements ZLinkClientServerChannelRuntimeOptions {
        private final String channelName;

        ClientServerRuntimeOptions(String channelName) {
            this.channelName = channelName;
        }

        @Override
        public ZLinkSocketRuntimeOptions configureServerSocket() {
            return new ServerSocketRuntimeOptions(channelName);
        }
    }

    private final class ServerSocketRuntimeOptions implements ZLinkSocketRuntimeOptions {
        private final String channelName;

        ServerSocketRuntimeOptions(String channelName) {
            this.channelName = channelName;
        }

        @Override
        public int weight() {
            return requireServerSocket(channelName).peerWeight();
        }

        @Override
        public void weight(int value) {
            validatePeerWeight(value);
            requireServerSocket(channelName).setPeerWeight(value);
        }
    }

    public ZLinkChannelRuntime(
        ZLinkChannelBackendAdapter backend,
        ZLinkFrameworkRegistration registration,
        ZLinkMessageSerializer serializer) {
        this(backend, registration, serializer, ZLinkHandlerFactory.reflection());
    }

    public ZLinkChannelRuntime(
        ZLinkChannelBackendAdapter backend,
        ZLinkFrameworkRegistration registration,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        this(backend, null, null, registration, serializer, handlerFactory);
    }

    public ZLinkChannelRuntime(
        ZLinkChannelBackendAdapter backend,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        this(
            backend,
            backend.createContext(),
            true,
            backendFactory,
            adapterOptions,
            registration,
            serializer,
            handlerFactory);
    }

    public ZLinkChannelRuntime(
        ZLinkChannelBackendAdapter backend,
        ZLinkBackendContext context,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        this(
            backend,
            context,
            false,
            backendFactory,
            adapterOptions,
            registration,
            serializer,
            handlerFactory);
    }

    private ZLinkChannelRuntime(
        ZLinkChannelBackendAdapter backend,
        ZLinkBackendContext context,
        boolean ownsContext,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        this.serializer = Objects.requireNonNull(serializer, "serializer");
        this.handlerFactory = Objects.requireNonNull(handlerFactory, "handlerFactory");
        this.handlerExecutor = Objects.requireNonNull(registration.handlerExecutor(), "handlerExecutor");
        this.suspendHandlerInvokers = registration.suspendHandlerInvokers();
        this.filterTypes = List.copyOf(registration.filters());
        this.defaultRequestTimeout = registration.defaultRequestTimeout();
        this.backendFactory = backendFactory;
        this.adapterOptions = adapterOptions;
        this.dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.dispatchOptions(),
            handlerFactory,
            this.handlerExecutor);
        this.context = Objects.requireNonNull(context, "context");
        this.ownsContext = ownsContext;
        ZLinkScannedHandlerCatalog handlerCatalog =
            ZLinkHandlerScanner.scan(registration.handlerPackageMarkers());
        for (ChannelRegistration channel : registration.channels()) {
            registrationsByName.put(channel.name(), channel);
            configureClientServerChannel(backend, channel, handlerCatalog);
            configureFanoutChannel(backend, channel, handlerCatalog);
            configureRouteMeshChannel(backend, channel, handlerCatalog);
        }
    }

    private void configureClientServerChannel(
        ZLinkChannelBackendAdapter backend,
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        if (channel.kind() != ChannelKind.CLIENT_SERVER) {
            return;
        }
        if (channel.clientEnabled()) {
            ZLinkBackendDealerSocket dealer = backend.createDealerSocket(context);
            dealer.setChannelName(channel.name());
            for (String endpoint : channel.clientManualEndpoints()) {
                dealer.connect(endpoint);
            }
            manualClients.add(dealer);
            clients.put(channel.name(), dealer);
        }
        if (channel.serverBinds().isEmpty()) {
            return;
        }
        ZLinkBackendRouterSocket router = backend.createRouterSocket(context);
        router.setChannelName(channel.name());
        if (channel.routingId() != null) {
            router.setRoutingId(channel.routingId());
        }
        manualServers.add(router);
        for (String endpoint : channel.serverBinds()) {
            router.bind(endpoint);
        }
        servers.put(channel.name(), router);
        sendHandlers.put(channel.name(), sendHandlersByPacket(channel, handlerCatalog));
        requestHandlers.put(channel.name(), handlersByPacket(channel, handlerCatalog));
        sendDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
        requestDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
        startRequestLoop(channel.name(), router);
    }

    private void configureFanoutChannel(
        ZLinkChannelBackendAdapter backend,
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        if (channel.kind() != ChannelKind.FANOUT) {
            return;
        }
        if (channel.publisherEnabled()) {
            ZLinkBackendPublisherSocket publisher = backend.createPublisherSocket(context);
            publisher.setChannelName(channel.name());
            if (channel.routingId() != null) {
                publisher.setRoutingId(channel.routingId());
            }
            manualPublishers.add(publisher);
            for (String endpoint : channel.publisherBinds()) {
                publisher.bind(endpoint);
            }
            publishers.put(channel.name(), publisher);
        }
        if (!channel.subscriberEnabled()) {
            return;
        }
        ZLinkBackendSubscriberSocket subscriber = backend.createSubscriberSocket(context);
        subscriber.setChannelName(channel.name());
        for (String endpoint : channel.subscriberManualEndpoints()) {
            subscriber.connect(endpoint);
        }
        manualSubscribers.add(subscriber);
        subscriber.setSubscription("");
        subscribers.put(channel.name(), subscriber);
        publishHandlers.put(channel.name(), publishHandlersByPacket(channel, handlerCatalog));
        publishDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
        startSubscribeLoop(channel.name(), subscriber);
    }

    private void configureRouteMeshChannel(
        ZLinkChannelBackendAdapter backend,
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        if (channel.kind() != ChannelKind.ROUTE_MESH) {
            return;
        }
        boot("routeMesh init channel=" + channel.name());
        ZLinkBackendRouterSocket router = backend.createRouterSocket(context);
        router.setChannelName(channel.name());
        if (channel.routeRoutingId() != null) {
            boot("routeMesh setRoutingId channel=" + channel.name());
            router.setRoutingId(channel.routeRoutingId());
            boot("routeMesh setRoutingId done channel=" + channel.name());
        }
        for (String endpoint : channel.routeManualEndpoints()) {
            boot("routeMesh manualConnect channel=" + channel.name() + " endpoint=" + endpoint);
            router.connect(endpoint);
            boot("routeMesh manualConnect done channel=" + channel.name() + " endpoint=" + endpoint);
        }
        manualRouteRouters.add(router);
        for (String endpoint : channel.routeBinds()) {
            boot("routeMesh bind channel=" + channel.name() + " endpoint=" + endpoint);
            router.bind(endpoint);
            boot("routeMesh bind done channel=" + channel.name() + " endpoint=" + endpoint);
        }
        routeRouters.put(channel.name(), router);
        routeSocketLocks.put(channel.name(), new Object());
        routeSendHandlers.put(channel.name(), routeSendHandlersByPacket(channel, handlerCatalog));
        routeRequestHandlers.put(channel.name(), routeHandlersByPacket(channel, handlerCatalog));
        routeSendDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
        routeRequestDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
        boot("routeMesh startLoop channel=" + channel.name());
        startRouteLoop(channel.name(), router);
        boot("routeMesh init done channel=" + channel.name());
    }

    public List<AutoConnectSurface> autoConnectSurfaces() {
        List<AutoConnectSurface> surfaces = new ArrayList<>();
        for (ChannelRegistration channel : registrationsByName.values()) {
            if (channel.kind() == ChannelKind.CLIENT_SERVER) {
                ZLinkBackendRouterSocket server = servers.get(channel.name());
                if (server != null) {
                    for (String endpoint : channel.serverBinds()) {
                        surfaces.add(new AutoConnectSurface(
                            ZLinkLocationAutoConnectType.CLIENT_SERVER,
                            channel.name(),
                            ZLinkLocationRole.ROUTER,
                            channel.routingId(),
                            endpoint,
                            server.peerWeight(),
                            null,
                            List.of()));
                    }
                }
                ZLinkBackendDealerSocket client = clients.get(channel.name());
                if (client != null) {
                    surfaces.add(new AutoConnectSurface(
                        ZLinkLocationAutoConnectType.CLIENT_SERVER,
                        channel.name(),
                        ZLinkLocationRole.DEALER,
                        channel.routingId(),
                        "",
                        100,
                        client,
                        channel.clientManualEndpoints()));
                }
                continue;
            }

            if (channel.kind() == ChannelKind.FANOUT) {
                if (publishers.containsKey(channel.name())) {
                    for (String endpoint : channel.publisherBinds()) {
                        surfaces.add(new AutoConnectSurface(
                            ZLinkLocationAutoConnectType.FANOUT,
                            channel.name(),
                            ZLinkLocationRole.PUB,
                            channel.routingId(),
                            endpoint,
                            100,
                            null,
                            List.of()));
                    }
                }
                ZLinkBackendSubscriberSocket subscriber = subscribers.get(channel.name());
                if (subscriber != null) {
                    surfaces.add(new AutoConnectSurface(
                        ZLinkLocationAutoConnectType.FANOUT,
                        channel.name(),
                        ZLinkLocationRole.SUB,
                        channel.routingId(),
                        "",
                        100,
                        subscriber,
                        channel.subscriberManualEndpoints()));
                }
                continue;
            }

            if (channel.kind() == ChannelKind.ROUTE_MESH) {
                ZLinkBackendRouterSocket router = routeRouters.get(channel.name());
                if (router == null) {
                    continue;
                }
                for (String endpoint : channel.routeBinds()) {
                    surfaces.add(new AutoConnectSurface(
                        ZLinkLocationAutoConnectType.ROUTE_MESH,
                        channel.name(),
                        ZLinkLocationRole.ROUTER,
                        channel.routeRoutingId(),
                        endpoint,
                        router.peerWeight(),
                        router,
                        channel.routeManualEndpoints()));
                }
                if (channel.routeBinds().isEmpty()) {
                    surfaces.add(new AutoConnectSurface(
                        ZLinkLocationAutoConnectType.ROUTE_MESH,
                        channel.name(),
                        ZLinkLocationRole.ROUTER,
                        channel.routeRoutingId(),
                        "",
                        router.peerWeight(),
                        router,
                        channel.routeManualEndpoints()));
                }
            }
        }
        return List.copyOf(surfaces);
    }

    @Override
    public ZLinkSendCall sendToChannel(String channelName, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new SendCall(
            requireClient(channelName),
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkYieldRequestCall requestToChannel(String channelName, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new RequestCall(
            requireClient(channelName),
            encoded.payload(),
            Optional.of(encoded.packetName()),
            defaultRequestTimeout(channelName));
    }

    @Override
    public ZLinkPublishCall publish(String channelName, String topic, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new PublishCall(
            requirePublisher(channelName),
            topic,
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkSendCall sendToNode(String channelName, RoutingId target, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new RouteSendCall(
            requireRouteRouter(channelName),
            target,
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkSendCall sendToSpot(
        String channelName,
        ZLinkSpotAddress address,
        Object message) {
        Objects.requireNonNull(address, "address");
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new RouteSpotSendCall(
            channelName,
            address.nodeRid(),
            address.spotRid(),
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkRequestCall requestToNode(String channelName, RoutingId target, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new RouteRequestCall(
            channelName,
            requireRouteRouter(channelName),
            target,
            encoded.payload(),
            Optional.of(encoded.packetName()),
            defaultRequestTimeout(channelName));
    }

    @Override
    public ZLinkRequestCall requestToSpot(
        String channelName,
        ZLinkSpotAddress address,
        Object message) {
        Objects.requireNonNull(address, "address");
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new RouteSpotRequestCall(
            channelName,
            address.nodeRid(),
            address.spotRid(),
            encoded.payload(),
            Optional.of(encoded.packetName()),
            defaultRequestTimeout(channelName));
    }

    public void registerSpotRouteBridgeOwner(
        Supplier<ZLinkBackendSpotNode> owner) {
        this.spotRouteBridgeOwner = Objects.requireNonNull(owner, "owner");
    }

    public void registerSpotRouteBridgeDispatchDrainer(Runnable drainer) {
        this.spotRouteBridgeDispatchDrainer = Objects.requireNonNull(drainer, "drainer");
    }

    public boolean attachSpotRouteBridgeToServer(
        String channelName,
        ZLinkBackendSpotNode node) {
        ZLinkBackendRouterSocket router = servers.get(channelName);
        if (router == null) {
            router = routeRouters.get(channelName);
        }
        if (router == null) {
            return false;
        }
        ZLinkBackendSpotRouteBridge bridge = node.createRouteBridge();
        bridge.attachRouterChannel(
            channelName,
            router);
        spotRouteBridges.put(channelName, bridge);
        return true;
    }

    public void registerSpotRouterNode(
        String routerChannelId,
        ZLinkBackendSpotNode node) {
        String channelId = requireRouterChannelId(routerChannelId);
        spotRouterNodes.put(channelId, Objects.requireNonNull(node, "node"));
    }

    private Duration defaultRequestTimeout(String channelName) {
        ChannelRegistration registration = registrationsByName.get(channelName);
        if (registration != null && registration.defaultRequestTimeout() != null) {
            return registration.defaultRequestTimeout();
        }
        return defaultRequestTimeout;
    }

    public Map<String, ZLinkBackendSocket> monitoringSocketSources() {
        Map<String, ZLinkBackendSocket> sources = new HashMap<>();
        sources.putAll(clients);
        sources.putAll(servers);
        sources.putAll(publishers);
        sources.putAll(subscribers);
        sources.putAll(routeRouters);
        return Map.copyOf(sources);
    }

    public void registerRouteInternalRequestHandler(
        String packetName,
        RouteInternalRequestHandler handler) {
        if (packetName == null || packetName.isBlank()) {
            throw new ZLinkConfigurationException("internal route packet name is required");
        }
        if (routeInternalRequestHandlers.putIfAbsent(packetName, Objects.requireNonNull(handler, "handler"))
            != null) {
            throw new ZLinkConfigurationException(
                "duplicate internal route packet handler: " + packetName);
        }
    }

    public CompletionStage<Void> sendToSpotViaRouterChannel(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts) {
        ChannelRegistration registration = registrationsByName.get(routerChannelId);
        if (registration == null || registration.kind() != ChannelKind.ROUTE_MESH) {
            ZLinkBackendSpotNode spotRouterNode = spotRouterNodes.get(routerChannelId);
            if (spotRouterNode != null) {
                return sendToSpotViaSpotRouterNode(
                    routerChannelId,
                    spotRouterNode,
                    targetNodeRid,
                    targetSpotRid,
                    spotParts);
            }
            throw new ZLinkConfigurationException(
                "route mesh channel is not configured: " + routerChannelId);
        }
        try {
            ZLinkBackendSpotRouteBridge bridge = requireSpotRouteBridge(routerChannelId);
            List<byte[]> bridgePayloads = spotParts.stream()
                .map(Message::toByteArray)
                .toList();
            CompletableFuture<Void> result = new CompletableFuture<>();
            submitSpotRouteBridgeSendWithRetry(
                bridge,
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                bridgePayloads,
                defaultRequestTimeout(routerChannelId),
                result);
            return result;
        } catch (RuntimeException ex) {
            CompletableFuture<Void> result = new CompletableFuture<>();
            result.completeExceptionally(ex);
            return result;
        }
    }

    private void submitSpotRouteBridgeSendWithRetry(
        ZLinkBackendSpotRouteBridge bridge,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<byte[]> bridgePayloads,
        Duration timeout,
        CompletableFuture<Void> result) {
        long timeoutNanos = timeout == null || timeout.isZero()
            ? defaultRequestTimeout.toNanos()
            : timeout.toNanos();
        long deadline = System.nanoTime() + timeoutNanos;
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                List<Message> attemptParts = bridgePayloads.stream()
                    .map(Message::from)
                    .toList();
                try {
                    boolean submitted = bridge.send(
                        routerChannelId,
                        targetNodeRid,
                        targetSpotRid,
                        attemptParts,
                        SendFlags.DONT_WAIT);
                    if (submitted) {
                        result.complete(null);
                        return;
                    }
                    if (System.nanoTime() >= deadline) {
                        result.completeExceptionally(new TimeoutException(
                            "routed SPOT route mesh send was not ready before timeout"));
                        return;
                    }
                    timeoutExecutor.schedule(this, 10, TimeUnit.MILLISECONDS);
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                } finally {
                    attemptParts.forEach(Message::close);
                }
            }
        }
        new Attempt().run();
    }

    public CompletionStage<List<Message>> requestToSpotViaRouterChannel(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts,
        Duration timeout) {
        trace("spot-route request-start router=" + routerChannelId
            + " targetNode=" + targetNodeRid
            + " targetSpot=" + targetSpotRid
            + " parts=" + describeTraceParts(spotParts));
        ChannelRegistration registration = registrationsByName.get(routerChannelId);
        if (registration == null || registration.kind() != ChannelKind.ROUTE_MESH) {
            ZLinkBackendSpotNode spotRouterNode = spotRouterNodes.get(routerChannelId);
            if (spotRouterNode != null) {
                trace("spot-route request-path=spot-router-node router=" + routerChannelId
                    + " targetNode=" + targetNodeRid
                    + " targetSpot=" + targetSpotRid);
                return requestToSpotViaSpotRouterNode(
                    routerChannelId,
                    spotRouterNode,
                    targetNodeRid,
                    targetSpotRid,
                    spotParts,
                    timeout);
            }
            trace("spot-route request-missing-router router=" + routerChannelId
                + " targetNode=" + targetNodeRid
                + " targetSpot=" + targetSpotRid);
            throw new ZLinkConfigurationException(
                "route mesh channel is not configured: " + routerChannelId);
        }
        CompletableFuture<List<Message>> result = new CompletableFuture<>();
        trackPendingRequest(result, timeout);
        try {
            ZLinkBackendSpotRouteBridge bridge = requireSpotRouteBridge(routerChannelId);
            trace("spot-route request-path=route-bridge router=" + routerChannelId
                + " targetNode=" + targetNodeRid
                + " targetSpot=" + targetSpotRid);
            List<Message> bridgeParts = copyMessages(spotParts);
            PendingRawSpotRouteBridgeReply rawReply = new PendingRawSpotRouteBridgeReply(
                targetNodeRid,
                bridgeParts.stream().map(Message::toByteArray).toList(),
                result);
            enqueueRawSpotRouteBridgeReply(routerChannelId, rawReply);
            try {
                bridge.requestAsync(
                        routerChannelId,
                        targetNodeRid,
                        targetSpotRid,
                        bridgeParts,
                        SendFlags.NONE,
                        timeout)
                    .whenComplete((reply, error) -> {
                        if (error != null) {
                            trace("spot-route bridge-reply-error router=" + routerChannelId
                                + " targetNode=" + targetNodeRid
                                + " targetSpot=" + targetSpotRid
                                + " error=" + error);
                            result.completeExceptionally(error);
                            return;
                        }
                        try {
                            trace("spot-route bridge-reply router=" + routerChannelId
                                + " targetNode=" + targetNodeRid
                                + " targetSpot=" + targetSpotRid
                                + " parts=" + describeTraceParts(reply));
                            if (sameMessageParts(rawReply.requestParts(), reply)
                                || sameFirstMessagePart(rawReply.requestParts(), reply)) {
                                return;
                            }
                            if (isFrameworkErrorReply(reply)) {
                                result.completeExceptionally(new ZLinkFrameworkException(
                                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                    frameworkErrorReplyMessage(reply)));
                                return;
                            }
                            List<Message> normalizedReply =
                                copySpotRouteBridgeReplyMessages(reply);
                            if (sameFirstMessagePart(rawReply.requestParts(), normalizedReply)) {
                                normalizedReply.forEach(Message::close);
                                return;
                            }
                            if (!result.complete(normalizedReply)) {
                                normalizedReply.forEach(Message::close);
                            }
                        } finally {
                            reply.forEach(Message::close);
                        }
                    });
            } finally {
                bridgeParts.forEach(Message::close);
            }
            return result;
        } catch (RuntimeException ex) {
            trace("spot-route request-exception router=" + routerChannelId
                + " targetNode=" + targetNodeRid
                + " targetSpot=" + targetSpotRid
                + " error=" + ex);
            removeRawSpotRouteBridgeReply(routerChannelId, result);
            result.completeExceptionally(ex);
            return result;
        }
    }

    private CompletionStage<Void> sendToSpotViaSpotRouterNode(
        String routerChannelId,
        ZLinkBackendSpotNode node,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        List<Message> requestParts = copyMessages(spotParts);
        try {
            boolean submitted = node.entrySpot().sendToSpot(
                targetNodeRid,
                targetSpotRid,
                requestParts,
                SendFlags.NONE);
            trace("spot-route node-send-submit router=" + routerChannelId
                + " targetNode=" + targetNodeRid
                + " targetSpot=" + targetSpotRid
                + " submitted=" + submitted);
            if (submitted) {
                result.complete(null);
            } else {
                result.completeExceptionally(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                    "Spot node router '" + routerChannelId + "' is not ready for SPOT send."));
            }
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        } finally {
            requestParts.forEach(Message::close);
        }
        return result;
    }

    private CompletionStage<List<Message>> requestToSpotViaSpotRouterNode(
        String routerChannelId,
        ZLinkBackendSpotNode node,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts,
        Duration timeout) {
        CompletableFuture<List<Message>> result = new CompletableFuture<>();
        trackPendingRequest(result, timeout);
        List<Message> requestParts = copyMessages(spotParts);
        long requestStartedNanos = System.nanoTime();
        try {
            boolean submitted = node.entrySpot().requestToSpot(
                targetNodeRid,
                targetSpotRid,
                requestParts,
                reply -> {
                    try {
                        trace("spot-route node-reply router=" + routerChannelId
                            + " targetNode=" + targetNodeRid
                            + " targetSpot=" + targetSpotRid
                            + " elapsedMs=" + elapsedMillis(requestStartedNanos)
                            + " result=" + reply.result()
                            + " origin=spot-node-callback"
                            + " sourceRid=" + reply.routingId().map(Object::toString).orElse(null)
                            + " sourceSpot=" + reply.spotRid().map(Object::toString).orElse(null)
                            + " requestSeq=" + reply.requestSeq().map(Object::toString).orElse(null)
                            + " parts=" + describeTraceParts(reply.parts()));
                        if (reply.result() != ZLinkBackendRequestResult.OK) {
                            result.completeExceptionally(new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                "SPOT route request failed through router '" + routerChannelId
                                    + "': " + reply.result()));
                            return;
                        }
                        List<Message> replyParts = copyMessages(reply.parts());
                        if (isFrameworkErrorReply(replyParts)) {
                            replyParts.forEach(Message::close);
                            result.completeExceptionally(new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                frameworkErrorReplyMessage(reply.parts())));
                            return;
                        }
                        if (!result.complete(replyParts)) {
                            replyParts.forEach(Message::close);
                        }
                    } finally {
                        reply.close();
                    }
                },
                SendFlags.NONE,
                timeout);
            trace("spot-route node-submit router=" + routerChannelId
                + " targetNode=" + targetNodeRid
                + " targetSpot=" + targetSpotRid
                + " submitted=" + submitted);
            if (!submitted) {
                result.completeExceptionally(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                    "Spot node router '" + routerChannelId + "' is not ready for SPOT request."));
            }
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        } finally {
            requestParts.forEach(Message::close);
        }
        return result;
    }

    private static void trace(String message) {
        if (STREAM_TRACE) {
            System.out.println("[zlink-java-stream-trace] " + message);
        }
    }

    private static String describeTraceParts(List<Message> parts) {
        List<String> descriptions = new ArrayList<>();
        for (int i = 0; i < parts.size(); i++) {
            byte[] bytes = parts.get(i).toByteArray();
            descriptions.add(i + ":" + bytes.length + ":" + traceText(bytes));
        }
        return descriptions.toString();
    }

    private static String traceText(byte[] bytes) {
        if (bytes.length == 0 || bytes.length > 512) {
            return "";
        }
        String text = new String(bytes, StandardCharsets.UTF_8);
        for (int i = 0; i < text.length(); i++) {
            char ch = text.charAt(i);
            if (Character.isISOControl(ch) && !Character.isWhitespace(ch)) {
                return "";
            }
        }
        return text;
    }

    private static long elapsedMillis(long startedNanos) {
        return TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startedNanos);
    }

    private void submitRouteRequestWithRetry(
        ZLinkBackendRouterSocket router,
        RoutingId targetPeerRid,
        List<Message> requestParts,
        ZLinkBackendRequestCallback callback,
        Duration timeout,
        List<String> reconnectEndpoints,
        CompletableFuture<?> result) {
        List<byte[]> requestPayloads = requestParts.stream()
            .map(Message::toByteArray)
            .toList();
        long timeoutNanos = timeout == null || timeout.isZero()
            ? defaultRequestTimeout.toNanos()
            : timeout.toNanos();
        long deadline = System.nanoTime() + timeoutNanos;
        class Attempt implements Runnable {
            private boolean reconnected;

            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                List<Message> attemptParts = requestPayloads.stream()
                    .map(Message::from)
                    .toList();
                try {
                    boolean submitted = router.request(
                        targetPeerRid,
                        attemptParts,
                        callback,
                        SendFlags.DONT_WAIT,
                        timeout);
                    if (submitted) {
                        return;
                    }
                    if (!reconnected) {
                        reconnected = true;
                        for (String endpoint : reconnectEndpoints) {
                            router.connect(endpoint);
                        }
                    }
                    if (System.nanoTime() >= deadline) {
                        result.completeExceptionally(new TimeoutException(
                            "routed SPOT route mesh request was not ready before timeout"));
                        return;
                    }
                    timeoutExecutor.schedule(this, 10, TimeUnit.MILLISECONDS);
                } catch (RuntimeException ex) {
                    if (isRetriableSubmit(ex) && System.nanoTime() < deadline) {
                        timeoutExecutor.schedule(this, 10, TimeUnit.MILLISECONDS);
                        return;
                    }
                    result.completeExceptionally(ex);
                } finally {
                    attemptParts.forEach(Message::close);
                }
            }
        }
        new Attempt().run();
    }

    @Override
    public void close() {
        beginClose();
        receiveExecutor.shutdown();
        spotRouteBridgeExecutor.shutdown();
        timeoutExecutor.shutdownNow();
        awaitTerminated(receiveExecutor);
        awaitTerminated(spotRouteBridgeExecutor);
        awaitTerminated(timeoutExecutor);
        closeSpotRouteBridges();
        java.util.Set<ZLinkBackendObject> closed =
            java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>());
        closeAll(clients.values(), closed);
        closeAll(servers.values(), closed);
        closeAll(publishers.values(), closed);
        closeAll(subscribers.values(), closed);
        closeAll(routeRouters.values(), closed);
        closeAll(manualClients, closed);
        closeAll(manualServers, closed);
        closeAll(manualPublishers, closed);
        closeAll(manualSubscribers, closed);
        closeAll(manualRouteRouters, closed);
        if (ownsContext) {
            context.close();
        }
    }

    public void closeSpotRouteBridges() {
        closeAll(spotRouteBridges.values());
        spotRouteBridges.clear();
    }

    public void beginClose() {
        running = false;
        for (CompletableFuture<?> pending : pendingRequests) {
            pending.completeExceptionally(new ZLinkConfigurationException("channel runtime is closed"));
        }
    }

    private static void closeAll(Iterable<? extends ZLinkBackendObject> closeables) {
        closeAll(closeables, java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>()));
    }

    private static void closeAll(
        Iterable<? extends ZLinkBackendObject> closeables,
        java.util.Set<ZLinkBackendObject> closed) {
        for (ZLinkBackendObject closeable : closeables) {
            if (!closed.add(closeable)) {
                continue;
            }
            try {
                closeable.close();
            } catch (ZlinkCloseException ignored) {
            }
        }
    }

    private static void awaitTerminated(java.util.concurrent.ExecutorService executor) {
        try {
            executor.awaitTermination(1, TimeUnit.SECONDS);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
    }

    private ZLinkBackendDealerSocket requireClient(String channelName) {
        ZLinkBackendDealerSocket client = clients.get(channelName);
        if (client == null) {
            throw new ZLinkConfigurationException("channel client is not configured: " + channelName);
        }
        return client;
    }

    private ZLinkBackendSpotRouteBridge requireSpotRouteBridge(String channelName) {
        ZLinkBackendSpotRouteBridge existing = spotRouteBridges.get(channelName);
        if (existing != null) {
            return existing;
        }
        if (spotRouteBridgeOwner == null) {
            throw new ZLinkConfigurationException(
                "routed SPOT egress requires a router-capable SPOT node");
        }
        ZLinkBackendSpotRouteBridge bridge =
            spotRouteBridgeOwner.get().createRouteBridge();
        ChannelRegistration registration = registrationsByName.get(channelName);
        if (registration != null && registration.kind() == ChannelKind.ROUTE_MESH) {
            bridge.attachRouterChannel(
                channelName,
                requireRouteRouter(channelName));
        } else {
            throw new ZLinkConfigurationException(
                "SPOT route bridge requires a router channel: " + channelName);
        }
        spotRouteBridges.put(channelName, bridge);
        return bridge;
    }

    private ZLinkBackendPublisherSocket requirePublisher(String channelName) {
        ZLinkBackendPublisherSocket publisher = publishers.get(channelName);
        if (publisher == null) {
            throw new ZLinkConfigurationException("fanout publisher is not configured: " + channelName);
        }
        return publisher;
    }

    private ZLinkBackendRouterSocket requireRouteRouter(String channelName) {
        ZLinkBackendRouterSocket router = routeRouters.get(channelName);
        if (router == null) {
            throw new ZLinkConfigurationException("route mesh channel is not configured: " + channelName);
        }
        return router;
    }

    private static List<Message> copyMessages(List<Message> parts) {
        List<Message> copy = new ArrayList<>(parts.size());
        try {
            for (Message part : parts) {
                copy.add(Message.from(part));
            }
            return copy;
        } catch (RuntimeException ex) {
            copy.forEach(Message::close);
            throw ex;
        }
    }

    private void startRequestLoop(String channelName, ZLinkBackendRouterSocket router) {
        receiveExecutor.submit(() -> {
            while (running) {
                ZLinkBackendReceived received = router.recv(ZLinkBackendRecvMode.DONT_WAIT);
                if (received != null) {
                    if (dispatchSpotRouteBridgePacket(channelName, received)) {
                        received.close();
                    } else {
                        dispatchRequest(channelName, router, received);
                    }
                } else {
                    Thread.onSpinWait();
                }
            }
        });
    }

    private void dispatchRequest(
        String channelName,
        ZLinkBackendRouterSocket router,
        ZLinkBackendReceived received) {
        try {
            if (isProbeFrame(received.parts())) {
                boot("clientServer probeSkip channel=" + channelName);
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
            if (isFrameworkErrorPacket(packet.packetName())) {
                reportDispatchError(
                    ZLinkDispatchErrorSurface.CHANNEL,
                    received.requestSeq().isPresent()
                        ? ZLinkDispatchMessageKind.REQUEST
                        : ZLinkDispatchMessageKind.SEND,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    packet.packetName(),
                    channelName,
                    received.routingId().map(RoutingId::toString).orElse(null),
                    null);
                return;
            }
            ChannelRequestHandlerRegistration registration =
                requestHandlers.getOrDefault(channelName, Map.of()).get(packet.packetName());
            if (received.routingId().isEmpty()) {
                reportDispatchError(
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    ZLinkDispatchErrorReason.REPLY_PATH_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    packet.packetName(),
                    channelName,
                    null,
                    null);
                return;
            }
            RoutingId routingId = received.routingId().get();
            if (received.requestSeq().isEmpty()) {
                dispatchSend(channelName, packet);
                return;
            }
            if (registration == null) {
                long requestSeq = received.requestSeq().get();
                replyErrorAndReport(
                    router,
                    routingId,
                    requestSeq,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    packet.packetName(),
                    channelName,
                    null,
                    null);
                return;
            }
            long requestSeq = received.requestSeq().get();
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.RECEIVED,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    packet.packetName(), channelName, null,
                    String.valueOf(requestSeq), null, null, null, null));
            }
            String packetName = packet.packetName();
            Message payloadCopy = Message.from(packet.payload());
            requestDispatchQueues.get(channelName).enqueue(() ->
                executeHandler(() -> invokeRequestHandler(channelName, registration, payloadCopy))
                    .whenComplete((reply, error) -> {
                        if (error != null) {
                            replyErrorAndReport(
                                router,
                                routingId,
                                requestSeq,
                                ZLinkDispatchErrorSurface.CHANNEL,
                                ZLinkDispatchMessageKind.REQUEST,
                                dispatchReasonFromError(error),
                                packetName,
                                channelName,
                                null,
                                error);
                        } else {
                            replyAndClose(router, routingId, requestSeq, reply);
                            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLIED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowOutcome.REPLIED,
                                    ZLinkDispatchErrorSurface.CHANNEL,
                                    ZLinkDispatchMessageKind.REQUEST,
                                    packetName, channelName, null,
                                    String.valueOf(requestSeq), null, null, null, null));
                            }
                        }
                    })
                    .whenComplete((ignored, error) -> payloadCopy.close())
                    .thenApply(ignored -> null));
        } finally {
            received.parts().forEach(Message::close);
        }
    }

    private boolean dispatchSpotRouteBridgePacket(
        String channelName,
        ZLinkBackendReceived received) {
        if (received.routingId().isEmpty()) {
            return false;
        }
        if (!looksLikeSpotRouteBridgePacket(received.parts())) {
            return false;
        }
        ZLinkBackendSpotRouteBridge bridge = spotRouteBridges.get(channelName);
        if (bridge == null) {
            if (spotRouteBridgeOwner == null) {
                return false;
            }
            ChannelRegistration registration = registrationsByName.get(channelName);
            if (registration == null || registration.kind() != ChannelKind.ROUTE_MESH) {
                return false;
            }
            bridge = requireSpotRouteBridge(channelName);
        }
        ZLinkBackendSpotRouteBridge selectedBridge = bridge;
        RoutingId sourceRid = received.routingId().get();
        long requestSeq = received.requestSeq().orElse(0L);
        List<Message> parts = copyMessages(received.parts());
        spotRouteBridgeExecutor.execute(() -> {
            try {
                synchronized (selectedBridge) {
                    if (!selectedBridge.handleRouterReceived(channelName, sourceRid, requestSeq, parts)) {
                        return;
                    }
                }
                drainSpotRouteBridgeDispatch();
            } catch (RuntimeException ex) {
                reportDispatchError(
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    received.requestSeq().isPresent()
                        ? ZLinkDispatchMessageKind.REQUEST
                        : ZLinkDispatchMessageKind.SEND,
                    ZLinkDispatchErrorReason.INVALID_FRAME,
                    ZLinkDispatchErrorAction.DROP,
                    null,
                    channelName,
                    sourceRid.toString(),
                    ex);
            } finally {
                parts.forEach(Message::close);
            }
        });
        return true;
    }

    public boolean dispatchSpotRouteBridgePacket(ZLinkBackendReceived received) {
        for (String channelName : List.copyOf(spotRouteBridges.keySet())) {
            if (dispatchSpotRouteBridgePacket(channelName, received)) {
                return true;
            }
        }
        return false;
    }

    private static boolean looksLikeSpotRouteBridgePacket(List<Message> parts) {
        if (parts.isEmpty()) {
            return false;
        }
        String packetName = parts.get(0).toUtf8String();
        return SPOT_ROUTE_BRIDGE_SEND_PACKET_NAME.equals(packetName)
            || SPOT_ROUTE_BRIDGE_REQUEST_PACKET_NAME.equals(packetName);
    }

    private void enqueueRawSpotRouteBridgeReply(
        String channelName,
        PendingRawSpotRouteBridgeReply pending) {
        synchronized (pendingRawSpotRouteBridgeReplies) {
            pendingRawSpotRouteBridgeReplies
                .computeIfAbsent(channelName, ignored -> new ArrayDeque<>())
                .addLast(pending);
        }
        pending.reply().whenComplete((ignored, error) ->
            completeRawSpotRouteBridgeReply(channelName, pending));
    }

    private void completeRawSpotRouteBridgeReply(
        String channelName,
        PendingRawSpotRouteBridgeReply completed) {
        synchronized (pendingRawSpotRouteBridgeReplies) {
            ArrayDeque<PendingRawSpotRouteBridgeReply> queue =
                pendingRawSpotRouteBridgeReplies.get(channelName);
            if (queue != null) {
                queue.removeIf(pending -> pending.reply() == completed.reply());
                if (queue.isEmpty()) {
                    pendingRawSpotRouteBridgeReplies.remove(channelName);
                }
            }
            if (completed.expectedPeerRid() != null) {
                recentlyCompletedRawSpotRouteBridgeReplies
                    .computeIfAbsent(channelName, ignored -> new HashMap<>())
                    .put(
                        completed.expectedPeerRid(),
                        System.nanoTime() + defaultRequestTimeout.toNanos());
            }
        }
    }

    private void removeRawSpotRouteBridgeReply(
        String channelName,
        CompletableFuture<List<Message>> pendingReply) {
        synchronized (pendingRawSpotRouteBridgeReplies) {
            ArrayDeque<PendingRawSpotRouteBridgeReply> queue =
                pendingRawSpotRouteBridgeReplies.get(channelName);
            if (queue == null) {
                return;
            }
            queue.removeIf(pending -> pending.reply() == pendingReply);
            if (queue.isEmpty()) {
                pendingRawSpotRouteBridgeReplies.remove(channelName);
            }
        }
    }

    private boolean tryCompleteRawSpotRouteBridgeReply(
        String channelName,
        ZLinkBackendReceived received) {
        if (received.routingId().isEmpty()) {
            return false;
        }
        PendingRawSpotRouteBridgeReply pending;
        synchronized (pendingRawSpotRouteBridgeReplies) {
            ArrayDeque<PendingRawSpotRouteBridgeReply> queue =
                pendingRawSpotRouteBridgeReplies.get(channelName);
            if (queue == null || queue.isEmpty()) {
                return false;
            }
            pending = queue.peekFirst();
        }
        if (pending.expectedPeerRid() != null
            && !pending.expectedPeerRid().equals(received.routingId().get())) {
            return false;
        }
        if (sameMessageParts(pending.requestParts(), received.parts())
            || sameFirstMessagePart(pending.requestParts(), received.parts())) {
            return false;
        }
        if (isFrameworkErrorReply(received.parts())) {
            pending.reply().completeExceptionally(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                frameworkErrorReplyMessage(received.parts())));
            return true;
        }
        List<Message> copiedReply = copySpotRouteBridgeReplyMessages(received.parts());
        if (sameFirstMessagePart(pending.requestParts(), copiedReply)) {
            copiedReply.forEach(Message::close);
            return false;
        }
        pending.reply().complete(copiedReply);
        return true;
    }

    private static List<Message> copySpotRouteBridgeReplyMessages(List<Message> parts) {
        int payloadOffset = 0;
        if (looksLikeSpotRouteBridgePacket(parts)) {
            payloadOffset = 1;
        }
        if (parts.size() - payloadOffset > 1) {
            payloadOffset++;
        }
        if (payloadOffset > 0 && payloadOffset < parts.size()) {
            return copyMessages(parts.subList(payloadOffset, parts.size()));
        }
        return copyMessages(parts);
    }

    private static List<byte[]> copyMessageBytes(List<Message> parts) {
        return parts.stream()
            .map(Message::toByteArray)
            .toList();
    }

    private static boolean sameMessageParts(
        List<byte[]> expected,
        List<Message> actual) {
        if (expected.size() != actual.size()) {
            return false;
        }
        for (int i = 0; i < expected.size(); i++) {
            if (!java.util.Arrays.equals(expected.get(i), actual.get(i).toByteArray())) {
                return false;
            }
        }
        return true;
    }

    private static boolean sameFirstMessagePart(
        List<byte[]> expected,
        List<Message> actual) {
        if (expected.isEmpty() || actual.isEmpty()) {
            return false;
        }
        return java.util.Arrays.equals(expected.get(0), actual.get(0).toByteArray());
    }

    private boolean hasPendingRawSpotRouteBridgeReply(
        String channelName,
        RoutingId peerRid) {
        synchronized (pendingRawSpotRouteBridgeReplies) {
            ArrayDeque<PendingRawSpotRouteBridgeReply> queue =
                pendingRawSpotRouteBridgeReplies.get(channelName);
            if (queue == null || queue.isEmpty()) {
                return false;
            }
            PendingRawSpotRouteBridgeReply pending = queue.peekFirst();
            return pending.expectedPeerRid() == null
                || pending.expectedPeerRid().equals(peerRid);
        }
    }

    private boolean discardRecentlyCompletedRawSpotRouteBridgeReply(
        String channelName,
        ZLinkBackendReceived received) {
        if (received.routingId().isEmpty() || received.requestSeq().isEmpty()) {
            return false;
        }
        RoutingId routingId = received.routingId().get();
        long now = System.nanoTime();
        synchronized (pendingRawSpotRouteBridgeReplies) {
            Map<RoutingId, Long> recent =
                recentlyCompletedRawSpotRouteBridgeReplies.get(channelName);
            if (recent == null) {
                return false;
            }
            recent.entrySet().removeIf(entry -> entry.getValue() < now);
            Long deadline = recent.remove(routingId);
            if (recent.isEmpty()) {
                recentlyCompletedRawSpotRouteBridgeReplies.remove(channelName);
            }
            return deadline != null && deadline >= now;
        }
    }

    private static boolean isFrameworkErrorReply(List<Message> parts) {
        return parts.size() >= 2
            && FRAMEWORK_ERROR_REPLY_MARKER.equals(parts.get(0).toUtf8String());
    }

    private static boolean isFrameworkErrorPacket(String packetName) {
        return FRAMEWORK_ERROR_REPLY_MARKER.equals(packetName);
    }

    private static String frameworkErrorReplyMessage(List<Message> parts) {
        return parts.get(1).toUtf8String();
    }

    private void startRouteLoop(String channelName, ZLinkBackendRouterSocket router) {
        boot("routeLoop submit channel=" + channelName);
        receiveExecutor.submit(() -> {
            boot("routeLoop running channel=" + channelName);
            while (running) {
                try {
                    drainSpotRouteBridgeNow(channelName);
                    ZLinkBackendReceived received;
                    Object routeSocketLock = routeSocketLocks.getOrDefault(channelName, this);
                    synchronized (routeSocketLock) {
                        received = router.recv(ZLinkBackendRecvMode.DONT_WAIT);
                    }
                    if (received != null) {
                        dispatchRouteRequest(channelName, router, received);
                    } else {
                        drainSpotRouteBridgeNow(channelName);
                        Thread.onSpinWait();
                    }
                } catch (RuntimeException ex) {
                    if (isNoDataReceive(ex)) {
                        Thread.onSpinWait();
                        continue;
                    }
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                        ZLinkDispatchMessageKind.REQUEST,
                        ZLinkDispatchErrorReason.INVALID_FRAME,
                        ZLinkDispatchErrorAction.DROP,
                        null,
                        channelName,
                        null,
                        ex);
                }
            }
        });
    }

    private void drainSpotRouteBridge(String channelName) {
        ZLinkBackendSpotRouteBridge bridge = spotRouteBridges.get(channelName);
        if (bridge == null || !spotRouteBridgeDrainScheduled.add(channelName)) {
            return;
        }
        spotRouteBridgeExecutor.execute(() -> {
            try {
                synchronized (bridge) {
                    bridge.drain();
                }
                drainSpotRouteBridgeDispatch();
            } catch (RuntimeException ex) {
                if (isNoDataReceive(ex)) {
                    return;
                }
                if (running) {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                        ZLinkDispatchMessageKind.REQUEST,
                        ZLinkDispatchErrorReason.INVALID_FRAME,
                        ZLinkDispatchErrorAction.DROP,
                        null,
                        channelName,
                        null,
                        ex);
                }
            } finally {
                spotRouteBridgeDrainScheduled.remove(channelName);
            }
        });
    }

    private void drainSpotRouteBridgeNow(String channelName) {
        ZLinkBackendSpotRouteBridge bridge = spotRouteBridges.get(channelName);
        if (bridge == null) {
            return;
        }
        try {
            synchronized (bridge) {
                bridge.drain();
            }
        } catch (RuntimeException ex) {
            if (isNoDataReceive(ex)) {
                return;
            }
            throw ex;
        }
        drainSpotRouteBridgeDispatch();
    }

    private void drainSpotRouteBridgeDispatch() {
        Runnable drainer = spotRouteBridgeDispatchDrainer;
        if (drainer != null) {
            drainer.run();
        }
    }

    private static boolean isNoDataReceive(Throwable error) {
        Throwable current = error;
        while (current != null) {
            if (current instanceof ZlinkRecvException ex) {
                return true;
            }
            current = current.getCause();
        }
        return false;
    }

    private void dispatchRouteRequest(
        String channelName,
        ZLinkBackendRouterSocket router,
        ZLinkBackendReceived received) {
        try {
            if (isProbeFrame(received.parts())) {
                boot("routeMesh probeSkip channel=" + channelName);
                return;
            }
            if (tryCompleteRawSpotRouteBridgeReply(channelName, received)) {
                return;
            }
            if (dispatchSpotRouteBridgePacket(channelName, received)) {
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
            if (isFrameworkErrorPacket(packet.packetName())) {
                reportDispatchError(
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    received.requestSeq().isPresent()
                        ? ZLinkDispatchMessageKind.REQUEST
                        : ZLinkDispatchMessageKind.SEND,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    packet.packetName(),
                    channelName,
                    received.routingId().map(RoutingId::toString).orElse(null),
                    null);
                return;
            }
            ChannelRouteRequestHandlerRegistration registration =
                routeRequestHandlers.getOrDefault(channelName, Map.of()).get(packet.packetName());
            if (received.routingId().isEmpty()) {
                reportDispatchError(
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    ZLinkDispatchErrorReason.REPLY_PATH_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    packet.packetName(),
                    channelName,
                    null,
                    null);
                return;
            }
            RoutingId routingId = received.routingId().get();
            if (received.requestSeq().isEmpty()) {
                dispatchRouteSend(channelName, routingId, packet);
                return;
            }
            if (dispatchRouteInternalRequest(channelName, router, routingId, received.requestSeq().get(), packet)) {
                return;
            }
            if (registration == null
                && hasPendingRawSpotRouteBridgeReply(channelName, routingId)) {
                return;
            }
            if (registration == null
                && discardRecentlyCompletedRawSpotRouteBridgeReply(channelName, received)) {
                return;
            }
            if (registration == null) {
                long requestSeq = received.requestSeq().get();
                replyErrorAndReport(
                    router,
                    routingId,
                    requestSeq,
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    packet.packetName(),
                    channelName,
                    routingId.toString(),
                    null);
                return;
            }
            long requestSeq = received.requestSeq().get();
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.RECEIVED,
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    packet.packetName(), channelName, null,
                    String.valueOf(requestSeq), routingId.toString(), null, null, null));
            }
            Message payloadCopy = Message.from(packet.payload());
            routeRequestDispatchQueues.get(channelName).enqueue(() ->
                executeHandler(() -> invokeRouteRequestHandler(
                    channelName,
                    registration,
                    routingId,
                    payloadCopy))
                    .whenComplete((reply, error) -> {
                        if (error != null) {
                            replyErrorAndReport(
                                router,
                                routingId,
                                requestSeq,
                                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                                ZLinkDispatchMessageKind.REQUEST,
                                dispatchReasonFromError(error),
                                packet.packetName(),
                                channelName,
                                routingId.toString(),
                                error);
                        } else {
                            replyAndClose(router, routingId, requestSeq, reply);
                            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLIED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowOutcome.REPLIED,
                                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                                    ZLinkDispatchMessageKind.REQUEST,
                                    packet.packetName(), channelName, null,
                                    String.valueOf(requestSeq), routingId.toString(), null, null, null));
                            }
                        }
                    })
                    .whenComplete((ignored, error) -> payloadCopy.close())
                    .thenApply(ignored -> null));
        } finally {
            received.parts().forEach(Message::close);
        }
    }

    private boolean dispatchRouteInternalRequest(
        String channelName,
        ZLinkBackendRouterSocket router,
        RoutingId sourceRoutingId,
        long requestSeq,
        ParsedPacket packet) {
        RouteInternalRequestHandler handler = routeInternalRequestHandlers.get(packet.packetName());
        if (handler == null) {
            return false;
        }
        Message payloadCopy = Message.from(packet.payload());
        routeRequestDispatchQueues.get(channelName).enqueue(() ->
            handler.handle(sourceRoutingId, payloadCopy)
                .thenAccept(reply -> replyAndClose(router, sourceRoutingId, requestSeq, reply))
                .whenComplete((ignored, error) -> payloadCopy.close()));
        return true;
    }

    private void startSubscribeLoop(String channelName, ZLinkBackendSubscriberSocket subscriber) {
        receiveExecutor.submit(() -> {
            while (running) {
                ZLinkBackendTopicMessage received = subscriber.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received != null) {
                    dispatchPublish(channelName, received);
                } else {
                    Thread.onSpinWait();
                }
            }
        });
    }

    private void dispatchPublish(String channelName, ZLinkBackendTopicMessage received) {
        try {
            ParsedPacket packet = parsePacket(received.parts());
            ChannelPublishHandlerRegistration registration =
                publishHandlers.getOrDefault(channelName, Map.of()).get(packet.packetName());
            if (registration == null) {
                reportDispatchError(
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.PUBLISH,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    packet.packetName(),
                    channelName,
                    received.topic(),
                    null,
                    null);
                return;
            }
            String publishPacketName = packet.packetName();
            String publishTopic = received.topic();
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.RECEIVED,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.PUBLISH,
                    publishPacketName, channelName, publishTopic,
                    null, null, null, null, null));
            }
            Message payloadCopy = Message.from(packet.payload());
            publishDispatchQueues.get(channelName).enqueue(() ->
                executeHandler(() -> invokePublishHandler(channelName, registration, received.topic(), payloadCopy))
                    .whenComplete((ignored, error) -> {
                        if (error != null) {
                            reportDispatchError(
                                ZLinkDispatchErrorSurface.CHANNEL,
                                ZLinkDispatchMessageKind.PUBLISH,
                                dispatchReasonFromError(error),
                                ZLinkDispatchErrorAction.DROP,
                                publishPacketName,
                                channelName,
                                publishTopic,
                                null,
                                error);
                        } else if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.DISPATCHED)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowOutcome.DISPATCHED,
                                ZLinkDispatchErrorSurface.CHANNEL,
                                ZLinkDispatchMessageKind.PUBLISH,
                                publishPacketName, channelName, publishTopic,
                                null, null, null, null, null));
                        }
                    })
                    .whenComplete((ignored, error) -> payloadCopy.close()));
        } finally {
            received.parts().forEach(Message::close);
        }
    }

    private void dispatchSend(String channelName, ParsedPacket packet) {
        ChannelSendHandlerRegistration registration =
            sendHandlers.getOrDefault(channelName, Map.of()).get(packet.packetName());
        if (registration == null) {
            reportDispatchError(
                ZLinkDispatchErrorSurface.CHANNEL,
                ZLinkDispatchMessageKind.SEND,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                ZLinkDispatchErrorAction.DROP,
                packet.packetName(),
                channelName,
                null,
                null);
            return;
        }
        String sendPacketName = packet.packetName();
        if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.RECEIVED,
                ZLinkDispatchErrorSurface.CHANNEL,
                ZLinkDispatchMessageKind.SEND,
                sendPacketName, channelName, null, null, null, null, null, null));
        }
        Message payloadCopy = Message.from(packet.payload());
        sendDispatchQueues.get(channelName).enqueue(() ->
            executeHandler(() -> invokeSendHandler(channelName, registration, payloadCopy))
                .whenComplete((ignored, error) -> {
                    if (error != null) {
                        reportDispatchError(
                            ZLinkDispatchErrorSurface.CHANNEL,
                            ZLinkDispatchMessageKind.SEND,
                            dispatchReasonFromError(error),
                            ZLinkDispatchErrorAction.DROP,
                            sendPacketName,
                            channelName,
                            null,
                            error);
                    } else if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.DISPATCHED)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowOutcome.DISPATCHED,
                            ZLinkDispatchErrorSurface.CHANNEL,
                            ZLinkDispatchMessageKind.SEND,
                            sendPacketName, channelName, null, null, null, null, null, null));
                    }
                })
                .whenComplete((ignored, error) -> payloadCopy.close()));
    }

    private void dispatchRouteSend(
        String channelName,
        RoutingId sourceRoutingId,
        ParsedPacket packet) {
        ChannelRouteSendHandlerRegistration registration =
            routeSendHandlers.getOrDefault(channelName, Map.of()).get(packet.packetName());
        if (registration == null) {
            reportDispatchError(
                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                ZLinkDispatchMessageKind.SEND,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                ZLinkDispatchErrorAction.DROP,
                packet.packetName(),
                channelName,
                null,
                null);
            return;
        }
        String routeSendPacketName = packet.packetName();
        if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.RECEIVED,
                ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                ZLinkDispatchMessageKind.SEND,
                routeSendPacketName, channelName, null, null,
                sourceRoutingId.toString(), null, null, null));
        }
        Message payloadCopy = Message.from(packet.payload());
        routeSendDispatchQueues.get(channelName).enqueue(() ->
            executeHandler(() -> invokeRouteSendHandler(channelName, registration, sourceRoutingId, payloadCopy))
                .whenComplete((ignored, error) -> {
                    if (error != null) {
                        reportDispatchError(
                            ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                            ZLinkDispatchMessageKind.SEND,
                            dispatchReasonFromError(error),
                            ZLinkDispatchErrorAction.DROP,
                            routeSendPacketName,
                            channelName,
                            null,
                            error);
                    } else if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.DISPATCHED)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowOutcome.DISPATCHED,
                            ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                            ZLinkDispatchMessageKind.SEND,
                            routeSendPacketName, channelName, null, null,
                            sourceRoutingId.toString(), null, null, null));
                    }
                })
                .whenComplete((ignored, error) -> payloadCopy.close()));
    }

    private void replyErrorAndReport(
        ZLinkBackendRouterSocket router,
        RoutingId routingId,
        long requestSeq,
        ZLinkDispatchErrorSurface surface,
        ZLinkDispatchMessageKind kind,
        ZLinkDispatchErrorReason reason,
        String packetName,
        String channelName,
        String sourceRid,
        Throwable error) {
        Throwable cause = unwrapCompletion(error);
        List<Message> reply = List.of(
            Message.from(FRAMEWORK_ERROR_REPLY_MARKER.getBytes(StandardCharsets.UTF_8)),
            Message.from(errorText(reason, packetName, cause).getBytes(StandardCharsets.UTF_8)));
        replyRawAndClose(router, routingId, requestSeq, reply);
        reportDispatchError(
            surface,
            kind,
            reason,
            ZLinkDispatchErrorAction.REPLY_ERROR,
            packetName,
            channelName,
            sourceRid,
            cause);
    }

    private void reportDispatchError(
        ZLinkDispatchErrorSurface surface,
        ZLinkDispatchMessageKind kind,
        ZLinkDispatchErrorReason reason,
        ZLinkDispatchErrorAction action,
        String packetName,
        String channelName,
        String sourceRid,
        Throwable error) {
        reportDispatchError(surface, kind, reason, action, packetName, channelName, null, sourceRid, error);
    }

    private void reportDispatchError(
        ZLinkDispatchErrorSurface surface,
        ZLinkDispatchMessageKind kind,
        ZLinkDispatchErrorReason reason,
        ZLinkDispatchErrorAction action,
        String packetName,
        String channelName,
        String topic,
        String sourceRid,
        Throwable error) {
        Throwable cause = unwrapCompletion(error);
        dispatchErrors.report(new ZLinkDispatchFailure(
            surface,
            kind,
            reason,
            action,
            packetName == null || packetName.isBlank() ? null : packetName,
            channelName,
            topic,
            null,
            null,
            sourceRid,
            null,
            errorType(cause),
            errorMessage(cause)));
    }

    private static Throwable unwrapCompletion(Throwable error) {
        if (error instanceof CompletionException && error.getCause() != null) {
            return error.getCause();
        }
        return error;
    }

    private static String errorType(Throwable error) {
        return error == null ? null : error.getClass().getSimpleName();
    }

    private static String errorMessage(Throwable error) {
        return error == null ? null : error.getMessage();
    }

    private static ZLinkDispatchErrorReason dispatchReasonFromError(Throwable error) {
        return unwrapCompletion(error) instanceof PayloadDecodeDispatchException
            ? ZLinkDispatchErrorReason.PAYLOAD_DECODE_FAILED
            : ZLinkDispatchErrorReason.HANDLER_EXCEPTION;
    }

    private static String errorText(
        ZLinkDispatchErrorReason reason,
        String packetName,
        Throwable error) {
        if (error != null && error.getMessage() != null) {
            return error.getMessage();
        }
        return reason + " for packet '" + packetName + "'";
    }

    private <T> CompletionStage<T> executeHandler(
        java.util.function.Supplier<CompletionStage<T>> operation) {
        CompletableFuture<T> result = new CompletableFuture<>();
        ZLinkYieldTurn turn = ZLinkFrameworkTurns.captureCurrent();
        try {
            handlerExecutor.execute(() -> {
                ZLinkFrameworkTurns.runWithTurn(turn, () -> {
                    try {
                        operation.get().whenComplete((value, error) -> {
                            if (error != null) {
                                result.completeExceptionally(error);
                            } else {
                                result.complete(value);
                            }
                        });
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    }
                    return null;
                });
            });
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        }
        return result;
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Void> invokeSendHandler(
        String channelName,
        ChannelSendHandlerRegistration registration,
        Message payload) {
        Object message;
        try {
            message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkSendContext context = new DefaultSendContext(channelName, registration.packetName());
            return invokeWithFilters(context, message, () ->
                invokeSendHandlerCore(registration, message, context));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Void> invokeSendHandlerCore(
        ChannelSendHandlerRegistration registration,
        Object message,
        ZLinkSendContext context) {
        try {
            if (registration.handlerMethod() != null) {
                return invokeVoidMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    message,
                    context);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {message, context}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Message> invokeRequestHandler(
        String channelName,
        ChannelRequestHandlerRegistration registration,
        Message payload) {
        Object request;
        try {
            request = ZLinkMessagePayloads.deserialize(serializer, payload, registration.requestType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkRequestContext context = new DefaultRequestContext(channelName, registration.packetName());
            return invokeWithFilters(context, request, () ->
                invokeRequestHandlerCore(registration, request, context))
                .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Object> invokeRequestHandlerCore(
        ChannelRequestHandlerRegistration registration,
        Object request,
        ZLinkRequestContext context) {
        try {
            if (registration.handlerMethod() != null) {
                return invokeReplyMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    request,
                    context);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {request, context}, suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Void> invokePublishHandler(
        String channelName,
        ChannelPublishHandlerRegistration registration,
        String topic,
        Message payload) {
        Object message;
        try {
            message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkPublishContext context = new DefaultPublishContext(channelName, registration.packetName(), topic);
            return invokeWithFilters(context, message, () ->
                invokePublishHandlerCore(registration, message, context));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Void> invokePublishHandlerCore(
        ChannelPublishHandlerRegistration registration,
        Object message,
        ZLinkPublishContext context) {
        try {
            if (registration.handlerMethod() != null) {
                return invokeVoidMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    message,
                    context);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {message, context}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private CompletionStage<Void> invokeVoidMethodHandler(
        Class<?> handlerType,
        Method method,
        Object message,
        ZLinkHandlerContext context) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker
                .invoke(handler, method, methodArguments(method, message, context), suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke handler method: " + handlerType.getName() + "." + method.getName(),
                ex));
        }
    }

    private CompletionStage<Object> invokeReplyMethodHandler(
        Class<?> handlerType,
        Method method,
        Object message,
        ZLinkHandlerContext context) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker.invoke(
                handler,
                method,
                methodArguments(method, message, context),
                suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke handler method: " + handlerType.getName() + "." + method.getName(),
                ex));
        }
    }

    static Object[] methodArguments(
        Method method,
        Object message,
        ZLinkHandlerContext context) {
        Class<?>[] parameterTypes = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        Object[] arguments = new Object[parameterTypes.length];
        arguments[0] = message;
        for (int index = 1; index < parameterTypes.length; index++) {
            if (parameterTypes[index] == CancellationToken.class) {
                arguments[index] = context.cancellationToken();
            } else if (parameterTypes[index].isInstance(context)) {
                arguments[index] = context;
            } else {
                arguments[index] = null;
            }
        }
        return arguments;
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Void> invokeRouteSendHandler(
        String channelName,
        ChannelRouteSendHandlerRegistration registration,
        RoutingId sourceRoutingId,
        Message payload) {
        Object message;
        try {
            message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkRouteSendContext context =
                new DefaultRouteSendContext(channelName, registration.packetName(), sourceRoutingId);
            if (registration.handlerMethod() != null) {
                Object handler = handlerFactory.create(registration.handlerType());
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(),
                        methodArguments(registration.handlerMethod(), message, context),
                        suspendHandlerInvokers)
                    .thenApply(ignored -> null);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {message, context}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Message> invokeRouteRequestHandler(
        String channelName,
        ChannelRouteRequestHandlerRegistration registration,
        RoutingId sourceRoutingId,
        Message payload) {
        Object request;
        try {
            request = ZLinkMessagePayloads.deserialize(serializer, payload, registration.requestType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkRouteRequestContext context =
                new DefaultRouteRequestContext(channelName, registration.packetName(), sourceRoutingId);
            if (registration.handlerMethod() != null) {
                Object handler = handlerFactory.create(registration.handlerType());
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(),
                        methodArguments(registration.handlerMethod(), request, context),
                        suspendHandlerInvokers)
                    .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {request, context}, suspendHandlerInvokers)
                .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private static PayloadDecodeDispatchException payloadDecodeFailure(
        String channelName,
        String packetName,
        RuntimeException cause) {
        return new PayloadDecodeDispatchException(
            "PayloadDecodeFailed: failed to decode payload for '" + channelName + ":" + packetName + "'.",
            cause);
    }

    private static final class PayloadDecodeDispatchException extends RuntimeException {
        PayloadDecodeDispatchException(String message, Throwable cause) {
            super(message, cause);
        }
    }

    private <T> CompletionStage<T> invokeWithFilters(
        ZLinkHandlerContext context,
        Object request,
        java.util.function.Supplier<CompletionStage<T>> terminal) {
        if (filterTypes.isEmpty()) {
            return terminal.get();
        }
        return ZLinkFilterPipeline.invokeAsync(
            filterTypes,
            handlerFactory,
            context,
            request,
            terminal);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static Map<String, ChannelRequestHandlerRegistration> handlersByPacket(
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, ChannelRequestHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.matching(
            Set.copyOf(channel.handlerGroups()),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.REQUEST)) {
            handlers.put(handler.packetName(), new ChannelRequestHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.messageType(),
                handler.replyType(),
                handler.packetName()));
        }
        for (ChannelRequestHandlerRegistration handler : channel.requestHandlers()) {
            handlers.put(handler.packetName(), handler);
        }
        return Map.copyOf(handlers);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static Map<String, ChannelSendHandlerRegistration> sendHandlersByPacket(
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, ChannelSendHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.matching(
            Set.copyOf(channel.handlerGroups()),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.SEND)) {
            handlers.put(handler.packetName(), new ChannelSendHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.messageType(),
                handler.packetName()));
        }
        for (ChannelSendHandlerRegistration handler : channel.sendHandlers()) {
            handlers.put(handler.packetName(), handler);
        }
        return Map.copyOf(handlers);
    }

    private static void replyAndClose(
        ZLinkBackendRouterSocket router,
        RoutingId routingId,
        long requestSeq,
        Message reply) {
        try {
            router.reply(routingId, requestSeq, List.of(reply));
        } finally {
            reply.close();
        }
    }

    private static void replyRawAndClose(
        ZLinkBackendRouterSocket router,
        RoutingId routingId,
        long requestSeq,
        List<Message> reply) {
        try {
            router.reply(routingId, requestSeq, reply);
        } finally {
            reply.forEach(Message::close);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static Map<String, ChannelPublishHandlerRegistration> publishHandlersByPacket(
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, ChannelPublishHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.matching(
            Set.copyOf(channel.handlerGroups()),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.PUBLISH)) {
            handlers.put(handler.packetName(), new ChannelPublishHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.messageType(),
                handler.packetName()));
        }
        for (ChannelPublishHandlerRegistration handler : channel.publishHandlers()) {
            handlers.put(handler.packetName(), handler);
        }
        return Map.copyOf(handlers);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static Map<String, ChannelRouteRequestHandlerRegistration> routeHandlersByPacket(
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, ChannelRouteRequestHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.matching(
            Set.copyOf(channel.handlerGroups()),
            ZLinkScannedHandlerSurface.ROUTE,
            ZLinkScannedHandlerKind.REQUEST)) {
            handlers.put(handler.packetName(), new ChannelRouteRequestHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.messageType(),
                handler.replyType(),
                handler.packetName()));
        }
        for (ChannelRouteRequestHandlerRegistration handler : channel.routeRequestHandlers()) {
            handlers.put(handler.packetName(), handler);
        }
        return Map.copyOf(handlers);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static Map<String, ChannelRouteSendHandlerRegistration> routeSendHandlersByPacket(
        ChannelRegistration channel,
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, ChannelRouteSendHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.matching(
            Set.copyOf(channel.handlerGroups()),
            ZLinkScannedHandlerSurface.ROUTE,
            ZLinkScannedHandlerKind.SEND)) {
            handlers.put(handler.packetName(), new ChannelRouteSendHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.messageType(),
                handler.packetName()));
        }
        for (ChannelRouteSendHandlerRegistration handler : channel.routeSendHandlers()) {
            handlers.put(handler.packetName(), handler);
        }
        return Map.copyOf(handlers);
    }

    private static ParsedPacket parsePacket(List<Message> parts) {
        if (parts.size() >= 2) {
            return new ParsedPacket(parts.get(0).toUtf8String(), parts.get(1));
        }
        return new ParsedPacket("", parts.get(0));
    }

    private static boolean isProbeFrame(List<Message> parts) {
        return parts.isEmpty() || parts.get(0).size() == 0;
    }

    private static void boot(String step) {
        System.out.println("[boot] component=channel-runtime step=" + step);
    }

    private record ParsedPacket(String packetName, Message payload) {
    }

    private static final class DefaultRequestContext implements ZLinkRequestContext {
        private static final CancellationToken NONE = () -> false;
        private final String channelName;
        private final String packetName;

        DefaultRequestContext(String channelName, String packetName) {
            this.channelName = channelName;
            this.packetName = packetName;
        }

        @Override
        public Optional<String> channelName() {
            return Optional.ofNullable(channelName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private static final class DefaultSendContext implements ZLinkSendContext {
        private static final CancellationToken NONE = () -> false;
        private final String channelName;
        private final String packetName;

        DefaultSendContext(String channelName, String packetName) {
            this.channelName = channelName;
            this.packetName = packetName;
        }

        @Override
        public Optional<String> channelName() {
            return Optional.ofNullable(channelName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private static final class DefaultPublishContext implements ZLinkPublishContext {
        private static final CancellationToken NONE = () -> false;
        private final String channelName;
        private final String packetName;
        private final String topic;

        DefaultPublishContext(String channelName, String packetName, String topic) {
            this.channelName = channelName;
            this.packetName = packetName;
            this.topic = topic;
        }

        @Override
        public String topic() {
            return topic;
        }

        @Override
        public Optional<String> source() {
            return Optional.empty();
        }

        @Override
        public Optional<String> channelName() {
            return Optional.ofNullable(channelName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private static final class DefaultRouteRequestContext implements ZLinkRouteRequestContext {
        private static final CancellationToken NONE = () -> false;
        private final String channelName;
        private final String packetName;
        private final RoutingId routingId;

        DefaultRouteRequestContext(String channelName, String packetName, RoutingId routingId) {
            this.channelName = channelName;
            this.packetName = packetName;
            this.routingId = routingId;
        }

        @Override
        public RoutingId routingId() {
            return routingId;
        }

        @Override
        public Optional<String> channelName() {
            return Optional.ofNullable(channelName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private static final class DefaultRouteSendContext implements ZLinkRouteSendContext {
        private static final CancellationToken NONE = () -> false;
        private final String channelName;
        private final String packetName;
        private final RoutingId routingId;

        DefaultRouteSendContext(String channelName, String packetName, RoutingId routingId) {
            this.channelName = channelName;
            this.packetName = packetName;
            this.routingId = routingId;
        }

        @Override
        public RoutingId routingId() {
            return routingId;
        }

        @Override
        public Optional<String> channelName() {
            return Optional.ofNullable(channelName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private final class PublishCall implements ZLinkPublishCall {
        private final ZLinkBackendPublisherSocket publisher;
        private final String topic;
        private final Message payload;
        private final Optional<String> packetName;

        PublishCall(
            ZLinkBackendPublisherSocket publisher,
            String topic,
            Message payload,
            Optional<String> packetName) {
            this.publisher = publisher;
            this.topic = topic;
            this.payload = payload;
            this.packetName = packetName;
        }

        PublishCall(ZLinkBackendPublisherSocket publisher, String topic, Message payload) {
            this(publisher, topic, payload, Optional.empty());
        }

        @Override
        public ZLinkPublishCall packetName(String packetName) {
            return new PublishCall(publisher, topic, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkPublishCall metadata(String key, String value) {
            return this;
        }

        @Override
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.PUBLISH,
                    packetName.orElse(null), null, topic, null, null, null, null, null));
            }
            return systems.zlink.framework.ZLinkSubmitStage.from(CompletableFuture.runAsync(() -> {
                List<Message> publishParts = parts(packetName, payload);
                try {
                    publisher.publish(topic, publishParts, SendFlags.NONE);
                } finally {
                    publishParts.forEach(Message::close);
                }
            }));
        }
    }

    private final class SendCall implements ZLinkSendCall {
        private final ZLinkBackendDealerSocket client;
        private final Message payload;
        private final Optional<String> packetName;

        SendCall(ZLinkBackendDealerSocket client, Message payload, Optional<String> packetName) {
            this.client = client;
            this.payload = payload;
            this.packetName = packetName;
        }

        SendCall(ZLinkBackendDealerSocket client, Message payload) {
            this(client, payload, Optional.empty());
        }

        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new SendCall(client, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.SEND,
                    packetName.orElse(null), null, null, null, null, null, null, null));
            }
            return systems.zlink.framework.ZLinkSubmitStage.from(CompletableFuture.runAsync(() -> {
                try {
                    client.send(parts(packetName, payload), SendFlags.NONE);
                } finally {
                    payload.close();
                }
            }));
        }
    }

    private final class RequestCall implements ZLinkYieldRequestCall {
        private final ZLinkBackendDealerSocket client;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;
        private final ZLinkYieldTurn turn;

        private RequestCall(
            ZLinkBackendDealerSocket client,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this(client, payload, packetName, timeout, ZLinkFrameworkTurns.captureCurrent());
        }

        private RequestCall(
            ZLinkBackendDealerSocket client,
            Message payload,
            Optional<String> packetName,
            Duration timeout,
            ZLinkYieldTurn turn) {
            this.client = client;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
            this.turn = turn;
        }

        @Override
        public ZLinkYieldRequestCall packetName(String packetName) {
            return new RequestCall(client, payload, Optional.of(packetName), timeout, turn);
        }

        @Override
        public ZLinkYieldRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkYieldRequestCall timeout(Duration timeout) {
            return new RequestCall(client, payload, packetName, timeout, turn);
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            trackPendingRequest(result, timeout);
            List<Message> requestParts = parts(packetName, payload);
            result.whenComplete((ignored, error) -> requestParts.forEach(Message::close));
            String reqPacket = packetName.orElse(null);
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    reqPacket, null, null, null, null, null, null, null));
            }
            submitClientRequestWithRetry(
                client,
                requestParts,
                timeout,
                reply -> {
                    try {
                        completeRequestReply(reply, replyType, result);
                        if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                                ZLinkDispatchErrorSurface.CHANNEL,
                                ZLinkDispatchMessageKind.RESPONSE,
                                reqPacket, null, null, null, null, null, null, null));
                        }
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        reply.parts().forEach(Message::close);
                    }
                },
                result);
            return result;
        }

        @Override
        public <TReply> TReply yield(Class<TReply> replyType) {
            return ZLinkAwait.await(
                ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit(replyType)));
        }

        @Override
        public <TReply> TReply yield(Class<TReply> replyType, CancellationToken cancellationToken) {
            ZLinkFrameworkTurns.throwIfCancellationRequested(cancellationToken);
            return ZLinkAwait.await(
                ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit(replyType), cancellationToken));
        }

        private ZLinkYieldTurn requireTurn() {
            if (turn == null) {
                ZLinkYieldTurn current = ZLinkFrameworkTurns.captureCurrent();
                if (current != null) {
                    return current;
                }
                throw new IllegalStateException(
                    "yield requires a framework Spot handler turn captured when the call object was created");
            }
            return turn;
        }
    }

    private <TReply> void completeRequestReply(
        ZLinkBackendReceived reply,
        Class<TReply> replyType,
        CompletableFuture<TReply> result) {
        if (reply.result() != ZLinkBackendRequestResult.OK) {
            result.completeExceptionally(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                "channel request failed: " + reply.result()));
            return;
        }
        if (isFrameworkErrorReply(reply.parts())) {
            String message = frameworkErrorReplyMessage(reply.parts());
            result.completeExceptionally(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                message));
            return;
        }
        Message emptyReply = null;
        Message firstReply = reply.parts().isEmpty()
            ? (emptyReply = Message.from(new byte[0]))
            : reply.parts().get(0);
        try {
            result.complete(ZLinkMessagePayloads.deserialize(serializer, firstReply, replyType));
        } finally {
            if (emptyReply != null) {
                emptyReply.close();
            }
        }
    }

    private final class RouteSendCall implements ZLinkSendCall {
        private final ZLinkBackendRouterSocket router;
        private final RoutingId target;
        private final Message payload;
        private final Optional<String> packetName;

        RouteSendCall(
            ZLinkBackendRouterSocket router,
            RoutingId target,
            Message payload,
            Optional<String> packetName) {
            this.router = router;
            this.target = target;
            this.payload = payload;
            this.packetName = packetName;
        }

        RouteSendCall(ZLinkBackendRouterSocket router, RoutingId target, Message payload) {
            this(router, target, payload, Optional.empty());
        }

        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new RouteSendCall(router, target, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    ZLinkDispatchMessageKind.SEND,
                    packetName.orElse(null), null, null, null, target.toString(), null, null, null));
            }
            return systems.zlink.framework.ZLinkSubmitStage.from(CompletableFuture.runAsync(() -> {
                List<Message> sendParts = parts(packetName, payload);
                try {
                    router.send(target, sendParts, SendFlags.NONE);
                } finally {
                    sendParts.forEach(Message::close);
                }
            }));
        }
    }

    private final class RouteRequestCall implements ZLinkRequestCall {
        private final String channelName;
        private final ZLinkBackendRouterSocket router;
        private final RoutingId target;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private RouteRequestCall(
            String channelName,
            ZLinkBackendRouterSocket router,
            RoutingId target,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.channelName = channelName;
            this.router = router;
            this.target = target;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new RouteRequestCall(
                channelName,
                router,
                target,
                payload,
                Optional.of(packetName),
                timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new RouteRequestCall(channelName, router, target, payload, packetName, timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            trackPendingRequest(result, timeout);
            List<Message> requestParts = parts(packetName, payload);
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    ZLinkDispatchMessageKind.REQUEST,
                    packetName.orElse(null), channelName, null, null,
                    target.toString(), null, null, null));
            }
            try {
                ChannelRegistration registration = registrationsByName.get(channelName);
                List<String> reconnectEndpoints = registration == null
                    ? List.of()
                    : registration.routeManualEndpoints();
                submitRouteRequestWithRetry(
                    router,
                    target,
                    requestParts,
                    reply -> {
                        try {
                            completeRequestReply(reply, replyType, result);
                            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                                    ZLinkDispatchMessageKind.RESPONSE,
                                    packetName.orElse(null), channelName, null, null,
                                    target.toString(), null, null, null));
                            }
                        } catch (RuntimeException ex) {
                            result.completeExceptionally(ex);
                        } finally {
                            reply.parts().forEach(Message::close);
                        }
                    },
                    timeout,
                    reconnectEndpoints,
                    result);
            } finally {
                requestParts.forEach(Message::close);
            }
            return result;
        }
    }

    private final class RouteSpotSendCall implements ZLinkSendCall {
        private final String channelName;
        private final RoutingId targetNode;
        private final RoutingId targetSpot;
        private final Message payload;
        private final Optional<String> packetName;

        private RouteSpotSendCall(
            String channelName,
            RoutingId targetNode,
            RoutingId targetSpot,
            Message payload,
            Optional<String> packetName) {
            this.channelName = channelName;
            this.targetNode = targetNode;
            this.targetSpot = targetSpot;
            this.payload = payload;
            this.packetName = packetName;
        }

        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new RouteSpotSendCall(
                channelName,
                targetNode,
                targetSpot,
                payload,
                Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            List<Message> sendParts = parts(packetName, payload);
            try {
                return systems.zlink.framework.ZLinkSubmitStage.from(
                    sendToSpotViaRouterChannel(
                    channelName,
                    targetNode,
                    targetSpot,
                    sendParts));
            } finally {
                sendParts.forEach(Message::close);
            }
        }
    }

    private final class RouteSpotRequestCall implements ZLinkRequestCall {
        private final String channelName;
        private final RoutingId targetNode;
        private final RoutingId targetSpot;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private RouteSpotRequestCall(
            String channelName,
            RoutingId targetNode,
            RoutingId targetSpot,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.channelName = channelName;
            this.targetNode = targetNode;
            this.targetSpot = targetSpot;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new RouteSpotRequestCall(
                channelName,
                targetNode,
                targetSpot,
                payload,
                Optional.of(packetName),
                timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new RouteSpotRequestCall(
                channelName,
                targetNode,
                targetSpot,
                payload,
                packetName,
                timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            List<Message> requestParts = parts(packetName, payload);
            try {
                return requestToSpotViaRouterChannel(
                    channelName,
                    targetNode,
                    targetSpot,
                    requestParts,
                    timeout)
                    .thenApply(replyParts -> {
                        try {
                            return decodeRouteSpotReply(replyParts, replyType);
                        } finally {
                            replyParts.forEach(Message::close);
                        }
                    });
            } finally {
                requestParts.forEach(Message::close);
            }
        }

        private <TReply> TReply decodeRouteSpotReply(
            List<Message> replies,
            Class<TReply> replyType) {
            if (replies.isEmpty()) {
                try (Message emptyReply = Message.from(new byte[0])) {
                    return ZLinkMessagePayloads.deserialize(serializer, emptyReply, replyType);
                }
            }
            RuntimeException lastError = null;
            for (int index = replies.size() - 1; index >= 0; index--) {
                Message reply = replies.get(index);
                try {
                    return ZLinkMessagePayloads.deserialize(serializer, reply, replyType);
                } catch (RuntimeException directError) {
                    lastError = directError;
                    try {
                        JsonNode root = JSON.readTree(reply.toByteArray());
                        JsonNode ok = root.get("ok");
                        JsonNode response = root.get("response");
                        if (ok == null || !ok.asBoolean(false) || response == null) {
                            continue;
                        }
                        try (Message responseMessage = Message.from(
                            JSON.writeValueAsBytes(response))) {
                            return ZLinkMessagePayloads.deserialize(
                                serializer,
                                responseMessage,
                                replyType);
                        }
                    } catch (Exception envelopeError) {
                        directError.addSuppressed(envelopeError);
                    }
                }
            }
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PAYLOAD_DECODE_FAILED,
                "route mesh SPOT reply decode failed; first reply frame="
                    + replies.get(0).toUtf8String(),
                lastError);
        }
    }

    private void trackPendingRequest(CompletableFuture<?> result, Duration timeout) {
        pendingRequests.add(result);
        var timeoutTask = timeoutExecutor.schedule(
            () -> result.completeExceptionally(new TimeoutException("request timed out after " + timeout)),
            timeout.toNanos(),
            TimeUnit.NANOSECONDS);
        result.whenComplete((ignored, error) -> {
            timeoutTask.cancel(false);
            pendingRequests.remove(result);
        });
    }

    private void submitClientRequestWithRetry(
        ZLinkBackendDealerSocket client,
        List<Message> requestParts,
        Duration timeout,
        ZLinkBackendRequestCallback callback,
        CompletableFuture<?> result) {
        List<byte[]> requestPayloads = requestParts.stream()
            .map(Message::toByteArray)
            .toList();
        long timeoutNanos = timeout == null || timeout.isZero()
            ? defaultRequestTimeout.toNanos()
            : timeout.toNanos();
        long deadline = System.nanoTime() + timeoutNanos;
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                List<Message> attemptParts = requestPayloads.stream()
                    .map(Message::from)
                    .toList();
                try {
                    boolean submitted = client.request(
                        attemptParts,
                        callback,
                        SendFlags.DONT_WAIT,
                        timeout);
                    if (submitted) {
                        return;
                    }
                    if (System.nanoTime() >= deadline) {
                        result.completeExceptionally(new TimeoutException(
                            "channel request was not ready before timeout"));
                        return;
                    }
                    timeoutExecutor.schedule(this, 10, TimeUnit.MILLISECONDS);
                } catch (RuntimeException ex) {
                    if (isRetriableSubmit(ex) && System.nanoTime() < deadline) {
                        timeoutExecutor.schedule(this, 10, TimeUnit.MILLISECONDS);
                        return;
                    }
                    result.completeExceptionally(ex);
                } finally {
                    attemptParts.forEach(Message::close);
                }
            }
        }
        new Attempt().run();
    }

    private static boolean isRetriableSubmit(Throwable error) {
        Throwable current = error;
        while (current != null) {
            if (current instanceof ZlinkSubmitException submit) {
                int errno = submit.getNativeErrno();
                return submit.getResult() == SubmitResult.BACKPRESSURED
                    || submit.getResult() == SubmitResult.NOT_CONNECTED
                    || errno == ERRNO_EHOSTUNREACH
                    || errno == ERRNO_EHOSTUNREACH_WIN
                    || errno == ERRNO_ENETUNREACH
                    || errno == ERRNO_ENETUNREACH_WIN
                    || errno == ERRNO_ECONNREFUSED
                    || errno == ERRNO_ECONNREFUSED_WIN
                    || errno == ERRNO_ENOTCONN
                    || errno == ERRNO_ENOTCONN_WIN
                    || errno == ERRNO_EAGAIN
                    || errno == ERRNO_EWOULDBLOCK_WIN;
            }
            current = current.getCause();
        }
        return false;
    }

    private static List<Message> parts(Optional<String> packetName, Message payload) {
        if (packetName.isEmpty()) {
            return List.of(payload);
        }
        return List.of(Message.from(packetName.get().getBytes(StandardCharsets.UTF_8)), payload);
    }

    @FunctionalInterface
    public interface RouteInternalRequestHandler {
        CompletionStage<Message> handle(RoutingId sourceRoutingId, Message payload);
    }
}
