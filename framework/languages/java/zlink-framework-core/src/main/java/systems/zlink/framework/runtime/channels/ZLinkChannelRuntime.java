package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.runtime.backend.*;

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
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.SpotRouteBridgeEndpointCapabilities;
import systems.zlink.contracts.service.spot.SpotRouteBridgeEndpointOptions;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteSendContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowPhase;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorEvent;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
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

public final class ZLinkChannelRuntime implements ZLinkClient, ZLinkFanoutClient, ZLinkRouteClient, AutoCloseable {
    private static final String FRAMEWORK_ERROR_REPLY_MARKER = "ZLinkFrameworkError";

    private final ZLinkBackendContext context;
    private final Map<String, ZLinkBackendDealerSocket> clients = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> servers = new HashMap<>();
    private final Map<String, ZLinkBackendSpotRouteBridge> spotRouteBridges = new HashMap<>();
    private final Map<String, ZLinkBackendPublisherSocket> publishers = new HashMap<>();
    private final Map<String, ZLinkBackendSubscriberSocket> subscribers = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> routeRouters = new HashMap<>();
    private final Map<String, ChannelRegistration> registrationsByName = new HashMap<>();
    private final List<ZLinkBackendDealerSocket> manualClients = new ArrayList<>();
    private final List<ZLinkBackendRouterSocket> manualServers = new ArrayList<>();
    private final List<ZLinkBackendPublisherSocket> manualPublishers = new ArrayList<>();
    private final List<ZLinkBackendSubscriberSocket> manualSubscribers = new ArrayList<>();
    private final List<ZLinkBackendRouterSocket> manualRouteRouters = new ArrayList<>();
    private final List<ZLinkBackendDiscovery> discoveries = new ArrayList<>();
    private final Map<String, ZLinkBackendDiscovery> discoveriesByName = new HashMap<>();
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
    private final Map<String, ArrayDeque<CompletableFuture<List<Message>>>> pendingRawSpotRouteBridgeReplies =
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
    private final List<String> registryEndpoints;
    private final ZLinkDispatchErrorReporter dispatchErrors;
    private Supplier<ZLinkBackendSpotNode> spotRouteBridgeOwner;
    private final ExecutorService receiveExecutor = Executors.newCachedThreadPool(task -> {
        Thread thread = new Thread(task, "zlink-java-channel-runtime");
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
        this.serializer = Objects.requireNonNull(serializer, "serializer");
        this.handlerFactory = Objects.requireNonNull(handlerFactory, "handlerFactory");
        this.handlerExecutor = Objects.requireNonNull(registration.handlerExecutor(), "handlerExecutor");
        this.suspendHandlerInvokers = registration.suspendHandlerInvokers();
        this.filterTypes = List.copyOf(registration.filters());
        this.defaultRequestTimeout = registration.defaultRequestTimeout();
        this.backendFactory = backendFactory;
        this.adapterOptions = adapterOptions;
        this.registryEndpoints = registration.registryEndpoints();
        this.dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.dispatchOptions(),
            handlerFactory,
            this.handlerExecutor);
        this.context = backend.createContext();
        ZLinkScannedHandlerCatalog handlerCatalog =
            ZLinkHandlerScanner.scan(registration.handlerPackageMarkers());
        for (ChannelRegistration channel : registration.channels()) {
            registrationsByName.put(channel.name(), channel);
            ZLinkBackendDiscovery discovery = discoveryFor(backend, registration, channel);
            if (channel.kind() == ChannelKind.CLIENT_SERVER && channel.clientEnabled()) {
                ZLinkBackendDealerSocket dealer = backend.createDealerSocket(context);
                dealer.setChannelName(channel.name());
                if (discovery == null) {
                    for (String endpoint : channel.clientManualEndpoints()) {
                        dealer.connect(endpoint);
                    }
                    manualClients.add(dealer);
                } else {
                    dealer.attachDiscovery(discovery);
                }
                clients.put(channel.name(), dealer);
            }
            if (channel.kind() == ChannelKind.CLIENT_SERVER && !channel.serverBinds().isEmpty()) {
                ZLinkBackendRouterSocket router = backend.createRouterSocket(context);
                router.setChannelName(channel.name());
                if (channel.serverRoutingId() != null) {
                    router.setRoutingId(channel.serverRoutingId());
                }
                if (discovery != null) {
                    router.attachDiscovery(discovery);
                } else {
                    manualServers.add(router);
                }
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
            if (channel.kind() == ChannelKind.FANOUT && channel.publisherEnabled()) {
                ZLinkBackendPublisherSocket publisher = backend.createPublisherSocket(context);
                publisher.setChannelName(channel.name());
                if (discovery != null) {
                    publisher.attachDiscovery(discovery);
                } else {
                    manualPublishers.add(publisher);
                }
                for (String endpoint : channel.publisherBinds()) {
                    publisher.bind(endpoint);
                }
                publishers.put(channel.name(), publisher);
            }
            if (channel.kind() == ChannelKind.FANOUT && channel.subscriberEnabled()) {
                ZLinkBackendSubscriberSocket subscriber = backend.createSubscriberSocket(context);
                subscriber.setChannelName(channel.name());
                if (discovery != null) {
                    subscriber.attachDiscovery(discovery);
                } else {
                    for (String endpoint : channel.subscriberManualEndpoints()) {
                        subscriber.connect(endpoint);
                    }
                    manualSubscribers.add(subscriber);
                }
                subscriber.setSubscription("");
                subscribers.put(channel.name(), subscriber);
                publishHandlers.put(channel.name(), publishHandlersByPacket(channel, handlerCatalog));
                publishDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
                startSubscribeLoop(channel.name(), subscriber);
            }
            if (channel.kind() == ChannelKind.ROUTE_MESH) {
                ZLinkBackendRouterSocket router = backend.createRouterSocket(context);
                router.setChannelName(channel.name());
                if (channel.routeRoutingId() != null) {
                    router.setRoutingId(channel.routeRoutingId());
                }
                if (discovery != null) {
                    router.attachDiscovery(discovery);
                } else {
                    for (String endpoint : channel.routeManualEndpoints()) {
                        router.connect(endpoint);
                    }
                    manualRouteRouters.add(router);
                }
                for (String endpoint : channel.routeBinds()) {
                    router.bind(endpoint);
                }
                routeRouters.put(channel.name(), router);
                routeSendHandlers.put(channel.name(), routeSendHandlersByPacket(channel, handlerCatalog));
                routeRequestHandlers.put(channel.name(), routeHandlersByPacket(channel, handlerCatalog));
                routeSendDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
                routeRequestDispatchQueues.put(channel.name(), new ZLinkAsyncSerialQueue());
                startRouteLoop(channel.name(), router);
            }
        }
    }

    private ZLinkBackendDiscovery discoveryFor(
        ZLinkChannelBackendAdapter backend,
        ZLinkFrameworkRegistration registration,
        ChannelRegistration channel) {
        if (!registration.discoveryEnabled()
            || (channel.kind() != ChannelKind.CLIENT_SERVER
                && channel.kind() != ChannelKind.FANOUT
                && channel.kind() != ChannelKind.ROUTE_MESH)) {
            return null;
        }
        if (channel.kind() == ChannelKind.CLIENT_SERVER
            && channel.clientEnabled()
            && !channel.clientManualEndpoints().isEmpty()) {
            return null;
        }
        if (channel.kind() == ChannelKind.FANOUT
            && channel.subscriberEnabled()
            && !channel.subscriberManualEndpoints().isEmpty()) {
            return null;
        }
        if (channel.kind() == ChannelKind.ROUTE_MESH
            && !channel.routeManualEndpoints().isEmpty()) {
            return null;
        }
        ZLinkBackendDiscovery discovery = backend.createDiscovery(
            context,
            autoConnectType(channel),
            channel.name());
        for (String endpoint : registration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        discoveries.add(discovery);
        discoveriesByName.put(channel.name(), discovery);
        return discovery;
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
    public ZLinkRequestCall requestToChannel(String channelName, Object message) {
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
    public ZLinkSendCall sendTo(String channelName, RoutingId target, Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new RouteSendCall(
            requireRouteRouter(channelName),
            target,
            encoded.payload(),
            Optional.of(encoded.packetName()));
    }

    @Override
    public ZLinkRequestCall requestTo(String channelName, RoutingId target, Object message) {
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

    public void registerSpotRouteBridgeOwner(
        Supplier<ZLinkBackendSpotNode> owner) {
        this.spotRouteBridgeOwner = Objects.requireNonNull(owner, "owner");
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
            router,
            new SpotRouteBridgeEndpointOptions()
                .capabilities(SpotRouteBridgeEndpointCapabilities.ROUTE_WITH_CHANNEL_INBOUND));
        spotRouteBridges.put(channelName, bridge);
        return true;
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

    public CompletionStage<Void> sendToSpotViaEgressChannel(
        String localEgressChannelName,
        RoutingId targetSpotRid,
        List<Message> spotParts) {
        return sendToSpotViaEgressChannel(
            localEgressChannelName,
            null,
            targetSpotRid,
            spotParts);
    }

    public CompletionStage<Void> sendToSpotViaEgressChannel(
        String localEgressChannelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts) {
        ChannelRegistration registration = requireSpotRouteEgress(localEgressChannelName);
        try {
            ZLinkBackendSpotRouteBridge bridge = requireSpotRouteBridge(localEgressChannelName);
            RoutingId targetPeerRid = resolveSpotRouteBridgeTargetNode(
                registration,
                localEgressChannelName,
                targetNodeRid);
            List<Message> bridgeParts = copyMessages(spotParts);
            return CompletableFuture.runAsync(() -> {
                try {
                    if (targetPeerRid != null) {
                        bridge.setTargetNode(localEgressChannelName, targetPeerRid);
                    }
                    bridge.send(localEgressChannelName, targetSpotRid, bridgeParts, SendFlags.NONE);
                } finally {
                    bridgeParts.forEach(Message::close);
                }
            });
        } catch (RuntimeException ex) {
            CompletableFuture<Void> result = new CompletableFuture<>();
            result.completeExceptionally(ex);
            return result;
        }
    }

    public CompletionStage<Void> sendToSpotViaRouterChannel(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts) {
        ChannelRegistration registration = registrationsByName.get(routerChannelId);
        if (registration == null || registration.kind() != ChannelKind.ROUTE_MESH) {
            throw new ZLinkConfigurationException(
                "route mesh channel is not configured: " + routerChannelId);
        }
        try {
            ZLinkBackendSpotRouteBridge bridge = requireSpotRouteBridge(routerChannelId);
            List<Message> bridgeParts = copyMessages(spotParts);
            return CompletableFuture.runAsync(() -> {
                try {
                    bridge.setTargetNode(routerChannelId, targetNodeRid);
                    bridge.send(routerChannelId, targetSpotRid, bridgeParts, SendFlags.NONE);
                } finally {
                    bridgeParts.forEach(Message::close);
                }
            });
        } catch (RuntimeException ex) {
            CompletableFuture<Void> result = new CompletableFuture<>();
            result.completeExceptionally(ex);
            return result;
        }
    }

    public CompletionStage<List<Message>> requestToSpotViaEgressChannel(
        String localEgressChannelName,
        RoutingId targetSpotRid,
        List<Message> spotParts,
        Duration timeout) {
        return requestToSpotViaEgressChannel(
            localEgressChannelName,
            null,
            targetSpotRid,
            spotParts,
            timeout);
    }

    public CompletionStage<List<Message>> requestToSpotViaEgressChannel(
        String localEgressChannelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts,
        Duration timeout) {
        ChannelRegistration registration = requireSpotRouteEgress(localEgressChannelName);
        CompletableFuture<List<Message>> result = new CompletableFuture<>();
        trackPendingRequest(result, timeout);
        try {
            ZLinkBackendSpotRouteBridge bridge = requireSpotRouteBridge(localEgressChannelName);
            RoutingId targetPeerRid = resolveSpotRouteBridgeTargetNode(
                registration,
                localEgressChannelName,
                targetNodeRid);
            List<Message> bridgeParts = copyMessages(spotParts);
            if (targetPeerRid != null) {
                bridge.setTargetNode(localEgressChannelName, targetPeerRid);
            }
            try {
                bridge.request(
                    localEgressChannelName,
                    targetSpotRid,
                    bridgeParts,
                    reply -> {
                        try {
                            result.complete(copyMessages(reply.parts()));
                        } catch (RuntimeException ex) {
                            result.completeExceptionally(ex);
                        } finally {
                            reply.parts().forEach(Message::close);
                        }
                    }, SendFlags.NONE, timeout);
            } finally {
                bridgeParts.forEach(Message::close);
            }
            return result;
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
            return result;
        }
    }

    public CompletionStage<List<Message>> requestToSpotViaRouterChannel(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts,
        Duration timeout) {
        ChannelRegistration registration = registrationsByName.get(routerChannelId);
        if (registration == null || registration.kind() != ChannelKind.ROUTE_MESH) {
            throw new ZLinkConfigurationException(
                "route mesh channel is not configured: " + routerChannelId);
        }
        CompletableFuture<List<Message>> result = new CompletableFuture<>();
        trackPendingRequest(result, timeout);
        try {
            ZLinkBackendSpotRouteBridge bridge = requireSpotRouteBridge(routerChannelId);
            List<Message> bridgeParts = copyMessages(spotParts);
            bridge.setTargetNode(routerChannelId, targetNodeRid);
            enqueueRawSpotRouteBridgeReply(routerChannelId, result);
            try {
                boolean submitted = bridge.request(
                    routerChannelId,
                    targetSpotRid,
                    bridgeParts,
                    reply -> {
                        try {
                            result.complete(copyMessages(reply.parts()));
                        } catch (RuntimeException ex) {
                            result.completeExceptionally(ex);
                        } finally {
                            reply.parts().forEach(Message::close);
                        }
                    },
                    SendFlags.NONE,
                    timeout);
                if (!submitted) {
                    result.completeExceptionally(new ZLinkConfigurationException(
                        "route mesh channel is not ready for SPOT request: " + routerChannelId));
                }
            } finally {
                bridgeParts.forEach(Message::close);
            }
            return result;
        } catch (RuntimeException ex) {
            removeRawSpotRouteBridgeReply(routerChannelId, result);
            result.completeExceptionally(ex);
            return result;
        }
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
        running = false;
        discoveries.forEach(ZLinkBackendDiscovery::close);
        manualClients.forEach(ZLinkBackendDealerSocket::close);
        manualServers.forEach(ZLinkBackendRouterSocket::close);
        manualPublishers.forEach(ZLinkBackendPublisherSocket::close);
        manualSubscribers.forEach(ZLinkBackendSubscriberSocket::close);
        manualRouteRouters.forEach(ZLinkBackendRouterSocket::close);
        spotRouteBridges.values().forEach(ZLinkBackendSpotRouteBridge::close);
        for (CompletableFuture<?> pending : pendingRequests) {
            pending.completeExceptionally(new ZLinkConfigurationException("channel runtime is closed"));
        }
        receiveExecutor.shutdownNow();
        timeoutExecutor.shutdownNow();
        awaitTerminated(receiveExecutor);
        awaitTerminated(timeoutExecutor);
        context.close();
    }

    private static void awaitTerminated(java.util.concurrent.ExecutorService executor) {
        try {
            executor.awaitTermination(1, TimeUnit.SECONDS);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
    }

    private static ZLinkBackendAutoConnectType autoConnectType(ChannelRegistration channel) {
        return switch (channel.kind()) {
            case CLIENT_SERVER -> ZLinkBackendAutoConnectType.CLIENT_SERVER;
            case FANOUT -> ZLinkBackendAutoConnectType.FANOUT;
            case ROUTE_MESH -> ZLinkBackendAutoConnectType.ROUTE_MESH;
            case DEALER_MESH -> ZLinkBackendAutoConnectType.DEALER_MESH;
        };
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
                requireRouteRouter(channelName),
                new SpotRouteBridgeEndpointOptions()
                    .capabilities(SpotRouteBridgeEndpointCapabilities.ROUTE_WITH_CHANNEL_INBOUND));
        } else {
            bridge.attachDealerChannel(
                channelName,
                requireClient(channelName),
                new SpotRouteBridgeEndpointOptions()
                    .capabilities(SpotRouteBridgeEndpointCapabilities.ROUTE_ONLY));
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

    private ChannelRegistration requireSpotRouteEgress(String channelName) {
        ChannelRegistration registration = registrationsByName.get(channelName);
        if (registration == null || registration.spotRouteEgressTarget() == null) {
            throw new ZLinkConfigurationException(
                "routed SPOT egress channel is not configured: " + channelName);
        }
        return registration;
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

    private RoutingId resolveSpotRouteBridgeTargetNode(
        ChannelRegistration registration,
        String localEgressChannelName,
        RoutingId targetNodeRid) {
        if (registration.kind() != ChannelKind.ROUTE_MESH) {
            return null;
        }
        return targetNodeRid == null
            ? resolveRouteEgressPeerRid(
                localEgressChannelName,
                registration.spotRouteEgressTarget())
            : targetNodeRid;
    }

    private RoutingId resolveRouteEgressPeerRid(
        String localEgressChannelName,
        String targetSpotNodeChannelName) {
        ChannelRegistration target = registrationsByName.get(targetSpotNodeChannelName);
        if (target != null
            && target.kind() == ChannelKind.ROUTE_MESH
            && target.routeRoutingId() != null) {
            return target.routeRoutingId();
        }
        RoutingId discoveryRid = resolveRouteEgressPeerRidFromDiscovery(
            localEgressChannelName,
            targetSpotNodeChannelName);
        if (discoveryRid != null) {
            return discoveryRid;
        }
        RoutingId registryRid = resolveRouteEgressPeerRidFromRegistry(targetSpotNodeChannelName);
        if (registryRid != null) {
            return registryRid;
        }
        throw new ZLinkConfigurationException(
            "routed SPOT route mesh egress channel '"
                + localEgressChannelName
                + "' cannot find a target route peer routing id for target SPOT node channel '"
                + targetSpotNodeChannelName
                + "'");
    }

    private RoutingId resolveRouteEgressPeerRidFromDiscovery(
        String localEgressChannelName,
        String targetSpotNodeChannelName) {
        ZLinkBackendDiscovery discovery = discoveriesByName.get(localEgressChannelName);
        if (discovery == null) {
            return null;
        }
        RoutingId matched = null;
        boolean matchedWithoutRoutingId = false;
        for (ZLinkBackendRegistryMemberPeerEntry peer : discovery.memberPeers()) {
            if (!targetSpotNodeChannelName.equals(peer.channelName())) {
                continue;
            }
            if (peer.serviceRole() != ServiceRole.ROUTER) {
                continue;
            }
            if (peer.routingId() == null) {
                matchedWithoutRoutingId = true;
                continue;
            }
            if (matched != null && !matched.equals(peer.routingId())) {
                throw new ZLinkConfigurationException(
                    "routed SPOT route mesh egress found multiple discovery route peers for target channel: "
                        + targetSpotNodeChannelName);
            }
            matched = peer.routingId();
        }
        if (matched == null && matchedWithoutRoutingId) {
            throw new ZLinkConfigurationException(
                "routed SPOT route mesh egress found discovery target channel without routing id: "
                    + targetSpotNodeChannelName);
        }
        return matched;
    }

    private RoutingId resolveRouteEgressPeerRidFromRegistry(String targetSpotNodeChannelName) {
        if (backendFactory == null || adapterOptions == null || registryEndpoints.isEmpty()) {
            return null;
        }
        ZLinkRegistryBackendAdapter registryAdapter =
            backendFactory.createRegistryAdapter(adapterOptions);
        RoutingId matched = null;
        boolean matchedWithoutRoutingId = false;
        for (String endpoint : registryEndpoints) {
            ZLinkBackendRegistryQueryClient client =
                registryAdapter.createRegistryQueryClient(context);
            try {
                client.connect(endpoint);
                for (ZLinkBackendRegistryTopologyEntry entry :
                    client.topology(new ZLinkBackendRegistryQueryFilter(
                        Optional.empty(),
                        Optional.empty(),
                        Optional.of(ServiceRole.ROUTER),
                        Optional.of(targetSpotNodeChannelName),
                        Optional.empty(),
                        Optional.empty(),
                        Optional.empty()))) {
                    if (!targetSpotNodeChannelName.equals(entry.channelName())) {
                        continue;
                    }
                    if (entry.serviceRole() != ServiceRole.ROUTER) {
                        continue;
                    }
                    if (entry.routingId() == null) {
                        matchedWithoutRoutingId = true;
                        continue;
                    }
                    if (matched != null && !matched.equals(entry.routingId())) {
                        throw new ZLinkConfigurationException(
                            "routed SPOT route mesh egress found multiple route peers for target channel: "
                                + targetSpotNodeChannelName);
                    }
                    matched = entry.routingId();
                }
            } finally {
                client.close();
            }
        }
        if (matched == null && matchedWithoutRoutingId) {
            throw new ZLinkConfigurationException(
                "routed SPOT route mesh egress found target channel without routing id: "
                    + targetSpotNodeChannelName);
        }
        return matched;
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
            ParsedPacket packet = parsePacket(received.parts());
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
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.RECEIVED,
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
                            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.REPLIED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowPhase.REPLIED,
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
        ZLinkBackendSpotRouteBridge bridge = spotRouteBridges.get(channelName);
        if (bridge == null || received.routingId().isEmpty()) {
            return false;
        }
        return bridge.handleRouterReceived(
            channelName,
            received.routingId().get(),
            received.requestSeq().orElse(0L),
            received.parts());
    }

    private void enqueueRawSpotRouteBridgeReply(
        String channelName,
        CompletableFuture<List<Message>> pending) {
        synchronized (pendingRawSpotRouteBridgeReplies) {
            pendingRawSpotRouteBridgeReplies
                .computeIfAbsent(channelName, ignored -> new ArrayDeque<>())
                .addLast(pending);
        }
        pending.whenComplete((ignored, error) ->
            removeRawSpotRouteBridgeReply(channelName, pending));
    }

    private void removeRawSpotRouteBridgeReply(
        String channelName,
        CompletableFuture<List<Message>> pending) {
        synchronized (pendingRawSpotRouteBridgeReplies) {
            ArrayDeque<CompletableFuture<List<Message>>> queue =
                pendingRawSpotRouteBridgeReplies.get(channelName);
            if (queue == null) {
                return;
            }
            queue.remove(pending);
            if (queue.isEmpty()) {
                pendingRawSpotRouteBridgeReplies.remove(channelName);
            }
        }
    }

    private boolean tryCompleteRawSpotRouteBridgeReply(
        String channelName,
        ZLinkBackendReceived received) {
        if (!looksLikeRawSpotRouteBridgeReply(received.parts())) {
            return false;
        }
        CompletableFuture<List<Message>> pending;
        synchronized (pendingRawSpotRouteBridgeReplies) {
            ArrayDeque<CompletableFuture<List<Message>>> queue =
                pendingRawSpotRouteBridgeReplies.get(channelName);
            if (queue == null || queue.isEmpty()) {
                return false;
            }
            pending = queue.peekFirst();
        }
        return pending.complete(copyMessages(received.parts()));
    }

    private static boolean looksLikeRawSpotRouteBridgeReply(List<Message> parts) {
        if (parts.isEmpty()) {
            return false;
        }
        String text = parts.get(0).toUtf8String().trim();
        return text.startsWith("{")
            && text.endsWith("}")
            && (text.contains("\"ok\"")
                || text.contains("\"response\"")
                || text.contains("\"error\"")
                || text.contains("\"actorPacketTarget\""));
    }

    private void startRouteLoop(String channelName, ZLinkBackendRouterSocket router) {
        receiveExecutor.submit(() -> {
            while (running) {
                ZLinkBackendReceived received = router.recv(ZLinkBackendRecvMode.DONT_WAIT);
                if (received != null) {
                    dispatchRouteRequest(channelName, router, received);
                } else {
                    Thread.onSpinWait();
                }
            }
        });
    }

    private void dispatchRouteRequest(
        String channelName,
        ZLinkBackendRouterSocket router,
        ZLinkBackendReceived received) {
        try {
            if (dispatchSpotRouteBridgePacket(channelName, received)) {
                return;
            }
            if (tryCompleteRawSpotRouteBridgeReply(channelName, received)) {
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
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
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.RECEIVED,
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
                            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.REPLIED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowPhase.REPLIED,
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
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.RECEIVED,
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
                        } else if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.DISPATCHED)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowPhase.DISPATCHED,
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
        if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowPhase.RECEIVED,
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
                    } else if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.DISPATCHED)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowPhase.DISPATCHED,
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
        if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowPhase.RECEIVED,
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
                    } else if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.DISPATCHED)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowPhase.DISPATCHED,
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
        dispatchErrors.report(new ZLinkMessageDispatchErrorEvent(
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
            cause));
    }

    private static Throwable unwrapCompletion(Throwable error) {
        if (error instanceof CompletionException && error.getCause() != null) {
            return error.getCause();
        }
        return error;
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
        try {
            handlerExecutor.execute(() -> {
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
            message = serializer.deserialize(payload, registration.messageType());
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
            request = serializer.deserialize(payload, registration.requestType());
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
                .thenApply(serializer::serialize);
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
            message = serializer.deserialize(payload, registration.messageType());
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
            message = serializer.deserialize(payload, registration.messageType());
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
            request = serializer.deserialize(payload, registration.requestType());
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
                    .thenApply(serializer::serialize);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {request, context}, suspendHandlerInvokers)
                .thenApply(serializer::serialize);
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
        public CompletionStage<Void> submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.SENT,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.PUBLISH,
                    packetName.orElse(null), null, topic, null, null, null, null, null));
            }
            return CompletableFuture.runAsync(() -> {
                List<Message> publishParts = parts(packetName, payload);
                try {
                    publisher.publish(topic, publishParts, SendFlags.NONE);
                } finally {
                    publishParts.forEach(Message::close);
                }
            });
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
        public CompletionStage<Void> submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.SENT,
                    ZLinkDispatchErrorSurface.CHANNEL,
                    ZLinkDispatchMessageKind.SEND,
                    packetName.orElse(null), null, null, null, null, null, null, null));
            }
            return CompletableFuture.runAsync(() -> {
                try {
                    client.send(parts(packetName, payload), SendFlags.NONE);
                } finally {
                    payload.close();
                }
            });
        }
    }

    private final class RequestCall implements ZLinkRequestCall {
        private final ZLinkBackendDealerSocket client;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private RequestCall(
            ZLinkBackendDealerSocket client,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.client = client;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new RequestCall(client, payload, Optional.of(packetName), timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new RequestCall(client, payload, packetName, timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            trackPendingRequest(result, timeout);
            List<Message> requestParts = parts(packetName, payload);
            result.whenComplete((ignored, error) -> requestParts.forEach(Message::close));
            String reqPacket = packetName.orElse(null);
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.SENT,
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
                        if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.REPLY_RECEIVED)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowPhase.REPLY_RECEIVED,
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
    }

    private <TReply> void completeRequestReply(
        ZLinkBackendReceived reply,
        Class<TReply> replyType,
        CompletableFuture<TReply> result) {
        if (reply.result() != ZLinkBackendRequestResult.OK) {
            result.completeExceptionally(new ZLinkFrameworkException(
                "channel request failed: " + reply.result()));
            return;
        }
        if (reply.parts().size() >= 2
            && FRAMEWORK_ERROR_REPLY_MARKER.equals(reply.parts().get(0).toUtf8String())) {
            String message = reply.parts().get(1).toUtf8String();
            result.completeExceptionally(new ZLinkFrameworkException(
                message));
            return;
        }
        Message emptyReply = null;
        Message firstReply = reply.parts().isEmpty()
            ? (emptyReply = Message.from(new byte[0]))
            : reply.parts().get(0);
        try {
            result.complete(serializer.deserialize(firstReply, replyType));
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
        public CompletionStage<Void> submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.SENT,
                    ZLinkDispatchErrorSurface.ROUTE_MESH_CHANNEL,
                    ZLinkDispatchMessageKind.SEND,
                    packetName.orElse(null), null, null, null, target.toString(), null, null, null));
            }
            return CompletableFuture.runAsync(() -> {
                List<Message> sendParts = parts(packetName, payload);
                try {
                    router.send(target, sendParts, SendFlags.NONE);
                } finally {
                    sendParts.forEach(Message::close);
                }
            });
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
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.SENT,
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
                            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.REPLY_RECEIVED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowPhase.REPLY_RECEIVED,
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
                try {
                    boolean submitted = client.request(
                        requestParts,
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
                    result.completeExceptionally(ex);
                }
            }
        }
        new Attempt().run();
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
