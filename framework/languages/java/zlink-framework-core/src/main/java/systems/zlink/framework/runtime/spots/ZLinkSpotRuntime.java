package systems.zlink.framework.runtime.spots;

import systems.zlink.framework.runtime.backend.*;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ConnectResult;
import systems.zlink.contracts.errors.ZlinkConnectException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkSpotActorDisconnected;
import systems.zlink.framework.handlers.ZLinkSpotActorJoin;
import systems.zlink.framework.handlers.ZLinkSpotActorLeft;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.channels.ChannelKind;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorChangeKind;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotHandlerRegistry;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

public final class ZLinkSpotRuntime implements ZLinkSpotManager, ZLinkChannelRuntime.SpotRelayIngress, AutoCloseable {
    private static final long ROUTER_ENDPOINT_ROUTE_KIND = 0x5a4c5245L;
    private static final int ROUTER_PEER_CONVERGENCE_SNAPSHOTS = 10;
    private static final CancellationToken NONE_CANCELLATION = () -> false;
    private static final ScheduledThreadPoolExecutor ACTOR_SESSION_REPLY_RETRY_EXECUTOR =
        new ScheduledThreadPoolExecutor(1, task -> {
            Thread thread = new Thread(task, "zlink-actor-session-reply-retry");
            thread.setDaemon(true);
            return thread;
        });

    static {
        ACTOR_SESSION_REPLY_RETRY_EXECUTOR.setRemoveOnCancelPolicy(true);
        ACTOR_SESSION_REPLY_RETRY_EXECUTOR.prestartCoreThread();
    }

    private final ZLinkBackendContext context;
    private final List<ZLinkBackendSpotNode> nodes = new ArrayList<>();
    private final List<ZLinkBackendDealerSocket> attachedChannelDealers = new ArrayList<>();
    private final List<ZLinkBackendDiscovery> attachedChannelDiscoveries = new ArrayList<>();
    private final Map<String, ZLinkBackendSpotNode> nodesByName = new HashMap<>();
    private final Map<String, ZLinkBackendDiscovery> spotDiscoveriesByMesh = new HashMap<>();
    private final List<SpotDiscoveryBinding> spotDiscoveryBindings = new ArrayList<>();
    private final Set<String> discoveredPubSubPeers = new HashSet<>();
    private final Set<String> discoveredRouterPeers = new HashSet<>();
    private final Map<String, Integer> discoveredRouterPeerSightings = new HashMap<>();
    private final Set<RoutingId> connectedRouterPeerRids = ConcurrentHashMap.newKeySet();
    private final Map<String, ZLinkBackendSpotNode> publisherNodesByChannel = new HashMap<>();
    private final Map<String, ZLinkBackendSpot> publisherSpotsByChannel = new HashMap<>();
    private final Set<Class<? extends ZLinkSpot>> registeredSpotTypes = new HashSet<>();
    private final Map<RoutingId, SpotActivation> spots = new HashMap<>();
    private final List<EntrySpotActivation> entrySpots = new ArrayList<>();
    private final ZLinkBackendSpotNode primaryNode;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final Map<String, SpotActorJoinHandlerRegistration> actorJoinHandlers;
    private final Map<String, SpotActorPacketHandlerRegistration> actorPacketHandlers;
    private final List<SpotActorLifecycleHandlerRegistration> actorJoinedHandlers;
    private final List<SpotActorLifecycleHandlerRegistration> actorLeftHandlers;
    private final List<SpotActorLifecycleHandlerRegistration> actorDisconnectedHandlers;
    private final Duration defaultTimeout;
    private final ZLinkChannelRuntime channels;
    private ZLinkActorRuntime actorRuntime;
    private final Map<String, SpotNodeRegistration> acceptedRouteNodesByChannel = new HashMap<>();
    private final List<String> egressChannels = new ArrayList<>();
    private final List<String> routeMeshChannels = new ArrayList<>();
    private final ThreadLocal<DefaultSpotOutbound> currentOutbound = new ThreadLocal<>();
    private volatile boolean closing;
    private final ScheduledExecutorService timerExecutor = Executors.newScheduledThreadPool(1, task -> {
        Thread thread = new Thread(task, "zlink-java-spot-timer");
        thread.setDaemon(true);
        return thread;
    });

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration) {
        this(backendFactory, adapterOptions, registration, null);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels) {
        this(backendFactory, adapterOptions, registration, channels, ZLinkHandlerFactory.reflection());
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkHandlerFactory handlerFactory) {
        this(
            backendFactory,
            adapterOptions,
            registration,
            channels,
            new ZLinkStringMessageSerializer(),
            handlerFactory);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        if (registration.spotNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one SpotNode is required");
        }
        this.channels = channels;
        this.serializer = java.util.Objects.requireNonNull(serializer, "serializer");
        this.handlerFactory = handlerFactory;
        ZLinkScannedHandlerCatalog handlerCatalog =
            ZLinkHandlerScanner.scan(registration.handlerPackageMarkers());
        this.actorJoinHandlers = new HashMap<>(actorJoinHandlersByPacket(handlerCatalog));
        this.actorPacketHandlers = new HashMap<>(actorPacketHandlersByPacket(handlerCatalog));
        this.actorJoinedHandlers = new ArrayList<>(actorJoinedHandlers(handlerCatalog));
        this.actorLeftHandlers = new ArrayList<>(actorLeftHandlers(handlerCatalog));
        this.actorDisconnectedHandlers = new ArrayList<>(actorDisconnectedHandlers(handlerCatalog));
        prepareHandlerSerializerTypes();
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkSpotBackendAdapter spotAdapter =
            backendFactory.createSpotAdapter(adapterOptions);
        this.context = channelAdapter.createContext();
        this.defaultTimeout = registration.defaultTimeout();
        for (SpotNodeRegistration nodeRegistration : registration.spotNodes()) {
            ZLinkBackendSpotNode node =
                spotAdapter.createSpotNode(context, resolveSpotNodeMode(nodeRegistration));
            if (nodeRegistration.nodeRoutingId() != null) {
                node.setRoutingId(nodeRegistration.nodeRoutingId());
            }
            if (nodeRegistration.entrySpotRoutingId() != null
                || !nodeRegistration.entrySpots().isEmpty()) {
                ZLinkBackendSpot entryBackendSpot = node.entrySpot();
                if (nodeRegistration.entrySpotRoutingId() != null) {
                    entryBackendSpot.setRoutingId(nodeRegistration.entrySpotRoutingId());
                }
                for (Class<? extends ZLinkEntrySpot> entrySpotType : nodeRegistration.entrySpots()) {
                    entrySpots.add(activateEntrySpot(
                        node.routingId(),
                        entryBackendSpot,
                        entrySpotType));
                }
            }
            if (nodeRegistration.routerBind() != null) {
                node.setRouterBind(nodeRegistration.routerBind());
            }
            if (nodeRegistration.pubBind() != null) {
                node.setPubBind(nodeRegistration.pubBind());
            }
            attachSpotMeshDiscovery(
                channelAdapter,
                registration,
                nodeRegistration,
                node);
            for (String endpoint : nodeRegistration.routerManualConnections()) {
                node.connectPeer(endpoint);
            }
            for (String endpoint : nodeRegistration.pubSubManualConnections()) {
                node.connectPeer(endpoint);
            }
            for (SpotChannelClientRegistration attached :
                nodeRegistration.attachedChannelClients().values()) {
                attachChannelClient(
                    channelAdapter,
                    registration,
                    node,
                    attached);
            }
            for (SpotRouteChannelAcceptanceRegistration acceptance :
                nodeRegistration.acceptedSpotRouteChannels().values()) {
                if (acceptedRouteNodesByChannel.putIfAbsent(
                    acceptance.channelName(),
                    nodeRegistration) != null) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT channel is accepted by multiple SPOT nodes: "
                            + acceptance.channelName());
                }
                attachAcceptedSpotRouteChannel(
                    channelAdapter,
                    registration,
                    node,
                    acceptance);
            }
            nodes.add(node);
            nodesByName.put(nodeRegistration.nodeName(), node);
            registeredSpotTypes.addAll(nodeRegistration.spotFactories());
            for (SpotPublisherClientRegistration publisher :
                nodeRegistration.attachedSpotPublisherClients().values()) {
                publisherNodesByChannel.put(publisher.channelName(), node);
                for (String endpoint : publisher.manualConnections()) {
                    node.connectPeer(endpoint);
                }
            }
        }
        for (var channel : registration.channels()) {
            if (channel.kind() == ChannelKind.ROUTE_MESH) {
                routeMeshChannels.add(channel.name());
            }
            if (channel.spotRouteEgressTarget() != null) {
                egressChannels.add(channel.name());
            }
        }
        this.primaryNode = nodes.get(0);
        startSpotDiscoveryReconciliation();
    }

    private static ZLinkBackendSpotNodeMode resolveSpotNodeMode(
        SpotNodeRegistration registration) {
        if (registration.routerEnabled() && registration.pubSubEnabled()) {
            return ZLinkBackendSpotNodeMode.ALL;
        }
        if (registration.routerEnabled()) {
            return ZLinkBackendSpotNodeMode.ROUTED;
        }
        if (registration.pubSubEnabled()) {
            return ZLinkBackendSpotNodeMode.PUBSUB;
        }
        throw new ZLinkConfigurationException(
            "spot node must enable router or pub/sub capability: "
                + registration.nodeName());
    }

    private static Map<String, SpotActorJoinHandlerRegistration> actorJoinHandlersByPacket(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, SpotActorJoinHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (handler.surface() != ZLinkScannedHandlerSurface.SPOT
                || handler.kind() != ZLinkScannedHandlerKind.ACTOR_JOIN) {
                continue;
            }
            ActorMessageShape shape = actorJoinHandlerShape(
                handler.handlerType(),
                handler.handlerMethod());
            SpotActorJoinHandlerRegistration registration =
                new SpotActorJoinHandlerRegistration(
                    handler.handlerType(),
                    handler.handlerMethod(),
                    shape.actorType(),
                    handler.messageType(),
                    handler.replyType(),
                    handler.packetName());
            if (handlers.putIfAbsent(handler.packetName(), registration) != null) {
                throw new ZLinkConfigurationException(
                    "duplicate Spot actor join handler packet: " + handler.packetName());
            }
        }
        return Map.copyOf(handlers);
    }

    private static Map<String, SpotActorPacketHandlerRegistration> actorPacketHandlersByPacket(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, SpotActorPacketHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (handler.surface() != ZLinkScannedHandlerSurface.SPOT
                || (handler.kind() != ZLinkScannedHandlerKind.ACTOR_SEND
                    && handler.kind() != ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
                continue;
            }
            ActorMessageShape shape = actorPacketHandlerShape(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
                    ? ZLinkSpotActorRequestContext.class
                    : ZLinkSpotActorSendContext.class);
            SpotActorPacketHandlerRegistration registration =
                new SpotActorPacketHandlerRegistration(
                    handler.handlerType(),
                    handler.handlerMethod(),
                    shape.actorType(),
                    handler.messageType(),
                    handler.replyType(),
                    handler.packetName(),
                    handler.kind());
            if (handlers.putIfAbsent(handler.packetName(), registration) != null) {
                throw new ZLinkConfigurationException(
                    "duplicate Spot actor packet handler packet: " + handler.packetName());
            }
        }
        return Map.copyOf(handlers);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorJoinedHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        return actorLifecycleHandlers(handlerCatalog, ZLinkScannedHandlerKind.ACTOR_JOINED);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorLeftHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        return actorLifecycleHandlers(handlerCatalog, ZLinkScannedHandlerKind.ACTOR_LEFT);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorDisconnectedHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        return actorLifecycleHandlers(handlerCatalog, ZLinkScannedHandlerKind.ACTOR_DISCONNECTED);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorLifecycleHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog,
        ZLinkScannedHandlerKind kind) {
        List<SpotActorLifecycleHandlerRegistration> handlers = new ArrayList<>();
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (handler.surface() != ZLinkScannedHandlerSurface.SPOT
                || handler.kind() != kind) {
                continue;
            }
            ActorLifecycleShape shape = kind == ZLinkScannedHandlerKind.ACTOR_DISCONNECTED
                ? actorDisconnectedHandlerShape(handler.handlerType(), handler.handlerMethod())
                : actorLifecycleHandlerShape(handler.handlerType(), handler.handlerMethod());
            handlers.add(new SpotActorLifecycleHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                shape.spotType(),
                shape.actorType(),
                handler.kind()));
        }
        return List.copyOf(handlers);
    }

    private void prepareHandlerSerializerTypes() {
        for (SpotActorJoinHandlerRegistration registration : actorJoinHandlers.values()) {
            serializer.prepare(registration.requestType());
            serializer.prepare(registration.replyType());
        }
        for (SpotActorPacketHandlerRegistration registration : actorPacketHandlers.values()) {
            serializer.prepare(registration.messageType());
            serializer.prepare(registration.replyType());
        }
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType) {
        return createAsync(spotType, List.of());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        List<Message> createParts) {
        requireRegistered(spotType);
        ZLinkBackendSpot spot = primaryNode.createSpot();
        RoutingId spotRid = spot.routingId();
        if (spots.containsKey(spotRid)) {
            spot.close();
            throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
        }
        return activateAsync(spotType, spot, createParts)
            .thenApply(activation -> {
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(spotRid, true);
            });
        }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid)) {
            throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
        }
        ZLinkBackendSpot spot = primaryNode.createSpot();
        spot.setRoutingId(spotRid);
        return activateAsync(spotType, spot, List.of())
            .thenApply(activation -> {
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(spotRid, true);
            });
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid) {
        return getOrCreateAsync(spotType, spotRid, List.of());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid,
        List<Message> createParts) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid)) {
            return CompletableFuture.completedFuture(
                new ZLinkSpotCreateResult(spotRid, false));
        }
        ZLinkBackendSpot spot = primaryNode.createSpot();
        spot.setRoutingId(spotRid);
        return activateAsync(spotType, spot, createParts)
            .thenApply(activation -> {
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(spotRid, true);
            });
    }

    @Override
    public CompletionStage<Optional<ZLinkSpotInfo>> findAsync(RoutingId spotRid) {
        requireRoutingId(spotRid);
        return CompletableFuture.completedFuture(
            spots.containsKey(spotRid)
                ? Optional.of(new ZLinkSpotInfo(spotRid))
                : Optional.empty());
    }

    @Override
    public CompletionStage<List<ZLinkSpotInfo>> listAsync() {
        return CompletableFuture.completedFuture(
            spots.keySet().stream().map(ZLinkSpotInfo::new).toList());
    }

    @Override
    public CompletionStage<Boolean> removeAsync(RoutingId spotRid) {
        requireRoutingId(spotRid);
        SpotActivation removed = spots.remove(spotRid);
        if (removed == null) {
            return CompletableFuture.completedFuture(false);
        }
        removed.close();
        return CompletableFuture.completedFuture(true);
    }

    @Override
    public void close() {
        beginClose();
        for (EntrySpotActivation entrySpot : entrySpots) {
            entrySpot.close();
        }
        entrySpots.clear();
        for (SpotActivation spot : spots.values()) {
            spot.close();
        }
        spots.clear();
        for (ZLinkBackendSpot publisherSpot : publisherSpotsByChannel.values()) {
            publisherSpot.close();
        }
        publisherSpotsByChannel.clear();
        for (ZLinkBackendDealerSocket dealer : attachedChannelDealers) {
            dealer.close();
        }
        attachedChannelDealers.clear();
        for (ZLinkBackendDiscovery discovery : attachedChannelDiscoveries) {
            discovery.close();
        }
        attachedChannelDiscoveries.clear();
        timerExecutor.shutdownNow();
        for (ZLinkBackendSpotNode node : nodes) {
            node.close();
        }
        context.close();
    }

    public void beginClose() {
        closing = true;
    }

    public ZLinkBackendSpotNode primaryNode() {
        return primaryNode;
    }

    public void attachActorRuntime(ZLinkActorRuntime actorRuntime) {
        this.actorRuntime = actorRuntime;
        this.actorRuntime.setDisconnectedNotifier(this::notifySpotActorDisconnected);
        this.actorRuntime.setSpotResolver(this::spotFor);
    }

    public Map<String, ZLinkBackendSpotNode> nodesByName() {
        return Map.copyOf(nodesByName);
    }

    public boolean isActorGatewayRouteReady(RoutingId nodeRid) {
        if (!hasRoutingId(nodeRid)) {
            return true;
        }
        for (ZLinkBackendSpotNode node : nodes) {
            if (nodeRid.equals(node.routingId())) {
                return true;
            }
        }
        return connectedRouterPeerRids.contains(nodeRid);
    }

    public ZLinkSpotOutbound outbound() {
        return new AmbientSpotOutbound();
    }

    public ZLinkSpotPublisherClient publisherClient() {
        return new DefaultSpotPublisherClient();
    }

    public ZLinkSpotRemoteAddress resolveRegistrySpotRemoteAddress(
        String namespaceName,
        String configuredRouterChannelId,
        RoutingId spotRid) {
        requireRoutingId(spotRid);
        String routerChannelId = resolveRouterChannelId(configuredRouterChannelId);
        ZLinkBackendDiscovery discovery = resolveSpotDiscovery(namespaceName);
        try {
            ZLinkBackendSpotRoute route = discovery.resolveSpot(spotRid);
            return new ZLinkSpotRemoteAddress(
                routerChannelId,
                route.nodeRid(),
                route.spotRid(),
                route.spotKind() == null ? ZLinkSpotKind.INVALID : route.spotKind());
        } catch (RuntimeException ex) {
            throw new ZLinkFrameworkException(
                "SPOT route was not found for '" + spotRid + "'.",
                ex);
        }
    }

    public CompletionStage<Optional<Message>> dispatchLocalSessionActor(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload) {
        if (actorRuntime == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor runtime is required for local session actor dispatch"));
        }
        SpotActorPacketHandlerRegistration handler =
            actorPacketHandlers.get(header.packetName());
        if (handler == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor packet handler is not registered: " + header.packetName()));
        }
        boolean isRequest = header.requestSequence().isPresent()
            || header.kind() == ZLinkStreamMessageKind.REQUEST;
        if (isRequest != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor packet kind does not match handler kind: " + header.packetName()));
        }
        Optional<ZLinkActor> localActor = actorRuntime.localActor(actorRef.actorId());
        if (localActor.isEmpty()) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "local actor is not available: " + actorRef.actorId()));
        }
        ZLinkActor actor = localActor.get();
        if (!handler.actorType().isInstance(actor)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor packet handler target type does not match actor: " + actorRef.actorId()));
        }
        Object spotSurface = localActorSpotSurface(actor);
        CompletableFuture<Optional<Message>> result = new CompletableFuture<>();
        return actorRuntime.submitActorDispatch(
            actor.actorId(),
            () -> dispatchLocalSessionActorPacket(handler, spotSurface, actor, payload)
                .whenComplete((reply, error) -> {
                    if (error != null) {
                        result.completeExceptionally(error);
                    } else {
                        result.complete(reply);
                    }
                })
                .thenApply(ignored -> null))
            .thenCompose(ignored -> result);
    }
    @Override
    public RoutingId resolveAcceptedSpotRouteNodeRid(String channelName) {
        SpotNodeRegistration nodeRegistration = acceptedRouteNodesByChannel.get(channelName);
        if (nodeRegistration == null) {
            throw new ZLinkConfigurationException(
                "routed SPOT target channel is not accepted by a SpotNode: " + channelName);
        }
        ZLinkBackendSpotNode node = nodesByName.get(nodeRegistration.nodeName());
        if (node == null) {
            throw new ZLinkConfigurationException(
                "accepted routed SPOT node is not active: " + nodeRegistration.nodeName());
        }
        return node.routingId();
    }

    @Override
    public void handleSend(
        String channelName,
        ZLinkBackendRouterSocket router,
        List<Message> relayParts) {
        RoutingId targetNodeRid = resolveAcceptedSpotRouteNodeRid(channelName);
        RoutingId targetSpotRid = ZLinkRoutedSpotRelayPackets.decodeTargetSpotRid(relayParts);
        List<Message> spotParts = ZLinkRoutedSpotRelayPackets.copySpotPayloadParts(relayParts);
        try {
            if (!router.sendToSpot(targetNodeRid, targetSpotRid, spotParts, SendFlags.NONE)) {
                throw new ZLinkConfigurationException(
                    "routed SPOT ingress channel is not ready for send: " + channelName);
            }
        } finally {
            spotParts.forEach(Message::close);
        }
    }

    @Override
    public CompletionStage<List<Message>> handleRequest(
        String channelName,
        ZLinkBackendRouterSocket router,
        List<Message> relayParts,
        Duration timeout) {
        CompletableFuture<List<Message>> result = new CompletableFuture<>();
        RoutingId targetNodeRid = resolveAcceptedSpotRouteNodeRid(channelName);
        RoutingId targetSpotRid = ZLinkRoutedSpotRelayPackets.decodeTargetSpotRid(relayParts);
        List<Message> spotParts = ZLinkRoutedSpotRelayPackets.copySpotPayloadParts(relayParts);
        try {
            if (!router.requestToSpot(targetNodeRid, targetSpotRid, spotParts, reply -> {
                try {
                    result.complete(ZLinkRoutedSpotRelayPackets.copyReplyParts(reply.parts()));
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                } finally {
                    reply.parts().forEach(Message::close);
                }
            }, SendFlags.NONE, timeout)) {
                result.completeExceptionally(new ZLinkConfigurationException(
                    "routed SPOT ingress channel is not ready for request: " + channelName));
            }
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        } finally {
            spotParts.forEach(Message::close);
        }
        return result;
    }

    private void requireRegistered(Class<? extends ZLinkSpot> spotType) {
        if (spotType == null) {
            throw new ZLinkConfigurationException("spot type is required");
        }
        if (!registeredSpotTypes.contains(spotType)) {
            throw new ZLinkConfigurationException(
                "spot type is not registered: " + spotType.getName());
        }
    }

    private static void requireRoutingId(RoutingId spotRid) {
        if (spotRid == null) {
            throw new ZLinkConfigurationException("spotRid is required");
        }
    }

    private CompletionStage<SpotActivation> activateAsync(
        Class<? extends ZLinkSpot> spotType,
        ZLinkBackendSpot backendSpot,
        List<Message> createParts) {
        List<Message> effectiveCreateParts = createParts == null ? List.of() : List.copyOf(createParts);
        DefaultSpotContext spotContext =
            new DefaultSpotContext(primaryNode.routingId(), backendSpot);
        ZLinkSpot spot = tryCreateSpot(spotType, spotContext);
        if (spot == null) {
            return CompletableFuture.completedFuture(new SpotActivation(null, backendSpot, spotContext));
        }
        spotContext.setSpot(spot);
        spot.configure();
        spotContext.closeRegistration();
        spotContext.bindSubscriptions(backendSpot);
        return withCurrentOutbound(spotContext.outbound, () -> spot.onCreateAsync(effectiveCreateParts))
            .thenCompose(ignored -> withCurrentOutbound(spotContext.outbound, spot::onInitializeAsync))
            .thenApply(ignored -> {
                SpotActivation activation = new SpotActivation(spot, backendSpot, spotContext);
                backendSpot.onDispatchEvent(activation::handleDispatchEvent);
                return activation;
            })
            .handle((activation, error) -> {
                if (error == null) {
                    return activation;
                }
                spotContext.closeTimers();
                backendSpot.close();
                throw new CompletionException(error);
            });
    }

    private EntrySpotActivation activateEntrySpot(
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot,
        Class<? extends ZLinkEntrySpot> entrySpotType) {
        DefaultEntrySpotContext entryContext = new DefaultEntrySpotContext(
            nodeRid,
            backendSpot);
        ZLinkEntrySpot entrySpot = createEntrySpot(entrySpotType, entryContext);
        if (entrySpot == null) {
            backendSpot.close();
            throw new ZLinkConfigurationException(
                "entry spot requires a public constructor accepting ZLinkEntrySpotContext or a public no-arg constructor: "
                    + entrySpotType.getName());
        }
        if (entrySpot.context() != entryContext) {
            backendSpot.close();
            throw new ZLinkConfigurationException(
                "entry spot must expose the context provided by the runtime: "
                    + entrySpotType.getName());
        }
        entryContext.setEntrySpot(entrySpot);
        entrySpot.configure();
        entryContext.closeRegistration();
        entryContext.bindSubscriptions(backendSpot);
        withCurrentOutbound(entryContext.outbound, entrySpot::onInitializeAsync)
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    entryContext.closeTimers();
                    backendSpot.close();
                }
            });
        EntrySpotActivation activation = new EntrySpotActivation(
            entrySpot,
            backendSpot,
            entryContext);
        backendSpot.onDispatchEvent(activation::handleDispatchEvent);
        return activation;
    }

    private <T> CompletionStage<T> withCurrentOutbound(
        DefaultSpotOutbound outbound,
        Supplier<CompletionStage<T>> action) {
        DefaultSpotOutbound previous = currentOutbound.get();
        currentOutbound.set(outbound);
        try {
            return action.get();
        } finally {
            if (previous == null) {
                currentOutbound.remove();
            } else {
                currentOutbound.set(previous);
            }
        }
    }

    private DefaultSpotOutbound requireCurrentOutbound() {
        DefaultSpotOutbound outbound = currentOutbound.get();
        if (outbound == null) {
            throw new ZLinkConfigurationException(
                "ZLinkSpotOutbound can only be used inside an active Spot callback");
        }
        return outbound;
    }

    private CompletionStage<Void> notifySpotActorLifecycle(
        Object spotSurface,
        ZLinkActor actor,
        ZLinkSpotActorChangeResult result,
        List<SpotActorLifecycleHandlerRegistration> registrations) {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        for (SpotActorLifecycleHandlerRegistration registration : registrations) {
            if (!matchesActorLifecycleTarget(registration, spotSurface, actor)) {
                continue;
            }
            tail = tail.thenCompose(ignored ->
                invokeSpotActorLifecycleHandler(registration, spotSurface, actor, result));
        }
        return tail;
    }

    private CompletionStage<Void> notifySpotActorLifecycle(
        ZLinkActor actor,
        ZLinkSpotActorChangeResult result,
        List<SpotActorLifecycleHandlerRegistration> registrations) {
        return notifySpotActorLifecycle(currentSpotSurface(actor), actor, result, registrations);
    }

    private CompletionStage<Void> notifySpotActorDisconnected(ZLinkActor actor) {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        Object spotSurface = currentSpotSurface(actor);
        for (SpotActorLifecycleHandlerRegistration registration : actorDisconnectedHandlers) {
            if (!matchesActorLifecycleTarget(registration, spotSurface, actor)) {
                continue;
            }
            tail = tail.thenCompose(ignored ->
                invokeSpotActorDisconnectedHandler(registration, spotSurface, actor));
        }
        return tail;
    }

    private CompletionStage<Void> invokeSpotActorDisconnectedHandler(
        SpotActorLifecycleHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object invocationResult =
                registration.handlerMethod().invoke(
                    handler,
                    actorDisconnectedArguments(registration.handlerMethod(), spotSurface, actor));
            if (invocationResult instanceof CompletionStage<?> stage) {
                return stage.thenApply(ignored -> null);
            }
            return CompletableFuture.completedFuture(null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke Spot actor disconnected handler: "
                    + registration.handlerType().getName()
                    + "."
                    + registration.handlerMethod().getName()));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private ZLinkSpot spotFor(RoutingId spotRid) {
        SpotActivation activation = spots.get(spotRid);
        return activation == null ? null : activation.spot;
    }

    private Object spotSurfaceFor(RoutingId spotRid) {
        SpotActivation activation = spots.get(spotRid);
        if (activation != null) {
            return activation.spot;
        }
        for (EntrySpotActivation entrySpot : entrySpots) {
            if (entrySpot.backendSpot.routingId().equals(spotRid)) {
                return entrySpot.entrySpot;
            }
        }
        return null;
    }

    private Object currentSpotSurface(ZLinkActor actor) {
        try {
            return actor.context().getSpot();
        } catch (RuntimeException ignored) {
            return null;
        }
    }

    private Object localActorSpotSurface(ZLinkActor actor) {
        Object current = currentSpotSurface(actor);
        if (current != null) {
            return current;
        }
        if (actorRuntime != null) {
            Optional<RoutingId> spotRid = actorRuntime.spotRid(actor);
            if (spotRid.isPresent()) {
                Object surface = spotSurfaceFor(spotRid.get());
                if (surface != null) {
                    return surface;
                }
            }
        }
        return entrySpots.isEmpty() ? null : entrySpots.get(0).entrySpot;
    }

    private CompletionStage<Optional<Message>> dispatchLocalSessionActorPacket(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        return registration.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
            ? invokeLocalActorRequestHandler(registration, spotSurface, actor, payload)
            : invokeLocalActorSendHandler(registration, spotSurface, actor, payload)
                .thenApply(ignored -> Optional.empty());
    }

    private CompletionStage<Void> invokeLocalActorSendHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object message = serializer.deserialize(payload, registration.messageType());
            Object result = registration.handlerMethod().invoke(
                handler,
                actorPacketArguments(
                    registration.handlerMethod(),
                    spotSurface,
                    actor,
                    new DefaultSpotActorSendContext(registration.packetName()),
                    message));
            if (result instanceof CompletionStage<?> stage) {
                return stage.thenApply(ignored -> null);
            }
            return CompletableFuture.completedFuture(null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke local session actor send handler: "
                    + registration.handlerType().getName()
                    + "."
                    + registration.handlerMethod().getName(),
                ex instanceof InvocationTargetException invocation
                    ? invocation.getCause()
                    : ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private CompletionStage<Optional<Message>> invokeLocalActorRequestHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object message = serializer.deserialize(payload, registration.messageType());
            Object result = registration.handlerMethod().invoke(
                handler,
                actorPacketArguments(
                    registration.handlerMethod(),
                    spotSurface,
                    actor,
                    new DefaultSpotActorRequestContext(registration.packetName()),
                    message));
            CompletionStage<?> stage = result instanceof CompletionStage<?> completionStage
                ? completionStage
                : CompletableFuture.completedFuture(result);
            return stage.thenApply(reply -> Optional.of(serializer.serialize(reply)));
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke local session actor request handler: "
                    + registration.handlerType().getName()
                    + "."
                    + registration.handlerMethod().getName(),
                ex instanceof InvocationTargetException invocation
                    ? invocation.getCause()
                    : ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private CompletionStage<Void> invokeSpotActorLifecycleHandler(
        SpotActorLifecycleHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        ZLinkSpotActorChangeResult result) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object invocationResult =
                registration.handlerMethod().invoke(
                    handler,
                    actorLifecycleArguments(registration.handlerMethod(), spotSurface, actor, result));
            if (invocationResult instanceof CompletionStage<?> stage) {
                return stage.thenApply(ignored -> null);
            }
            return CompletableFuture.completedFuture(null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke Spot actor lifecycle handler: "
                    + registration.handlerType().getName()
                    + "."
                    + registration.handlerMethod().getName()));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private synchronized ZLinkBackendSpot publisherSpot(String channelName) {
        ZLinkBackendSpotNode node = publisherNodesByChannel.get(channelName);
        if (node == null) {
            throw new ZLinkConfigurationException(
                "SPOT publisher client is not configured: " + channelName);
        }
        return publisherSpotsByChannel.computeIfAbsent(
            channelName,
            ignored -> node.createSpot());
    }

    private void attachChannelClient(
        ZLinkChannelBackendAdapter channelAdapter,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkBackendSpotNode node,
        SpotChannelClientRegistration attached) {
        ZLinkBackendDealerSocket dealer = channelAdapter.createDealerSocket(context);
        dealer.setChannelName(attached.channelName());
        attachedChannelDealers.add(dealer);
        if (!attached.manualConnections().isEmpty()) {
            for (String endpoint : attached.manualConnections()) {
                dealer.connect(endpoint);
            }
            node.attachChannelDealerManual(attached.channelName(), dealer);
            return;
        }

        ZLinkBackendDiscovery discovery = channelAdapter.createDiscovery(
            context,
            ZLinkBackendAutoConnectType.CLIENT_SERVER,
            attached.channelName());
        for (String endpoint : frameworkRegistration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        attachedChannelDiscoveries.add(discovery);
        dealer.attachDiscovery(discovery);
        node.attachChannelDealer(discovery, dealer);
    }

    private void attachSpotMeshDiscovery(
        ZLinkChannelBackendAdapter channelAdapter,
        ZLinkFrameworkRegistration frameworkRegistration,
        SpotNodeRegistration nodeRegistration,
        ZLinkBackendSpotNode node) {
        if (!frameworkRegistration.discoveryEnabled()
            || (!nodeRegistration.routerEnabled() && !nodeRegistration.pubSubEnabled())) {
            return;
        }
        ZLinkBackendDiscovery discovery = channelAdapter.createDiscovery(
            context,
            ZLinkBackendAutoConnectType.SPOT_MESH,
            nodeRegistration.meshName());
        for (String endpoint : frameworkRegistration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        node.attachDiscovery(discovery);
        attachedChannelDiscoveries.add(discovery);
        spotDiscoveriesByMesh.putIfAbsent(nodeRegistration.meshName(), discovery);
        spotDiscoveryBindings.add(new SpotDiscoveryBinding(
            nodeRegistration.meshName(),
            node,
            discovery,
            nodeRegistration.routerEnabled(),
            nodeRegistration.pubSubEnabled(),
            nodeRegistration.routerBind(),
            nodeRegistration.pubBind()));
    }

    private void startSpotDiscoveryReconciliation() {
        if (spotDiscoveryBindings.isEmpty()) {
            return;
        }
        timerExecutor.scheduleWithFixedDelay(
            this::reconcileSpotDiscoveryPeers,
            0,
            100,
            TimeUnit.MILLISECONDS);
    }

    private void reconcileSpotDiscoveryPeers() {
        for (SpotDiscoveryBinding binding : spotDiscoveryBindings) {
            try {
                reconcileSpotDiscoveryPeer(binding);
            } catch (RuntimeException ignored) {
                // Discovery can be temporarily unavailable while registry peers are starting.
                // Keep the reconciler alive so the next tick can converge.
            }
        }
    }

    private void reconcileSpotDiscoveryPeer(SpotDiscoveryBinding binding) {
            bindLocalRouterEndpoint(binding);
            List<ZLinkBackendRegistryMemberPeerEntry> peers = binding.discovery().memberPeers();
            Map<RoutingId, String> routerEndpointsByRid =
                routerEndpointsByRid(peers, binding.meshName());
            for (ZLinkBackendRegistryMemberPeerEntry peer : peers) {
                if (!"SPOT_MESH".equalsIgnoreCase(peer.autoConnectType())
                    || !binding.meshName().equals(peer.channelName())
                    || peer.endpoint() == null
                    || peer.endpoint().isBlank()
                    || peer.endpoint().equals(binding.routerBind())
                    || peer.endpoint().equals(binding.pubBind())) {
                    continue;
                }
                if ("ROUTER".equalsIgnoreCase(peer.serviceRole())) {
                    if (binding.routerEnabled() && !binding.pubSubEnabled()) {
                        connectDiscoveredRouterPeer(
                            binding,
                            peer.routingId(),
                            peer.endpoint());
                    }
                    continue;
                }
                if (!"SPOT".equalsIgnoreCase(peer.serviceRole())) {
                    continue;
                }
                if (binding.pubSubEnabled()) {
                    connectDiscoveredPubSubPeer(binding, peer.endpoint());
                }
                if (!binding.routerEnabled()) {
                    continue;
                }
                RoutingId peerRoutingId = peer.routingId();
                String routerEndpoint = resolveRouterEndpoint(
                    binding,
                    peerRoutingId,
                    peer.endpoint(),
                    routerEndpointsByRid,
                    binding.discovery());
                if (routerEndpoint == null
                    || routerEndpoint.isBlank()
                    || routerEndpoint.equals(binding.routerBind())
                    || routerEndpoint.equals(binding.pubBind())) {
                    continue;
                }
                connectDiscoveredRouterPeer(binding, peerRoutingId, routerEndpoint);
            }
    }

    private static Map<RoutingId, String> routerEndpointsByRid(
        List<ZLinkBackendRegistryMemberPeerEntry> peers,
        String meshName) {
        Map<RoutingId, String> endpoints = new HashMap<>();
        for (ZLinkBackendRegistryMemberPeerEntry peer : peers) {
            if ("SPOT_MESH".equalsIgnoreCase(peer.autoConnectType())
                && "ROUTER".equalsIgnoreCase(peer.serviceRole())
                && meshName.equals(peer.channelName())
                && peer.endpoint() != null
                && !peer.endpoint().isBlank()
                && hasRoutingId(peer.routingId())) {
                endpoints.put(peer.routingId(), peer.endpoint());
            }
        }
        return endpoints;
    }

    private static String resolveRouterEndpoint(
        SpotDiscoveryBinding binding,
        RoutingId peerRoutingId,
        String peerEndpoint,
        Map<RoutingId, String> routerEndpointsByRid,
        ZLinkBackendDiscovery discovery) {
        if (!hasRoutingId(peerRoutingId)) {
            return binding.pubSubEnabled() ? null : peerEndpoint;
        }
        String discoveredEndpoint = routerEndpointsByRid.get(peerRoutingId);
        if (discoveredEndpoint != null) {
            return discoveredEndpoint;
        }
        String routeEndpoint = resolveRouterEndpointRoute(discovery, peerRoutingId);
        if (routeEndpoint != null && !routeEndpoint.isBlank()) {
            return routeEndpoint;
        }
        return binding.pubSubEnabled() ? null : peerEndpoint;
    }

    private static void bindLocalRouterEndpoint(SpotDiscoveryBinding binding) {
        if (!binding.routerEnabled()
            || binding.routerBind() == null
            || binding.routerBind().isBlank()
            || !hasRoutingId(binding.node().routingId())) {
            return;
        }
        try {
            binding.discovery().bindRoute(
                ROUTER_ENDPOINT_ROUTE_KIND,
                binding.node().routingId().toBytes(),
                binding.routerBind().getBytes(StandardCharsets.UTF_8));
        } catch (RuntimeException ignored) {
            // Route publication converges through the next discovery tick.
        }
    }

    private static String resolveRouterEndpointRoute(
        ZLinkBackendDiscovery discovery,
        RoutingId peerRoutingId) {
        try {
            return discovery.resolveRoute(
                    ROUTER_ENDPOINT_ROUTE_KIND,
                    peerRoutingId.toBytes())
                .endpoint()
                .orElse(null);
        } catch (RuntimeException ex) {
            return null;
        }
    }

    private static boolean hasRoutingId(RoutingId routingId) {
        return routingId != null && routingId.size() > 0;
    }

    private void connectDiscoveredPubSubPeer(
        SpotDiscoveryBinding binding,
        String endpoint) {
        String key = binding.meshName() + "|pubsub|" + endpoint;
        if (!discoveredPubSubPeers.add(key)) {
            return;
        }
        try {
            binding.node().connectPeer(endpoint);
        } catch (ZlinkConnectException ex) {
            if (isIdempotentConnectFailure(ex)) {
                return;
            }
            discoveredPubSubPeers.remove(key);
        } catch (RuntimeException ex) {
            discoveredPubSubPeers.remove(key);
        }
    }

    private void connectDiscoveredRouterPeer(
        SpotDiscoveryBinding binding,
        RoutingId peerRoutingId,
        String endpoint) {
        String key = binding.meshName() + "|router|" + endpoint;
        if (!discoveredRouterPeers.add(key)) {
            int sightings = discoveredRouterPeerSightings.merge(key, 1, Integer::sum);
            if (hasRoutingId(peerRoutingId)
                && sightings >= ROUTER_PEER_CONVERGENCE_SNAPSHOTS) {
                connectedRouterPeerRids.add(peerRoutingId);
            }
            return;
        }
        discoveredRouterPeerSightings.put(key, 1);
        try {
            if (hasRoutingId(peerRoutingId)) {
                binding.node().connectRouterChannelPeerRid(
                    binding.meshName(),
                    peerRoutingId,
                    endpoint);
            } else {
                binding.node().connectRouterChannelPeer(binding.meshName(), endpoint);
            }
        } catch (ZlinkConnectException ex) {
            if (isIdempotentConnectFailure(ex)) {
                int sightings = discoveredRouterPeerSightings.merge(key, 1, Integer::sum);
                if (hasRoutingId(peerRoutingId)
                    && sightings >= ROUTER_PEER_CONVERGENCE_SNAPSHOTS) {
                    connectedRouterPeerRids.add(peerRoutingId);
                }
                return;
            }
            discoveredRouterPeers.remove(key);
            discoveredRouterPeerSightings.remove(key);
        } catch (RuntimeException ex) {
            discoveredRouterPeers.remove(key);
            discoveredRouterPeerSightings.remove(key);
        }
    }

    private static boolean isIdempotentConnectFailure(ZlinkConnectException ex) {
        int errno = ex.getInternalErrno();
        return ex.getResult() == ConnectResult.BUSY
            || errno == 16
            || errno == 106
            || errno == 114
            || errno == 115;
    }

    private void attachAcceptedSpotRouteChannel(
        ZLinkChannelBackendAdapter channelAdapter,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkBackendSpotNode node,
        SpotRouteChannelAcceptanceRegistration acceptance) {
        if (!acceptance.manualConnections().isEmpty()) {
            for (String endpoint : acceptance.manualConnections()) {
                node.connectRouterChannelPeer(acceptance.channelName(), endpoint);
            }
            return;
        }

        ZLinkBackendDiscovery discovery = channelAdapter.createDiscovery(
            context,
            acceptedRouteAutoConnectType(frameworkRegistration, acceptance.channelName()),
            acceptance.channelName());
        for (String endpoint : frameworkRegistration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        attachedChannelDiscoveries.add(discovery);
        node.attachSpotRouteChannelDiscovery(acceptance.channelName(), discovery);
    }

    private static ZLinkBackendAutoConnectType acceptedRouteAutoConnectType(
        ZLinkFrameworkRegistration frameworkRegistration,
        String channelName) {
        return frameworkRegistration.channels().stream()
            .filter(channel -> channel.name().equals(channelName))
            .findFirst()
            .map(channel -> channel.kind() == ChannelKind.ROUTE_MESH
                ? ZLinkBackendAutoConnectType.ROUTE_MESH
                : ZLinkBackendAutoConnectType.CLIENT_SERVER)
            .orElseThrow(() -> new ZLinkConfigurationException(
                "accepted SPOT route channel is not registered: " + channelName));
    }

    private ZLinkSpot tryCreateSpot(
        Class<? extends ZLinkSpot> spotType,
        ZLinkSpotContext context) {
        try {
            return (ZLinkSpot) ZLinkHandlerFactory.services(handlerFactory)
                .add(ZLinkSpotContext.class, context)
                .create(spotType);
        } catch (RuntimeException ex) {
            throw new ZLinkConfigurationException(
                "failed to create spot: " + spotType.getName(),
                ex);
        }
    }

    private ZLinkEntrySpot createEntrySpot(
        Class<? extends ZLinkEntrySpot> entrySpotType,
        ZLinkEntrySpotContext context) {
        try {
            return (ZLinkEntrySpot) ZLinkHandlerFactory.services(handlerFactory)
                .add(ZLinkEntrySpotContext.class, context)
                .create(entrySpotType);
        } catch (RuntimeException ex) {
            throw new ZLinkConfigurationException(
                "failed to create entry spot: " + entrySpotType.getName(),
                ex);
        }
    }

    private final class DefaultEntrySpotContext implements ZLinkEntrySpotContext {
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotOutbound outbound;
        private final List<DefaultSpotContext> timerContexts = new ArrayList<>();
        private final List<Class<?>> handlerTypes = new ArrayList<>();
        private final List<Class<?>> packetHandlerTypes = new ArrayList<>();
        private final List<SpotSubscriptionRegistration> subscriptionHandlerTypes = new ArrayList<>();
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkEntrySpot entrySpot;

        DefaultEntrySpotContext(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.outbound = new DefaultSpotOutbound(nodeRid, backendSpot);
        }

        @Override
        public RoutingId spotRid() {
            return backendSpot.routingId();
        }

        @Override
        public RoutingId nodeRid() {
            return nodeRid;
        }

        void setEntrySpot(ZLinkEntrySpot entrySpot) {
            this.entrySpot = entrySpot;
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return outbound;
        }

        @Override
        public ZLinkSpotHandlerRegistry handlers() {
            return new ZLinkSpotHandlerRegistry() {
                @Override
                public void addHandler(Class<?> handlerType) {
                    ensureRegistrationOpen();
                    handlerTypes.add(requireHandlerType(handlerType));
                }

                @Override
                public void addPacket(Class<?> handlerType) {
                    ensureRegistrationOpen();
                    packetHandlerTypes.add(requireHandlerType(handlerType));
                }

                @Override
                public void addSubscribe(String topic, Class<?> handlerType) {
                    ensureRegistrationOpen();
                    subscriptionHandlerTypes.add(new SpotSubscriptionRegistration(
                        requireTopic(topic),
                        requireHandlerType(handlerType)));
                }
            };
        }

        @Override
        public CompletionStage<ZLinkTimer> addTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            DefaultSpotContext timerContext = new DefaultSpotContext(nodeRid, backendSpot);
            timerContext.setSpot(new EntrySpotTimerSurface(this));
            timerContexts.add(timerContext);
            return timerContext.addTimer(name, period, handlerType, options);
        }

        void closeTimers() {
            timerContexts.forEach(DefaultSpotContext::closeTimers);
            timerContexts.clear();
        }

        void closeRegistration() {
            registrationOpen = false;
            for (Class<?> handlerType : handlerTypes) {
                registerConfiguredSpotHandler(handlerType, entrySpot.getClass(), packetHandlers);
            }
            for (Class<?> handlerType : packetHandlerTypes) {
                SpotPacketHandlerRegistration registration =
                    createSpotPacketRegistration(handlerType, entrySpot.getClass());
                if (packetHandlers.putIfAbsent(registration.packetName(), registration) != null) {
                    throw new ZLinkConfigurationException(
                        "duplicate EntrySpot packet handler packet: " + registration.packetName());
                }
            }
            for (SpotSubscriptionRegistration subscription : subscriptionHandlerTypes) {
                SpotSubscriptionHandlerRegistration registration =
                    createSpotSubscriptionRegistration(
                        subscription.topic(),
                        subscription.handlerType(),
                        entrySpot.getClass());
                subscriptionHandlers
                    .computeIfAbsent(subscription.topic(), ignored -> new ArrayList<>())
                    .add(registration);
            }
        }

        void bindSubscriptions(ZLinkBackendSpot backendSpot) {
            for (String topic : subscriptionHandlers.keySet()) {
                backendSpot.setSubscription(topic);
            }
        }

        SpotPacketHandlerRegistration packetHandler(String packetName) {
            return packetHandlers.get(packetName);
        }

        List<SpotSubscriptionHandlerRegistration> subscriptionHandlers(String topic) {
            return subscriptionHandlers.getOrDefault(topic, List.of());
        }

        private void ensureRegistrationOpen() {
            if (!registrationOpen) {
                throw new ZLinkConfigurationException(
                    "EntrySpot handler registration is only allowed while configure is running");
            }
        }
    }

    private final class EntrySpotTimerSurface implements ZLinkSpot {
        private final DefaultEntrySpotContext context;

        EntrySpotTimerSurface(DefaultEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return new EntrySpotBackedSpotContext(context);
        }
    }

    private record EntrySpotBackedSpotContext(
        DefaultEntrySpotContext entryContext) implements ZLinkSpotContext {
        @Override
        public RoutingId spotRid() {
            return entryContext.spotRid();
        }

        @Override
        public RoutingId nodeRid() {
            return entryContext.nodeRid();
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return entryContext.outbound();
        }

        @Override
        public CompletionStage<Void> leaveActorAsync(
            ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkTimer> addTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            return entryContext.addTimer(name, period, handlerType, options);
        }
    }

    private final class EntrySpotActivation implements AutoCloseable {
        private final ZLinkEntrySpot entrySpot;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultEntrySpotContext context;
        private ZLinkBackendActorReceived pendingActorHeader;

        EntrySpotActivation(
            ZLinkEntrySpot entrySpot,
            ZLinkBackendSpot backendSpot,
            DefaultEntrySpotContext context) {
            this.entrySpot = entrySpot;
            this.backendSpot = backendSpot;
            this.context = context;
        }

        void handleDispatchEvent(ZLinkBackendSpotDispatchInfo info) {
            if (closing) {
                return;
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                drainRoutes();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE) {
                drainSubscriptions();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE) {
                drainUnhandledActorJoins();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                dispatchActorMessages(info.actorMessages());
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE) {
                drainActorLifecycleEvents();
            }
            for (ZLinkBackendActorReceived actorMessage : info.actorMessages()) {
                actorMessage.close();
            }
        }

        private void drainRoutes() {
            while (true) {
                ZLinkBackendReceived received =
                    backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchRoute(received);
            }
        }

        private void dispatchRoute(ZLinkBackendReceived received) {
            if (received.parts().isEmpty()) {
                received.close();
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
            SpotPacketHandlerRegistration handler =
                context.packetHandler(packet.packetName());
            if (handler == null) {
                received.close();
                return;
            }
            if (received.requestSeq().isPresent()) {
                if (!handler.request()) {
                    received.close();
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotRequestHandler(handler, entrySpot, payloadCopy)
                    .thenAccept(reply -> received.reply(List.of(reply)))
                    .whenComplete((ignored, error) -> {
                        payloadCopy.close();
                        received.close();
                    });
                return;
            }
            try (received) {
                if (handler.request()) {
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotPacketHandler(handler, entrySpot, payloadCopy)
                    .whenComplete((ignored, error) -> payloadCopy.close());
            }
        }

        private void drainSubscriptions() {
            while (true) {
                ZLinkBackendTopicMessage received =
                    backendSpot.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchSubscription(received);
            }
        }

        private void dispatchSubscription(ZLinkBackendTopicMessage received) {
            try {
                if (received.parts().isEmpty()) {
                    return;
                }
                ParsedPacket packet = parsePacket(received.parts());
                for (SpotSubscriptionHandlerRegistration handler :
                    context.subscriptionHandlers(received.topic())) {
                    if (!handler.packetName().equals(packet.packetName())) {
                        continue;
                    }
                    Message payloadCopy = Message.from(packet.payload());
                    invokeSpotSubscriptionHandler(handler, entrySpot, payloadCopy)
                        .whenComplete((ignored, error) -> payloadCopy.close());
                }
            } finally {
                received.parts().forEach(Message::close);
            }
        }

        private void drainActorLifecycleEvents() {
            while (true) {
                if (closing) {
                    return;
                }
                ZLinkBackendActorLifecycleEvent event =
                    backendSpot.recvActorLifecycle(ZLinkBackendRecvMode.DONT_WAIT);
                if (event == null) {
                    return;
                }
                if (actorRuntime == null) {
                    continue;
                }
                ZLinkBackendActorRef actorRef = event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
                    ? event.info().previousActor()
                    : event.info().currentActor();
                actorRuntime.localActor(actorRef.actorId())
                    .ifPresent(actor -> dispatchActorLifecycle(event, actorRef, actor));
            }
        }

        private void dispatchActorLifecycle(
            ZLinkBackendActorLifecycleEvent event,
            ZLinkBackendActorRef actorRef,
            ZLinkActor actor) {
            if (closing) {
                return;
            }
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT) {
                actorRuntime.markLeft(actor);
                ZLinkSpotActorChangeResult result =
                    new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.LEAVE_SPOT);
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> notifySpotActorLifecycle(entrySpot, actor, result, actorLeftHandlers));
                return;
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            actorRuntime.markJoined(
                actor,
                actorRef,
                spotRid,
                spotSurfaceFor(spotRid) instanceof ZLinkSpot spot ? spot : null);
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            actorRuntime.submitActorDispatch(
                actor.actorId(),
                () -> notifySpotActorLifecycle(entrySpot, actor, result, actorJoinedHandlers));
        }

        private void dispatchActorMessages(List<ZLinkBackendActorReceived> actorMessages) {
            int index = 0;
            while (index < actorMessages.size() || pendingActorHeader != null) {
                boolean pendingHeader = pendingActorHeader != null;
                ZLinkBackendActorReceived headerPart = pendingHeader
                    ? pendingActorHeader
                    : actorMessages.get(index++);
                ZLinkBackendActorReceived bodyPart =
                    headerPart.hasMore() && index < actorMessages.size()
                        ? actorMessages.get(index++)
                        : null;
                if (headerPart.hasMore() && bodyPart == null) {
                    if (!pendingHeader) {
                        pendingActorHeader = copyActorReceived(headerPart);
                    }
                    return;
                }
                if (pendingHeader) {
                    pendingActorHeader = null;
                }
                ActorPacketHeader packetHeader = decodeActorPacketHeader(headerPart);
                SpotActorPacketHandlerRegistration handler =
                    actorPacketHandlers.get(packetHeader.packetName());
                if (handler == null || actorRuntime == null) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                if (packetHeader.requestSeq().isPresent()
                    != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                Optional<ZLinkActor> localActor =
                    actorRuntime.localActor(headerPart.actor().actorId());
                if (localActor.isEmpty()) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                ZLinkActor actor = localActor.get();
                if (!handler.actorType().isInstance(actor)) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                ZLinkBackendActorReceived headerCopy = pendingHeader
                    ? headerPart
                    : copyActorReceived(headerPart);
                Message payloadCopy = bodyPart == null
                    ? Message.from(new byte[0])
                    : Message.from(bodyPart.message());
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> dispatchActorPacket(
                        handler,
                        actor,
                        packetHeader,
                        headerCopy,
                        payloadCopy));
            }
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            ZLinkActor actor,
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            actorRuntime.bindNativeSession(actor, primaryNode, headerPart.actor());
            CompletionStage<Optional<Message>> stage = withCurrentOutbound(
                context.outbound,
                () -> handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
                    ? invokeActorRequestHandler(handler, actor, payload)
                    : invokeActorSendHandler(handler, actor, payload)
                        .thenApply(ignored -> Optional.empty()));
            return stage.handle((reply, error) -> {
                    if (error != null) {
                        return Optional.of(new ActorDispatchReply(
                            encodeActorErrorFrame(packetHeader, error),
                            true));
                    }
                    return reply.map(message -> new ActorDispatchReply(message, false));
                })
                .thenCompose(reply -> {
                    if (reply.isEmpty()) {
                        return CompletableFuture.completedFuture(null);
                    }
                    byte[] frameBytes;
                    try (Message frame = reply.get().streamFrame()
                        ? reply.get().message()
                        : encodeActorReplyFrame(packetHeader, headerPart, reply.get().message())) {
                        frameBytes = frame.toByteArray();
                    }
                    return sendActorBoundSessionWithRetry(
                        primaryNode,
                        new ZLinkBackendActorRef(
                            headerPart.actor().nodeRid(),
                            actor.actorId(),
                            headerPart.actor().epoch()),
                        actor.actorId(),
                        frameBytes,
                        "actor bound session reply failed");
                })
                .whenComplete((ignored, error) -> {
                    payload.close();
                    headerPart.close();
                });
        }

        private ActorPacketHeader decodeActorPacketHeader(ZLinkBackendActorReceived headerPart) {
            byte[] bytes = headerPart.message().toByteArray();
            try {
                return decodeStreamActorPacketHeader(bytes, headerPart.requestSeq());
            } catch (RuntimeException ignored) {
                return new ActorPacketHeader(
                    headerPart.message().toUtf8String(),
                    headerPart.requestSeq(),
                    false,
                    0);
            }
        }

        private ActorPacketHeader decodeStreamActorPacketHeader(
            byte[] bytes,
            Optional<Long> backendRequestSeq) {
            if (bytes.length < 4) {
                throw new IllegalArgumentException("actor STREAM header is too short");
            }
            ByteBuffer buffer = ByteBuffer.wrap(bytes);
            int kind = Byte.toUnsignedInt(buffer.get());
            int codec = Byte.toUnsignedInt(buffer.get());
            int flags = Byte.toUnsignedInt(buffer.get());
            Optional<Long> requestSeq = backendRequestSeq;
            if ((flags & 0x01) != 0) {
                if (buffer.remaining() < Long.BYTES) {
                    throw new IllegalArgumentException("actor STREAM header missing request sequence");
                }
                long decodedRequestSeq = buffer.getLong();
                if (decodedRequestSeq == 0) {
                    throw new IllegalArgumentException("actor STREAM request sequence is zero");
                }
                requestSeq = Optional.of(decodedRequestSeq);
            }
            if (buffer.remaining() < 1) {
                throw new IllegalArgumentException("actor STREAM header missing packet name");
            }
            int nameLength = Byte.toUnsignedInt(buffer.get());
            if (buffer.remaining() < nameLength) {
                throw new IllegalArgumentException("actor STREAM header packet name is truncated");
            }
            byte[] nameBytes = new byte[nameLength];
            buffer.get(nameBytes);
            if ((flags & 0x02) != 0) {
                if (buffer.remaining() < 2) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                int metadataLength = Short.toUnsignedInt(buffer.getShort());
                if (buffer.remaining() < metadataLength) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                buffer.position(buffer.position() + metadataLength);
            }
            if (buffer.hasRemaining()) {
                throw new IllegalArgumentException("actor STREAM header has trailing bytes");
            }
            if (kind != 1 && kind != 2) {
                throw new IllegalArgumentException("actor STREAM header is not dispatch kind");
            }
            return new ActorPacketHeader(
                new String(nameBytes, StandardCharsets.UTF_8),
                requestSeq,
                true,
                codec);
        }

        private Message encodeActorReplyFrame(
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived originalHeader,
            Message payload) {
            if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
                return Message.from(payload);
            }
            byte[] name = packetHeader.packetName().getBytes(StandardCharsets.UTF_8);
            ByteBuffer header = ByteBuffer.allocate(3 + Long.BYTES + 1 + name.length);
            header.put((byte) 3);
            header.put((byte) packetHeader.codec());
            header.put((byte) 0x01);
            header.putLong(packetHeader.requestSeq().get());
            header.put((byte) name.length);
            header.put(name);
            byte[] headerBytes = header.array();
            byte[] body = payload.toByteArray();
            ByteBuffer frame = ByteBuffer.allocate(6 + headerBytes.length + body.length);
            frame.putShort((short) headerBytes.length);
            frame.putInt(body.length);
            frame.put(headerBytes);
            frame.put(body);
            return Message.from(frame.array());
        }

        private Message encodeActorErrorFrame(
            ActorPacketHeader packetHeader,
            Throwable error) {
            byte[] body = errorMessage(error).getBytes(StandardCharsets.UTF_8);
            if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
                return Message.from(body);
            }
            byte[] name = packetHeader.packetName().getBytes(StandardCharsets.UTF_8);
            ByteBuffer header = ByteBuffer.allocate(3 + Long.BYTES + 1 + name.length);
            header.put((byte) 4);
            header.put((byte) 1);
            header.put((byte) 0x01);
            header.putLong(packetHeader.requestSeq().get());
            header.put((byte) name.length);
            header.put(name);
            byte[] headerBytes = header.array();
            ByteBuffer frame = ByteBuffer.allocate(6 + headerBytes.length + body.length);
            frame.putShort((short) headerBytes.length);
            frame.putInt(body.length);
            frame.put(headerBytes);
            frame.put(body);
            return Message.from(frame.array());
        }

        private record ActorPacketHeader(
            String packetName,
            Optional<Long> requestSeq,
            boolean streamHeader,
            int codec) {
        }

        private void drainUnhandledActorJoins() {
            while (true) {
                ZLinkBackendActorJoinRequest request =
                    backendSpot.recvActorJoin(ZLinkBackendRecvMode.DONT_WAIT);
                if (request == null) {
                    return;
                }
                try {
                    SpotActorJoinHandlerRegistration handler =
                        resolveActorJoinHandler(request);
                    if (handler == null) {
                        try (Message emptyReply = Message.from(new byte[0])) {
                            backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                        }
                        continue;
                    }
                    Message payload = actorJoinPayload(request);
                    withCurrentOutbound(context.outbound, () ->
                        invokeActorJoinHandler(handler, request, payload))
                        .whenComplete((reply, error) -> {
                            if (error != null) {
                                try (Message emptyReply = Message.from(new byte[0])) {
                                    backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                                }
                                return;
                            }
                            backendSpot.replyActorJoin(request, 0, List.of(reply));
                            reply.close();
                        });
                } finally {
                    request.parts().forEach(Message::close);
                }
            }
        }

        private SpotActorJoinHandlerRegistration resolveActorJoinHandler(
            ZLinkBackendActorJoinRequest request) {
            if (request.parts().isEmpty()) {
                return null;
            }
            if (request.parts().size() >= 2) {
                return actorJoinHandlers.get(request.parts().get(0).toUtf8String());
            }
            for (SpotActorJoinHandlerRegistration handler : actorJoinHandlers.values()) {
                if (handler.requestType() == Message.class
                    || handler.requestType() == byte[].class
                    || handler.requestType() == String.class) {
                    return handler;
                }
            }
            return null;
        }

        private Message actorJoinPayload(ZLinkBackendActorJoinRequest request) {
            return request.parts().size() >= 2 ? request.parts().get(1) : request.parts().get(0);
        }

        private CompletionStage<Message> invokeActorJoinHandler(
            SpotActorJoinHandlerRegistration registration,
            ZLinkBackendActorJoinRequest request,
            Message payload) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor join handler"));
            }
            return actorRuntime
                .getOrCreateLocalActor(
                    request.targetActor().actorId(),
                    registration.actorType())
                .thenCompose(actor -> {
                    if (actor.isEmpty()) {
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "Spot actor join target actor is not available: "
                                + request.targetActor().actorId()));
                    }
                    actorRuntime.markJoined(
                        actor.get(),
                        request.targetActor(),
                        backendSpot.routingId(),
                        null);
                    try {
                        Object handler = handlerFactory.create(registration.handlerType());
                        Object requestObject =
                            serializer.deserialize(payload, registration.requestType());
                        Object result = registration.handlerMethod().invoke(
                            handler,
                            actorJoinArguments(
                                registration.handlerMethod(),
                                entrySpot,
                                actor.get(),
                                requestObject));
                        CompletionStage<?> stage =
                            result instanceof CompletionStage<?> completionStage
                                ? completionStage
                                : CompletableFuture.completedFuture(result);
                        return stage
                            .thenCompose(reply -> notifyActorJoined(actor.get())
                                .thenApply(ignored -> reply))
                            .whenComplete((reply, error) -> {
                                if (error != null) {
                                    actorRuntime.markLeft(actor.get());
                                }
                            })
                            .thenApply(serializer::serialize);
                    } catch (IllegalAccessException | InvocationTargetException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "failed to invoke Spot actor join handler: "
                                + registration.handlerType().getName()
                                + "."
                                + registration.handlerMethod().getName(),
                            ex instanceof InvocationTargetException invocation
                                ? invocation.getCause()
                                : ex));
                    } catch (RuntimeException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(ex);
                    }
                });
        }

        private CompletionStage<Void> notifyActorJoined(ZLinkActor actor) {
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            return notifyActorLifecycle(actor, result, actorJoinedHandlers);
        }

        private CompletionStage<Void> notifyActorLifecycle(
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result,
            List<SpotActorLifecycleHandlerRegistration> registrations) {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            for (SpotActorLifecycleHandlerRegistration registration : registrations) {
                if (!matchesActorLifecycleTarget(registration, entrySpot, actor)) {
                    continue;
                }
                tail = tail.thenCompose(ignored ->
                    invokeActorJoinedHandler(registration, entrySpot, actor, result));
            }
            return tail;
        }

        private CompletionStage<Void> invokeActorJoinedHandler(
            SpotActorLifecycleHandlerRegistration registration,
            Object spotSurface,
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object invocationResult =
                    registration.handlerMethod().invoke(
                        handler,
                        actorLifecycleArguments(
                            registration.handlerMethod(),
                            spotSurface,
                            actor,
                            result));
                if (invocationResult instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor joined handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Void> invokeActorSendHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(
                    handler,
                    actorPacketArguments(
                        registration.handlerMethod(),
                        entrySpot,
                        actor,
                        new DefaultSpotActorSendContext(registration.packetName()),
                        message));
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor send handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Optional<Message>> invokeActorRequestHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(
                    handler,
                    actorPacketArguments(
                        registration.handlerMethod(),
                        entrySpot,
                        actor,
                        new DefaultSpotActorRequestContext(registration.packetName()),
                        message));
                CompletionStage<?> stage = result instanceof CompletionStage<?> completionStage
                    ? completionStage
                    : CompletableFuture.completedFuture(result);
                return stage.thenApply(reply -> Optional.of(serializer.serialize(reply)));
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor request handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        @Override
        public void close() {
            try {
                awaitClosing(entrySpot.onClosingAsync());
            } finally {
                if (pendingActorHeader != null) {
                    pendingActorHeader.close();
                    pendingActorHeader = null;
                }
                context.closeTimers();
                backendSpot.close();
            }
        }
    }

    private final class DefaultSpotContext implements ZLinkSpotContext {
        private static final CancellationToken NONE = () -> false;
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotOutbound outbound;
        private final List<ManagedTimer> timers = new ArrayList<>();
        private final List<Class<?>> handlerTypes = new ArrayList<>();
        private final List<Class<?>> packetHandlerTypes = new ArrayList<>();
        private final List<SpotSubscriptionRegistration> subscriptionHandlerTypes = new ArrayList<>();
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkSpot spot;

        DefaultSpotContext(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.outbound = new DefaultSpotOutbound(nodeRid, backendSpot);
        }

        void setSpot(ZLinkSpot spot) {
            this.spot = spot;
        }

        @Override
        public RoutingId spotRid() {
            return backendSpot.routingId();
        }

        @Override
        public RoutingId nodeRid() {
            return nodeRid;
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return outbound;
        }

        @Override
        public ZLinkSpotHandlerRegistry handlers() {
            return new ZLinkSpotHandlerRegistry() {
                @Override
                public void addHandler(Class<?> handlerType) {
                    ensureRegistrationOpen();
                    handlerTypes.add(requireHandlerType(handlerType));
                }

                @Override
                public void addPacket(Class<?> handlerType) {
                    ensureRegistrationOpen();
                    packetHandlerTypes.add(requireHandlerType(handlerType));
                }

                @Override
                public void addSubscribe(String topic, Class<?> handlerType) {
                    ensureRegistrationOpen();
                    subscriptionHandlerTypes.add(new SpotSubscriptionRegistration(
                        requireTopic(topic),
                        requireHandlerType(handlerType)));
                }
            };
        }

        @Override
        public CompletionStage<Void> leaveActorAsync(ZLinkActor actor) {
            if (actor == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor is required"));
            }
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor leave"));
            }
            actorRuntime.markLeft(actor);
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.LEAVE_SPOT);
            return notifySpotActorLifecycle(actor, result, actorLeftHandlers);
        }

        @Override
        public CompletionStage<ZLinkTimer> addTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            if (name == null || name.isBlank()) {
                throw new ZLinkConfigurationException("timer name is required");
            }
            if (period == null || period.isNegative() || period.isZero()) {
                throw new ZLinkConfigurationException("timer period must be positive");
            }
            ManagedTimer timer = new ManagedTimer(name, period, handlerType);
            timers.add(timer);
            timer.start();
            return CompletableFuture.completedFuture(timer);
        }

        void closeTimers() {
            timers.forEach(ManagedTimer::close);
            timers.clear();
        }

        void closeRegistration() {
            registrationOpen = false;
            for (Class<?> handlerType : handlerTypes) {
                registerConfiguredSpotHandler(handlerType, spot.getClass(), packetHandlers);
            }
            for (Class<?> handlerType : packetHandlerTypes) {
                SpotPacketHandlerRegistration registration =
                    createSpotPacketRegistration(handlerType, spot.getClass());
                if (packetHandlers.putIfAbsent(registration.packetName(), registration) != null) {
                    throw new ZLinkConfigurationException(
                        "duplicate SPOT packet handler packet: " + registration.packetName());
                }
            }
            for (SpotSubscriptionRegistration subscription : subscriptionHandlerTypes) {
                SpotSubscriptionHandlerRegistration registration =
                    createSpotSubscriptionRegistration(
                        subscription.topic(),
                        subscription.handlerType(),
                        spot.getClass());
                subscriptionHandlers
                    .computeIfAbsent(subscription.topic(), ignored -> new ArrayList<>())
                    .add(registration);
            }
        }

        void bindSubscriptions(ZLinkBackendSpot backendSpot) {
            for (String topic : subscriptionHandlers.keySet()) {
                backendSpot.setSubscription(topic);
            }
        }

        SpotPacketHandlerRegistration packetHandler(String packetName) {
            return packetHandlers.get(packetName);
        }

        List<SpotSubscriptionHandlerRegistration> subscriptionHandlers(String topic) {
            return subscriptionHandlers.getOrDefault(topic, List.of());
        }

        private void ensureRegistrationOpen() {
            if (!registrationOpen) {
                throw new ZLinkConfigurationException(
                    "SPOT handler registration is only allowed while configure is running");
            }
        }

        private final class ManagedTimer implements ZLinkTimer {
            private final String name;
            private final Duration period;
            private final Class<?> handlerType;
            private final ZLinkAsyncSerialQueue dispatchQueue = new ZLinkAsyncSerialQueue();
            private final Instant startedAt = Instant.now();
            private long tickIndex;
            private volatile boolean disposed;
            private ScheduledFuture<?> future;

            ManagedTimer(String name, Duration period, Class<?> handlerType) {
                this.name = name;
                this.period = period;
                this.handlerType = handlerType;
            }

            void start() {
                future = timerExecutor.scheduleAtFixedRate(
                    this::dispatch,
                    period.toNanos(),
                    period.toNanos(),
                    TimeUnit.NANOSECONDS);
            }

            private void dispatch() {
                if (disposed) {
                    return;
                }
                long index = ++tickIndex;
                Instant now = Instant.now();
                ZLinkTimerTick tick = new ZLinkTimerTick(
                    name,
                    index,
                    index,
                    period,
                    startedAt.plusNanos(period.toNanos() * index),
                    now,
                    Duration.ofNanos(period.toNanos() * index),
                    Duration.between(startedAt, now),
                    Duration.between(startedAt.plusNanos(period.toNanos() * index), now),
                    0);
                dispatchQueue.enqueue(() ->
                    disposed
                        ? CompletableFuture.completedFuture(null)
                        : withCurrentOutbound(DefaultSpotContext.this.outbound, () ->
                            invokeTimerHandler(handlerType, spot, tick)));
            }

            @Override
            public boolean isDisposed() {
                return disposed;
            }

            @Override
            public CompletionStage<Void> cancelAsync() {
                close();
                return CompletableFuture.completedFuture(null);
            }

            @Override
            public void close() {
                disposed = true;
                if (future != null) {
                    future.cancel(false);
                }
            }
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeTimerHandler(Class<?> handlerType, ZLinkSpot spot, ZLinkTimerTick tick) {
        try {
            Object handler = handlerFactory.create(handlerType);
            if (handler instanceof ZLinkSpotTimerHandler timerHandler) {
                return timerHandler.handleAsync(spot, tick);
            }
            return CompletableFuture.completedFuture(null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to create timer handler: " + handlerType.getName(),
                ex));
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeSpotPacketHandler(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload) {
        Object message = serializer.deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            if (registration.handlerMethod() != null) {
                Object result = registration.handlerMethod().invoke(handler, spot, message);
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            }
            return ((ZLinkSpotPacketHandler) handler)
                .handleAsync(spot, message)
                .thenApply(ignored -> null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT packet handler: "
                    + registration.handlerType().getName(),
                ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Message> invokeSpotRequestHandler(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload) {
        Object message = serializer.deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object result = registration.handlerMethod() != null
                ? registration.handlerMethod().invoke(handler, spot, message)
                : ((ZLinkSpotRequestHandler) handler).handleAsync(spot, message);
            CompletionStage<?> stage = result instanceof CompletionStage<?> completion
                ? completion
                : CompletableFuture.completedFuture(result);
            return stage.thenApply(serializer::serialize);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT request handler: "
                    + registration.handlerType().getName(),
                ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeSpotSubscriptionHandler(
        SpotSubscriptionHandlerRegistration registration,
        Object spot,
        Message payload) {
        Object message = serializer.deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            if (registration.handlerMethod() != null) {
                Object result = registration.handlerMethod().invoke(handler, spot, message);
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            }
            return ((ZLinkSpotSubscriptionHandler) handler)
                .handleAsync(spot, message)
                .thenApply(ignored -> null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT subscription handler: "
                    + registration.handlerType().getName(),
                ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private final class DefaultSpotOutbound implements ZLinkSpotOutbound {
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;

        DefaultSpotOutbound(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
        }

        @Override
        public <TMessage> ZLinkSendCall sendToSpot(RoutingId spotRid, TMessage message) {
            requireRoutingId(spotRid);
            if (channels != null && !egressChannels.isEmpty()) {
                if (egressChannels.size() > 1) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT outbound requires a single egress channel until remote address resolution is implemented");
                }
                return new EgressSpotSendCall(
                    channels,
                    egressChannels.get(0),
                    spotRid,
                    serializer.serialize(message),
                    Optional.of(defaultPacketName(message)));
            }
            return new SpotToSpotSendCall(
                backendSpot,
                nodeRid,
                spotRid,
                serializer.serialize(message),
                Optional.of(defaultPacketName(message)));
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToSpot(RoutingId spotRid, TMessage request) {
            requireRoutingId(spotRid);
            if (channels != null && !egressChannels.isEmpty()) {
                if (egressChannels.size() > 1) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT outbound requires a single egress channel until remote address resolution is implemented");
                }
                return new EgressSpotRequestCall(
                    channels,
                    egressChannels.get(0),
                    spotRid,
                    serializer.serialize(request),
                    Optional.of(defaultPacketName(request)),
                    defaultTimeout);
            }
            return new SpotToSpotRequestCall(
                backendSpot,
                nodeRid,
                spotRid,
                serializer.serialize(request),
                Optional.of(defaultPacketName(request)),
                defaultTimeout);
        }

        @Override
        public <TEvent> ZLinkPublishCall publish(String topic, TEvent message) {
            return new SpotPublishCall(
                backendSpot,
                topic,
                serializer.serialize(message),
                Optional.of(defaultPacketName(message)));
        }

        @Override
        public <TMessage> ZLinkSendCall sendToChannel(String channelName, TMessage message) {
            return new SpotChannelSendCall(
                backendSpot,
                channelName,
                serializer.serialize(message),
                Optional.of(defaultPacketName(message)));
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToChannel(String channelName, TMessage request) {
            return new SpotChannelRequestCall(
                backendSpot,
                channelName,
                serializer.serialize(request),
                Optional.of(defaultPacketName(request)),
                defaultTimeout);
        }
    }

    private record EgressSpotSendCall(
        ZLinkChannelRuntime channels,
        String egressChannelName,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) implements ZLinkSendCall {
        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new EgressSpotSendCall(
                channels,
                egressChannelName,
                spotRid,
                payload,
                Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            List<Message> spotParts = parts(packetName, payload);
            try {
                return channels.sendToSpotViaEgressChannel(
                    egressChannelName,
                    spotRid,
                    spotParts);
            } finally {
                spotParts.forEach(Message::close);
            }
        }
    }

    private final class EgressSpotRequestCall implements ZLinkRequestCall {
        private final ZLinkChannelRuntime channels;
        private final String egressChannelName;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        EgressSpotRequestCall(
            ZLinkChannelRuntime channels,
            String egressChannelName,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.channels = channels;
            this.egressChannelName = egressChannelName;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new EgressSpotRequestCall(
                channels,
                egressChannelName,
                spotRid,
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
            return new EgressSpotRequestCall(
                channels,
                egressChannelName,
                spotRid,
                payload,
                packetName,
                timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            List<Message> spotParts = parts(packetName, payload);
            try {
                return channels.requestToSpotViaEgressChannel(
                    egressChannelName,
                    spotRid,
                    spotParts,
                    timeout)
                    .thenApply(replyParts -> {
                        Message emptyReply = null;
                        try {
                            Message firstReply = replyParts.isEmpty()
                                ? (emptyReply = Message.from(new byte[0]))
                                : replyParts.get(0);
                            return serializer.deserialize(firstReply, replyType);
                        } finally {
                            if (emptyReply != null) {
                                emptyReply.close();
                            }
                            replyParts.forEach(Message::close);
                        }
                    });
            } finally {
                spotParts.forEach(Message::close);
            }
        }
    }

    private final class AmbientSpotOutbound implements ZLinkSpotOutbound {
        @Override
        public <TMessage> ZLinkSendCall sendToSpot(RoutingId spotRid, TMessage message) {
            return requireCurrentOutbound().sendToSpot(spotRid, message);
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToSpot(
            RoutingId spotRid,
            TMessage request) {
            return requireCurrentOutbound().requestToSpot(spotRid, request);
        }

        @Override
        public <TEvent> ZLinkPublishCall publish(String topic, TEvent message) {
            return requireCurrentOutbound().publish(topic, message);
        }

        @Override
        public <TMessage> ZLinkSendCall sendToChannel(
            String channelName,
            TMessage message) {
            return requireCurrentOutbound().sendToChannel(channelName, message);
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToChannel(
            String channelName,
            TMessage request) {
            return requireCurrentOutbound().requestToChannel(channelName, request);
        }
    }

    private final class DefaultSpotPublisherClient implements ZLinkSpotPublisherClient {
        @Override
        public <TEvent> ZLinkPublishCall publishSpot(
            String channelName,
            String topic,
            TEvent message) {
            if (channelName == null || channelName.isBlank()) {
                throw new ZLinkConfigurationException("SPOT publisher channel name is required");
            }
            if (topic == null || topic.isBlank()) {
                throw new ZLinkConfigurationException("SPOT publish topic is required");
            }
            if (!publisherNodesByChannel.containsKey(channelName)) {
                throw new ZLinkConfigurationException(
                    "SPOT publisher client is not configured: " + channelName);
            }
            return new ExternalSpotPublishCall(
                channelName,
                topic,
                serializer.serialize(message),
                Optional.of(defaultPacketName(message)));
        }
    }

    private final class ExternalSpotPublishCall implements ZLinkPublishCall {
        private final String channelName;
        private final String topic;
        private final Message payload;
        private final Optional<String> packetName;

        private ExternalSpotPublishCall(
            String channelName,
            String topic,
            Message payload,
            Optional<String> packetName) {
            this.channelName = channelName;
            this.topic = topic;
            this.payload = payload;
            this.packetName = packetName;
        }

        @Override
        public ZLinkPublishCall packetName(String packetName) {
            return new ExternalSpotPublishCall(
                channelName,
                topic,
                payload,
                Optional.of(packetName));
        }

        @Override
        public ZLinkPublishCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    publisherSpot(channelName).publish(topic, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private String resolveRouterChannelId(String configuredRouterChannelId) {
        if (configuredRouterChannelId != null && !configuredRouterChannelId.isBlank()) {
            return configuredRouterChannelId;
        }
        if (routeMeshChannels.size() == 1) {
            return routeMeshChannels.get(0);
        }
        throw new ZLinkConfigurationException(
            "registry SPOT remote addresses require a single route mesh egress channel or an explicit routerChannelId");
    }

    private ZLinkBackendDiscovery resolveSpotDiscovery(String namespaceName) {
        if (namespaceName != null) {
            ZLinkBackendDiscovery discovery = spotDiscoveriesByMesh.get(namespaceName);
            if (discovery != null) {
                return discovery;
            }
        }
        if (spotDiscoveriesByMesh.size() == 1) {
            return spotDiscoveriesByMesh.values().iterator().next();
        }
        throw new ZLinkConfigurationException(
            "registry SPOT remote addresses require a unique Spot mesh discovery");
    }

    private record SpotToSpotSendCall(
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) implements ZLinkSendCall {
        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new SpotToSpotSendCall(
                spot,
                targetNodeRid,
                spotRid,
                payload,
                Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.sendToSpot(targetNodeRid, spotRid, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private final class SpotToSpotRequestCall implements ZLinkRequestCall {
        private final ZLinkBackendSpot spot;
        private final RoutingId targetNodeRid;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private SpotToSpotRequestCall(
            ZLinkBackendSpot spot,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.spot = spot;
            this.targetNodeRid = targetNodeRid;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new SpotToSpotRequestCall(
                spot,
                targetNodeRid,
                spotRid,
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
            return new SpotToSpotRequestCall(
                spot,
                targetNodeRid,
                spotRid,
                payload,
                packetName,
                timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            List<Message> requestParts = parts(packetName, payload);
            try {
                spot.requestToSpot(targetNodeRid, spotRid, requestParts, reply -> {
                    Message emptyReply = null;
                    try {
                        Message firstReply = reply.parts().isEmpty()
                            ? (emptyReply = Message.from(new byte[0]))
                            : reply.parts().get(0);
                        result.complete(serializer.deserialize(firstReply, replyType));
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        if (emptyReply != null) {
                            emptyReply.close();
                        }
                        reply.parts().forEach(Message::close);
                    }
                }, SendFlags.NONE, timeout);
            } catch (RuntimeException ex) {
                result.completeExceptionally(ex);
            } finally {
                requestParts.forEach(Message::close);
            }
            return result;
        }
    }

    private record SpotChannelSendCall(
        ZLinkBackendSpot spot,
        String channelName,
        Message payload,
        Optional<String> packetName) implements ZLinkSendCall {
        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new SpotChannelSendCall(spot, channelName, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.sendToChannel(channelName, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private final class SpotChannelRequestCall implements ZLinkRequestCall {
        private final ZLinkBackendSpot spot;
        private final String channelName;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private SpotChannelRequestCall(
            ZLinkBackendSpot spot,
            String channelName,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.spot = spot;
            this.channelName = channelName;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new SpotChannelRequestCall(spot, channelName, payload, Optional.of(packetName), timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new SpotChannelRequestCall(spot, channelName, payload, packetName, timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            List<Message> requestParts = parts(packetName, payload);
            result.whenComplete((ignored, error) -> requestParts.forEach(Message::close));
            submitSpotChannelRequestWithRetry(
                spot,
                channelName,
                requestParts,
                timeout,
                reply -> {
                    Message emptyReply = null;
                    try {
                        Message firstReply = reply.parts().isEmpty()
                            ? (emptyReply = Message.from(new byte[0]))
                            : reply.parts().get(0);
                        result.complete(serializer.deserialize(firstReply, replyType));
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        if (emptyReply != null) {
                            emptyReply.close();
                        }
                        reply.parts().forEach(Message::close);
                    }
                },
                result);
            return result;
        }
    }

    private void submitSpotChannelRequestWithRetry(
        ZLinkBackendSpot spot,
        String channelName,
        List<Message> requestParts,
        Duration timeout,
        ZLinkBackendRequestCallback callback,
        CompletableFuture<?> result) {
        long timeoutNanos = timeout == null || timeout.isZero()
            ? defaultTimeout.toNanos()
            : timeout.toNanos();
        long deadline = System.nanoTime() + timeoutNanos;
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                try {
                    boolean submitted = spot.requestToChannel(
                        channelName,
                        requestParts,
                        callback,
                        SendFlags.DONT_WAIT,
                        timeout);
                    if (submitted) {
                        return;
                    }
                    if (System.nanoTime() >= deadline) {
                        result.completeExceptionally(new TimeoutException(
                            "SPOT channel request was not ready before timeout: "
                                + channelName));
                        return;
                    }
                    timerExecutor.schedule(this, 10, TimeUnit.MILLISECONDS);
                } catch (ZlinkSubmitException ex) {
                    if (ex.getResult() != SubmitResult.NOT_CONNECTED
                        && ex.getResult() != SubmitResult.BACKPRESSURED) {
                        result.completeExceptionally(ex);
                        return;
                    }
                    if (System.nanoTime() >= deadline) {
                        result.completeExceptionally(new TimeoutException(
                            "SPOT channel request was not ready before timeout: "
                                + channelName));
                        return;
                    }
                    timerExecutor.schedule(this, 10, TimeUnit.MILLISECONDS);
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                }
            }
        }
        new Attempt().run();
    }

    private CompletionStage<Void> sendActorBoundSessionWithRetry(
        ZLinkBackendSpotNode node,
        ZLinkBackendActorRef actor,
        String actorId,
        byte[] frameBytes,
        String failureMessage) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        long deadline = System.nanoTime() + defaultTimeout.toNanos();
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                if (closing) {
                    result.complete(null);
                    return;
                }
                try (Message frame = Message.from(frameBytes)) {
                    if (node.sendActorBoundSession(actor, List.of(frame), SendFlags.DONT_WAIT)) {
                        result.complete(null);
                        return;
                    }
                } catch (ZlinkSubmitException ex) {
                    if (ex.getResult() != SubmitResult.NOT_CONNECTED) {
                        result.completeExceptionally(ex);
                        return;
                    }
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                    return;
                }
                if (System.nanoTime() >= deadline) {
                    result.completeExceptionally(new ZLinkConfigurationException(
                        failureMessage + ": " + actorId));
                    return;
                }
                ACTOR_SESSION_REPLY_RETRY_EXECUTOR.schedule(this, 25, TimeUnit.MILLISECONDS);
            }
        }
        ACTOR_SESSION_REPLY_RETRY_EXECUTOR.execute(new Attempt());
        return result;
    }

    private record SpotDiscoveryBinding(
        String meshName,
        ZLinkBackendSpotNode node,
        ZLinkBackendDiscovery discovery,
        boolean routerEnabled,
        boolean pubSubEnabled,
        String routerBind,
        String pubBind) {
    }

    private record ActorDispatchReply(Message message, boolean streamFrame) {
    }

    private static ZLinkBackendActorReceived copyActorReceived(
        ZLinkBackendActorReceived received) {
        return new ZLinkBackendActorReceived(
            received.actor(),
            received.sourceNodeRid(),
            received.sourceSessionRid(),
            received.requestSeq(),
            received.flags(),
            Message.from(received.message()),
            received.hasMore());
    }

    private static String errorMessage(Throwable error) {
        Throwable current = error;
        while (current instanceof CompletionException && current.getCause() != null) {
            current = current.getCause();
        }
        return current.getMessage() == null
            ? current.getClass().getName()
            : current.getMessage();
    }

    private record SpotPublishCall(
        ZLinkBackendSpot spot,
        String topic,
        Message payload,
        Optional<String> packetName) implements ZLinkPublishCall {
        @Override
        public ZLinkPublishCall packetName(String packetName) {
            return new SpotPublishCall(spot, topic, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkPublishCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.publish(topic, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private static List<Message> parts(Optional<String> packetName, Message payload) {
        return packetName
            .map(name -> List.of(Message.from(name.getBytes(StandardCharsets.UTF_8)), payload))
            .orElseGet(() -> List.of(payload));
    }

    private static String defaultPacketName(Object message) {
        if (message == null) {
            return "Null";
        }
        return resolvePacketName(message.getClass());
    }

    private static ParsedPacket parsePacket(List<Message> parts) {
        if (parts.size() >= 2) {
            return new ParsedPacket(parts.get(0).toUtf8String(), parts.get(1));
        }
        return new ParsedPacket("", parts.get(0));
    }

    private record ParsedPacket(String packetName, Message payload) {
    }

    private record SpotSubscriptionRegistration(String topic, Class<?> handlerType) {
    }

    private record SpotPacketHandlerRegistration(
        Class<?> handlerType,
        Method handlerMethod,
        Class<?> spotType,
        Class<?> messageType,
        Class<?> replyType,
        String packetName,
        boolean request) {
    }

    private record SpotSubscriptionHandlerRegistration(
        String topic,
        Class<?> handlerType,
        Method handlerMethod,
        Class<?> spotType,
        Class<?> messageType,
        String packetName) {
    }

    private void registerConfiguredSpotHandler(
        Class<?> handlerType,
        Class<?> expectedSpotType,
        Map<String, SpotPacketHandlerRegistration> packetHandlers) {
        boolean matched = false;
        if (isSpotPacketHandlerType(handlerType)) {
            SpotPacketHandlerRegistration registration =
                createSpotPacketRegistration(handlerType, expectedSpotType);
            addConfiguredPacketHandler(packetHandlers, registration);
            matched = true;
        }
        for (Method method : handlerType.getMethods()) {
            if (method.getAnnotation(ZLinkSpotActorJoin.class) != null) {
                addConfiguredActorJoinHandler(handlerType, method);
                matched = true;
            }
            if (method.getAnnotation(ZLinkSpotActorSend.class) != null) {
                addConfiguredActorPacketHandler(handlerType, method, ZLinkScannedHandlerKind.ACTOR_SEND);
                matched = true;
            }
            if (method.getAnnotation(ZLinkSpotActorRequest.class) != null) {
                addConfiguredActorPacketHandler(handlerType, method, ZLinkScannedHandlerKind.ACTOR_REQUEST);
                matched = true;
            }
            if (method.getAnnotation(ZLinkSpotPostActorJoined.class) != null) {
                addConfiguredActorLifecycleHandler(handlerType, method, actorJoinedHandlers,
                    ZLinkScannedHandlerKind.ACTOR_JOINED);
                matched = true;
            }
            if (method.getAnnotation(ZLinkSpotActorLeft.class) != null) {
                addConfiguredActorLifecycleHandler(handlerType, method, actorLeftHandlers,
                    ZLinkScannedHandlerKind.ACTOR_LEFT);
                matched = true;
            }
            if (method.getAnnotation(ZLinkSpotActorDisconnected.class) != null) {
                ActorLifecycleShape shape = actorDisconnectedHandlerShape(handlerType, method);
                addConfiguredActorLifecycleHandler(
                    new SpotActorLifecycleHandlerRegistration(
                        handlerType,
                        method,
                        shape.spotType(),
                        shape.actorType(),
                        ZLinkScannedHandlerKind.ACTOR_DISCONNECTED),
                    actorDisconnectedHandlers);
                matched = true;
            }
        }
        if (!matched) {
            throw new ZLinkConfigurationException(
                "SPOT handler must declare a SPOT or actor handler contract: "
                    + handlerType.getName());
        }
    }

    private static boolean isSpotPacketHandlerType(Class<?> handlerType) {
        if (findInterface(handlerType, ZLinkSpotPacketHandler.class) != null
            || findInterface(handlerType, ZLinkSpotRequestHandler.class) != null) {
            return true;
        }
        for (Method method : handlerType.getMethods()) {
            if (method.getAnnotation(ZLinkSpotRequest.class) != null) {
                return true;
            }
        }
        return false;
    }

    private static void addConfiguredPacketHandler(
        Map<String, SpotPacketHandlerRegistration> handlers,
        SpotPacketHandlerRegistration registration) {
        SpotPacketHandlerRegistration previous =
            handlers.putIfAbsent(registration.packetName(), registration);
        if (previous != null && previous.handlerType() != registration.handlerType()) {
            throw new ZLinkConfigurationException(
                "duplicate SPOT packet handler packet: " + registration.packetName());
        }
    }

    private void addConfiguredActorJoinHandler(Class<?> handlerType, Method method) {
        ActorMessageShape shape = actorJoinHandlerShape(handlerType, method);
        Class<?> replyType = resolveReplyType(handlerType, method);
        String packetName = resolvePacketName(shape.messageType());
        SpotActorJoinHandlerRegistration registration =
            new SpotActorJoinHandlerRegistration(
                handlerType,
                method,
                shape.actorType(),
                shape.messageType(),
                replyType,
                packetName);
        SpotActorJoinHandlerRegistration previous =
            actorJoinHandlers.putIfAbsent(packetName, registration);
        if (previous != null && previous.handlerType() != handlerType) {
            throw new ZLinkConfigurationException(
                "duplicate Spot actor join handler packet: " + packetName);
        }
    }

    private void addConfiguredActorPacketHandler(
        Class<?> handlerType,
        Method method,
        ZLinkScannedHandlerKind kind) {
        ActorMessageShape shape = actorPacketHandlerShape(
            handlerType,
            method,
            kind == ZLinkScannedHandlerKind.ACTOR_REQUEST
                ? ZLinkSpotActorRequestContext.class
                : ZLinkSpotActorSendContext.class);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.ACTOR_REQUEST
            ? resolveReplyType(handlerType, method)
            : Void.class;
        String explicitPacketName = kind == ZLinkScannedHandlerKind.ACTOR_REQUEST
            ? method.getAnnotation(ZLinkSpotActorRequest.class).packetName()
            : method.getAnnotation(ZLinkSpotActorSend.class).packetName();
        String packetName = resolvePacketName(shape.messageType(), explicitPacketName);
        SpotActorPacketHandlerRegistration registration =
            new SpotActorPacketHandlerRegistration(
                handlerType,
                method,
                shape.actorType(),
                shape.messageType(),
                replyType,
                packetName,
                kind);
        SpotActorPacketHandlerRegistration previous =
            actorPacketHandlers.putIfAbsent(packetName, registration);
        if (previous != null && previous.handlerType() != handlerType) {
            throw new ZLinkConfigurationException(
                "duplicate Spot actor packet handler packet: " + packetName);
        }
    }

    private static void addConfiguredActorLifecycleHandler(
        Class<?> handlerType,
        Method method,
        List<SpotActorLifecycleHandlerRegistration> registrations,
        ZLinkScannedHandlerKind kind) {
        ActorLifecycleShape shape = actorLifecycleHandlerShape(handlerType, method);
        addConfiguredActorLifecycleHandler(
            new SpotActorLifecycleHandlerRegistration(
                handlerType,
                method,
                shape.spotType(),
                shape.actorType(),
                kind),
            registrations);
    }

    private static void addConfiguredActorLifecycleHandler(
        SpotActorLifecycleHandlerRegistration registration,
        List<SpotActorLifecycleHandlerRegistration> registrations) {
        for (SpotActorLifecycleHandlerRegistration previous : registrations) {
            if (previous.handlerType() == registration.handlerType()
                && previous.handlerMethod().equals(registration.handlerMethod())
                && previous.kind() == registration.kind()) {
                return;
            }
        }
        registrations.add(registration);
    }

    private static SpotPacketHandlerRegistration createSpotPacketRegistration(
        Class<?> handlerType,
        Class<?> expectedSpotType) {
        ParameterizedType packet = findInterface(handlerType, ZLinkSpotPacketHandler.class);
        if (packet != null) {
            Type[] arguments = packet.getActualTypeArguments();
            Class<?> spotType = requireClassArgument(handlerType, arguments[0]);
            requireExactSpotType(handlerType, expectedSpotType, spotType);
            Class<?> messageType = requireClassArgument(handlerType, arguments[1]);
            return new SpotPacketHandlerRegistration(
                handlerType, null, spotType, messageType, Void.class,
                resolvePacketName(messageType), false);
        }

        ParameterizedType request = findInterface(handlerType, ZLinkSpotRequestHandler.class);
        if (request != null) {
            Type[] arguments = request.getActualTypeArguments();
            Class<?> spotType = requireClassArgument(handlerType, arguments[0]);
            requireExactSpotType(handlerType, expectedSpotType, spotType);
            Class<?> messageType = requireClassArgument(handlerType, arguments[1]);
            Class<?> replyType = requireClassArgument(handlerType, arguments[2]);
            return new SpotPacketHandlerRegistration(
                handlerType, null, spotType, messageType, replyType,
                resolvePacketName(messageType), true);
        }

        for (Method method : handlerType.getMethods()) {
            ZLinkSpotRequest annotation = method.getAnnotation(ZLinkSpotRequest.class);
            if (annotation == null) {
                continue;
            }
            Class<?>[] parameters = method.getParameterTypes();
            if (parameters.length != 2) {
                throw new ZLinkConfigurationException(
                    "SPOT request handler method must have spot and request parameters: "
                        + handlerType.getName() + "." + method.getName());
            }
            requireExactSpotType(handlerType, expectedSpotType, parameters[0]);
            return new SpotPacketHandlerRegistration(
                handlerType, method, parameters[0], parameters[1],
                resolveReplyType(handlerType, method),
                resolvePacketName(parameters[1], annotation.packetName()), true);
        }

        throw new ZLinkConfigurationException(
            "SPOT packet handler must implement ZLinkSpotPacketHandler or ZLinkSpotRequestHandler: "
                + handlerType.getName());
    }

    private static SpotSubscriptionHandlerRegistration createSpotSubscriptionRegistration(
        String topic,
        Class<?> handlerType,
        Class<?> expectedSpotType) {
        ParameterizedType subscription = findInterface(handlerType, ZLinkSpotSubscriptionHandler.class);
        if (subscription != null) {
            Type[] arguments = subscription.getActualTypeArguments();
            Class<?> spotType = requireClassArgument(handlerType, arguments[0]);
            requireExactSpotType(handlerType, expectedSpotType, spotType);
            Class<?> messageType = requireClassArgument(handlerType, arguments[1]);
            return new SpotSubscriptionHandlerRegistration(
                topic, handlerType, null, spotType, messageType, resolvePacketName(messageType));
        }

        for (Method method : handlerType.getMethods()) {
            ZLinkSpotSubscription annotation = method.getAnnotation(ZLinkSpotSubscription.class);
            if (annotation == null) {
                continue;
            }
            Class<?>[] parameters = method.getParameterTypes();
            if (parameters.length != 2) {
                throw new ZLinkConfigurationException(
                    "SPOT subscription handler method must have spot and event parameters: "
                        + handlerType.getName() + "." + method.getName());
            }
            requireExactSpotType(handlerType, expectedSpotType, parameters[0]);
            return new SpotSubscriptionHandlerRegistration(
                topic, handlerType, method, parameters[0], parameters[1],
                resolvePacketName(parameters[1]));
        }

        throw new ZLinkConfigurationException(
            "SPOT subscription handler must implement ZLinkSpotSubscriptionHandler: "
                + handlerType.getName());
    }

    private static void requireExactSpotType(
        Class<?> handlerType,
        Class<?> expectedSpotType,
        Class<?> actualSpotType) {
        if (actualSpotType != expectedSpotType) {
            throw new ZLinkConfigurationException(
                "SPOT handler " + handlerType.getName()
                    + " targets " + actualSpotType.getName()
                    + " but expected " + expectedSpotType.getName());
        }
    }

    private static ActorLifecycleShape actorDisconnectedHandlerShape(Class<?> handlerType, Method method) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 1) {
            return new ActorLifecycleShape(null, parameters[0]);
        }
        if (parameters.length == 3 && parameters[2] == CancellationToken.class) {
            return new ActorLifecycleShape(parameters[0], parameters[1]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor disconnected handler method must have actor parameter or spot, actor, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private static ActorLifecycleShape actorLifecycleHandlerShape(Class<?> handlerType, Method method) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 2 && parameters[1] == ZLinkSpotActorChangeResult.class) {
            return new ActorLifecycleShape(null, parameters[0]);
        }
        if (parameters.length == 4
            && parameters[2] == ZLinkSpotActorChangeResult.class
            && parameters[3] == CancellationToken.class) {
            return new ActorLifecycleShape(parameters[0], parameters[1]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor lifecycle handler method must have actor/change or spot, actor, change, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    static boolean matchesActorLifecycleTarget(
        SpotActorLifecycleHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor) {
        return registration.actorType().isInstance(actor)
            && (registration.spotType() == null || registration.spotType().isInstance(spotSurface));
    }

    private static ActorMessageShape actorJoinHandlerShape(Class<?> handlerType, Method method) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 2) {
            return new ActorMessageShape(parameters[0], parameters[1]);
        }
        if (parameters.length == 4 && parameters[3] == CancellationToken.class) {
            return new ActorMessageShape(parameters[1], parameters[2]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor join handler method must have actor/request or spot, actor, request, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private static ActorMessageShape actorPacketHandlerShape(
        Class<?> handlerType,
        Method method,
        Class<? extends ZLinkHandlerContext> contextType) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 2) {
            return new ActorMessageShape(parameters[0], parameters[1]);
        }
        if (parameters.length == 5
            && parameters[2].isAssignableFrom(contextType)
            && parameters[4] == CancellationToken.class) {
            return new ActorMessageShape(parameters[1], parameters[3]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor packet handler method must have actor/message or spot, actor, context, message, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private record ActorMessageShape(Class<?> actorType, Class<?> messageType) {
    }

    private record ActorLifecycleShape(Class<?> spotType, Class<?> actorType) {
    }

    static Object[] actorJoinArguments(Method method, Object spot, ZLinkActor actor, Object message) {
        Class<?>[] parameterTypes = method.getParameterTypes();
        if (parameterTypes.length == 2) {
            return new Object[] {actor, message};
        }
        return new Object[] {spot, actor, message, NONE_CANCELLATION};
    }

    static Object[] actorPacketArguments(
        Method method,
        Object spot,
        ZLinkActor actor,
        ZLinkHandlerContext context,
        Object message) {
        Class<?>[] parameterTypes = method.getParameterTypes();
        if (parameterTypes.length == 2) {
            return new Object[] {actor, message};
        }
        return new Object[] {spot, actor, context, message, context.cancellationToken()};
    }

    static Object[] actorLifecycleArguments(
        Method method,
        Object spot,
        ZLinkActor actor,
        ZLinkSpotActorChangeResult result) {
        Class<?>[] parameterTypes = method.getParameterTypes();
        if (parameterTypes.length == 2) {
            return new Object[] {actor, result};
        }
        return new Object[] {spot, actor, result, NONE_CANCELLATION};
    }

    static Object[] actorDisconnectedArguments(Method method, Object spot, ZLinkActor actor) {
        Class<?>[] parameterTypes = method.getParameterTypes();
        if (parameterTypes.length == 1) {
            return new Object[] {actor};
        }
        return new Object[] {spot, actor, NONE_CANCELLATION};
    }

    private static final class DefaultSpotActorSendContext implements ZLinkSpotActorSendContext {
        private final String packetName;

        DefaultSpotActorSendContext(String packetName) {
            this.packetName = packetName;
        }

        @Override
        public Optional<String> channelName() {
            return Optional.empty();
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName);
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE_CANCELLATION;
        }
    }

    private static final class DefaultSpotActorRequestContext implements ZLinkSpotActorRequestContext {
        private final String packetName;

        DefaultSpotActorRequestContext(String packetName) {
            this.packetName = packetName;
        }

        @Override
        public Optional<String> channelName() {
            return Optional.empty();
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName);
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE_CANCELLATION;
        }
    }

    private static ParameterizedType findInterface(Class<?> type, Class<?> targetRawType) {
        for (Type interfaceType : type.getGenericInterfaces()) {
            ParameterizedType matched = matchInterface(interfaceType, targetRawType);
            if (matched != null) {
                return matched;
            }
        }
        Class<?> superclass = type.getSuperclass();
        return superclass == null || superclass == Object.class
            ? null
            : findInterface(superclass, targetRawType);
    }

    private static ParameterizedType matchInterface(Type interfaceType, Class<?> targetRawType) {
        if (interfaceType instanceof ParameterizedType parameterized
            && parameterized.getRawType() == targetRawType) {
            return parameterized;
        }
        if (interfaceType instanceof Class<?> raw) {
            return findInterface(raw, targetRawType);
        }
        return null;
    }

    private static Class<?> requireClassArgument(Class<?> handlerType, Type argument) {
        if (argument instanceof Class<?> klass) {
            return klass;
        }
        if (argument instanceof ParameterizedType parameterized
            && parameterized.getRawType() instanceof Class<?> raw) {
            return raw;
        }
        throw new ZLinkConfigurationException(
            "handler generic argument must resolve to a class: " + handlerType.getName());
    }

    private static Class<?> resolveReplyType(Class<?> handlerType, Method method) {
        Type returnType = method.getGenericReturnType();
        if (returnType instanceof ParameterizedType parameterized
            && parameterized.getRawType() == CompletionStage.class) {
            return requireClassArgument(handlerType, parameterized.getActualTypeArguments()[0]);
        }
        if (method.getReturnType() == Void.TYPE || method.getReturnType() == Void.class) {
            throw new ZLinkConfigurationException(
                "SPOT request handler method must return a reply: "
                    + handlerType.getName() + "." + method.getName());
        }
        return method.getReturnType();
    }

    private static String resolvePacketName(Class<?> messageType) {
        ZLinkPacket packet = messageType.getAnnotation(ZLinkPacket.class);
        return packet == null ? messageType.getSimpleName() : packet.value();
    }

    private static String resolvePacketName(Class<?> messageType, String explicitPacketName) {
        return explicitPacketName == null || explicitPacketName.isBlank()
            ? resolvePacketName(messageType)
            : explicitPacketName;
    }

    private static Class<?> requireHandlerType(Class<?> handlerType) {
        if (handlerType == null) {
            throw new ZLinkConfigurationException("SPOT handler type is required");
        }
        return handlerType;
    }

    private static String requireTopic(String topic) {
        if (topic == null || topic.isBlank()) {
            throw new ZLinkConfigurationException("SPOT subscription topic must not be empty");
        }
        return topic;
    }

    private final class SpotActivation implements AutoCloseable {
        private final ZLinkSpot spot;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotContext context;
        private ZLinkBackendActorReceived pendingActorHeader;

        SpotActivation(
            ZLinkSpot spot,
            ZLinkBackendSpot backendSpot,
            DefaultSpotContext context) {
            this.spot = spot;
            this.backendSpot = backendSpot;
            this.context = context;
        }

        void handleDispatchEvent(ZLinkBackendSpotDispatchInfo info) {
            if (closing) {
                return;
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                drainRoutes();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE) {
                drainSubscriptions();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE) {
                drainUnhandledActorJoins();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                dispatchActorMessages(info.actorMessages());
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE) {
                drainActorLifecycleEvents();
            }
            for (ZLinkBackendActorReceived actorMessage : info.actorMessages()) {
                actorMessage.close();
            }
        }

        private void drainRoutes() {
            while (true) {
                ZLinkBackendReceived received =
                    backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchRoute(received);
            }
        }

        private void dispatchRoute(ZLinkBackendReceived received) {
            if (received.parts().isEmpty()) {
                received.close();
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
            SpotPacketHandlerRegistration handler =
                context.packetHandler(packet.packetName());
            if (handler == null) {
                received.close();
                return;
            }
            if (received.requestSeq().isPresent()) {
                if (!handler.request()) {
                    received.close();
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotRequestHandler(handler, spot, payloadCopy)
                    .thenAccept(reply -> received.reply(List.of(reply)))
                    .whenComplete((ignored, error) -> {
                        payloadCopy.close();
                        received.close();
                    });
                return;
            }
            try (received) {
                if (handler.request()) {
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotPacketHandler(handler, spot, payloadCopy)
                    .whenComplete((ignored, error) -> payloadCopy.close());
            }
        }

        private void drainSubscriptions() {
            while (true) {
                ZLinkBackendTopicMessage received =
                    backendSpot.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchSubscription(received);
            }
        }

        private void dispatchSubscription(ZLinkBackendTopicMessage received) {
            try {
                if (received.parts().isEmpty()) {
                    return;
                }
                ParsedPacket packet = parsePacket(received.parts());
                for (SpotSubscriptionHandlerRegistration handler :
                    context.subscriptionHandlers(received.topic())) {
                    if (!handler.packetName().equals(packet.packetName())) {
                        continue;
                    }
                    Message payloadCopy = Message.from(packet.payload());
                    invokeSpotSubscriptionHandler(handler, spot, payloadCopy)
                        .whenComplete((ignored, error) -> payloadCopy.close());
                }
            } finally {
                received.parts().forEach(Message::close);
            }
        }

        private void drainActorLifecycleEvents() {
            while (true) {
                if (closing) {
                    return;
                }
                ZLinkBackendActorLifecycleEvent event =
                    backendSpot.recvActorLifecycle(ZLinkBackendRecvMode.DONT_WAIT);
                if (event == null) {
                    return;
                }
                if (actorRuntime == null) {
                    continue;
                }
                ZLinkBackendActorRef actorRef = event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
                    ? event.info().previousActor()
                    : event.info().currentActor();
                actorRuntime.localActor(actorRef.actorId())
                    .ifPresent(actor -> dispatchActorLifecycle(event, actorRef, actor));
            }
        }

        private void dispatchActorLifecycle(
            ZLinkBackendActorLifecycleEvent event,
            ZLinkBackendActorRef actorRef,
            ZLinkActor actor) {
            if (closing) {
                return;
            }
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT) {
                actorRuntime.markLeft(actor);
                ZLinkSpotActorChangeResult result =
                    new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.LEAVE_SPOT);
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> notifySpotActorLifecycle(spot, actor, result, actorLeftHandlers));
                return;
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            actorRuntime.markJoined(
                actor,
                actorRef,
                spotRid,
                spotSurfaceFor(spotRid) instanceof ZLinkSpot spot ? spot : null);
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            actorRuntime.submitActorDispatch(
                actor.actorId(),
                () -> notifySpotActorLifecycle(spot, actor, result, actorJoinedHandlers));
        }

        private void dispatchActorMessages(List<ZLinkBackendActorReceived> actorMessages) {
            int index = 0;
            while (index < actorMessages.size() || pendingActorHeader != null) {
                boolean pendingHeader = pendingActorHeader != null;
                ZLinkBackendActorReceived headerPart = pendingHeader
                    ? pendingActorHeader
                    : actorMessages.get(index++);
                ZLinkBackendActorReceived bodyPart =
                    headerPart.hasMore() && index < actorMessages.size()
                        ? actorMessages.get(index++)
                        : null;
                if (headerPart.hasMore() && bodyPart == null) {
                    if (!pendingHeader) {
                        pendingActorHeader = copyActorReceived(headerPart);
                    }
                    return;
                }
                if (pendingHeader) {
                    pendingActorHeader = null;
                }
                ActorPacketHeader packetHeader = decodeActorPacketHeader(headerPart);
                SpotActorPacketHandlerRegistration handler =
                    actorPacketHandlers.get(packetHeader.packetName());
                if (handler == null || actorRuntime == null) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                if (packetHeader.requestSeq().isPresent()
                    != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                Optional<ZLinkActor> localActor =
                    actorRuntime.localActor(headerPart.actor().actorId());
                if (localActor.isEmpty()) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                ZLinkActor actor = localActor.get();
                if (!handler.actorType().isInstance(actor)) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                ZLinkBackendActorReceived headerCopy = pendingHeader
                    ? headerPart
                    : copyActorReceived(headerPart);
                Message payloadCopy = bodyPart == null
                    ? Message.from(new byte[0])
                    : Message.from(bodyPart.message());
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> dispatchActorPacket(
                        handler,
                        actor,
                        packetHeader,
                        headerCopy,
                        payloadCopy));
            }
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            ZLinkActor actor,
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            actorRuntime.bindNativeSession(actor, primaryNode, headerPart.actor());
            CompletionStage<Optional<Message>> stage = withCurrentOutbound(
                context.outbound,
                () -> handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
                    ? invokeActorRequestHandler(handler, actor, payload)
                    : invokeActorSendHandler(handler, actor, payload)
                        .thenApply(ignored -> Optional.empty()));
            return stage.handle((reply, error) -> {
                    if (error != null) {
                        return Optional.of(new ActorDispatchReply(
                            encodeActorErrorFrame(packetHeader, error),
                            true));
                    }
                    return reply.map(message -> new ActorDispatchReply(message, false));
                })
                .thenCompose(reply -> {
                    if (reply.isEmpty()) {
                        return CompletableFuture.completedFuture(null);
                    }
                    byte[] frameBytes;
                    try (Message frame = reply.get().streamFrame()
                        ? reply.get().message()
                        : encodeActorReplyFrame(packetHeader, headerPart, reply.get().message())) {
                        frameBytes = frame.toByteArray();
                    }
                    return sendActorBoundSessionWithRetry(
                        primaryNode,
                        new ZLinkBackendActorRef(
                            headerPart.actor().nodeRid(),
                            actor.actorId(),
                            headerPart.actor().epoch()),
                        actor.actorId(),
                        frameBytes,
                        "user actor bound session reply failed");
                })
                .whenComplete((ignored, error) -> {
                    payload.close();
                    headerPart.close();
                });
        }

        private ActorPacketHeader decodeActorPacketHeader(ZLinkBackendActorReceived headerPart) {
            byte[] bytes = headerPart.message().toByteArray();
            try {
                return decodeStreamActorPacketHeader(bytes, headerPart.requestSeq());
            } catch (RuntimeException ignored) {
                return new ActorPacketHeader(
                    headerPart.message().toUtf8String(),
                    headerPart.requestSeq(),
                    false,
                    0);
            }
        }

        private ActorPacketHeader decodeStreamActorPacketHeader(
            byte[] bytes,
            Optional<Long> backendRequestSeq) {
            if (bytes.length < 4) {
                throw new IllegalArgumentException("actor STREAM header is too short");
            }
            ByteBuffer buffer = ByteBuffer.wrap(bytes);
            int kind = Byte.toUnsignedInt(buffer.get());
            int codec = Byte.toUnsignedInt(buffer.get());
            int flags = Byte.toUnsignedInt(buffer.get());
            Optional<Long> requestSeq = backendRequestSeq;
            if ((flags & 0x01) != 0) {
                if (buffer.remaining() < Long.BYTES) {
                    throw new IllegalArgumentException("actor STREAM header missing request sequence");
                }
                long decodedRequestSeq = buffer.getLong();
                if (decodedRequestSeq == 0) {
                    throw new IllegalArgumentException("actor STREAM request sequence is zero");
                }
                requestSeq = Optional.of(decodedRequestSeq);
            }
            if (buffer.remaining() < 1) {
                throw new IllegalArgumentException("actor STREAM header missing packet name");
            }
            int nameLength = Byte.toUnsignedInt(buffer.get());
            if (buffer.remaining() < nameLength) {
                throw new IllegalArgumentException("actor STREAM header packet name is truncated");
            }
            byte[] nameBytes = new byte[nameLength];
            buffer.get(nameBytes);
            if ((flags & 0x02) != 0) {
                if (buffer.remaining() < 2) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                int metadataLength = Short.toUnsignedInt(buffer.getShort());
                if (buffer.remaining() < metadataLength) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                buffer.position(buffer.position() + metadataLength);
            }
            if (buffer.hasRemaining()) {
                throw new IllegalArgumentException("actor STREAM header has trailing bytes");
            }
            if (kind != 1 && kind != 2) {
                throw new IllegalArgumentException("actor STREAM header is not dispatch kind");
            }
            return new ActorPacketHeader(
                new String(nameBytes, StandardCharsets.UTF_8),
                requestSeq,
                true,
                codec);
        }

        private Message encodeActorReplyFrame(
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived originalHeader,
            Message payload) {
            if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
                return Message.from(payload);
            }
            byte[] name = packetHeader.packetName().getBytes(StandardCharsets.UTF_8);
            ByteBuffer header = ByteBuffer.allocate(3 + Long.BYTES + 1 + name.length);
            header.put((byte) 3);
            header.put((byte) packetHeader.codec());
            header.put((byte) 0x01);
            header.putLong(packetHeader.requestSeq().get());
            header.put((byte) name.length);
            header.put(name);
            byte[] headerBytes = header.array();
            byte[] body = payload.toByteArray();
            ByteBuffer frame = ByteBuffer.allocate(6 + headerBytes.length + body.length);
            frame.putShort((short) headerBytes.length);
            frame.putInt(body.length);
            frame.put(headerBytes);
            frame.put(body);
            return Message.from(frame.array());
        }

        private Message encodeActorErrorFrame(
            ActorPacketHeader packetHeader,
            Throwable error) {
            byte[] body = errorMessage(error).getBytes(StandardCharsets.UTF_8);
            if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
                return Message.from(body);
            }
            byte[] name = packetHeader.packetName().getBytes(StandardCharsets.UTF_8);
            ByteBuffer header = ByteBuffer.allocate(3 + Long.BYTES + 1 + name.length);
            header.put((byte) 4);
            header.put((byte) 1);
            header.put((byte) 0x01);
            header.putLong(packetHeader.requestSeq().get());
            header.put((byte) name.length);
            header.put(name);
            byte[] headerBytes = header.array();
            ByteBuffer frame = ByteBuffer.allocate(6 + headerBytes.length + body.length);
            frame.putShort((short) headerBytes.length);
            frame.putInt(body.length);
            frame.put(headerBytes);
            frame.put(body);
            return Message.from(frame.array());
        }

        private record ActorPacketHeader(
            String packetName,
            Optional<Long> requestSeq,
            boolean streamHeader,
            int codec) {
        }

        private void drainUnhandledActorJoins() {
            while (true) {
                ZLinkBackendActorJoinRequest request =
                    backendSpot.recvActorJoin(ZLinkBackendRecvMode.DONT_WAIT);
                if (request == null) {
                    return;
                }
                try {
                    SpotActorJoinHandlerRegistration handler =
                        resolveActorJoinHandler(request);
                    if (handler == null) {
                        try (Message emptyReply = Message.from(new byte[0])) {
                            backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                        }
                        continue;
                    }
                    Message payload = actorJoinPayload(request);
                    withCurrentOutbound(context.outbound, () ->
                        invokeActorJoinHandler(handler, request, payload))
                        .whenComplete((reply, error) -> {
                            if (error != null) {
                                try (Message emptyReply = Message.from(new byte[0])) {
                                    backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                                }
                                return;
                            }
                            backendSpot.replyActorJoin(request, 0, List.of(reply));
                            reply.close();
                        });
                } finally {
                    request.parts().forEach(Message::close);
                }
            }
        }

        private SpotActorJoinHandlerRegistration resolveActorJoinHandler(
            ZLinkBackendActorJoinRequest request) {
            if (request.parts().isEmpty()) {
                return null;
            }
            if (request.parts().size() >= 2) {
                return actorJoinHandlers.get(request.parts().get(0).toUtf8String());
            }
            for (SpotActorJoinHandlerRegistration handler : actorJoinHandlers.values()) {
                if (handler.requestType() == Message.class
                    || handler.requestType() == byte[].class
                    || handler.requestType() == String.class) {
                    return handler;
                }
            }
            return null;
        }

        private Message actorJoinPayload(ZLinkBackendActorJoinRequest request) {
            return request.parts().size() >= 2 ? request.parts().get(1) : request.parts().get(0);
        }

        private CompletionStage<Message> invokeActorJoinHandler(
            SpotActorJoinHandlerRegistration registration,
            ZLinkBackendActorJoinRequest request,
            Message payload) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor join handler"));
            }
            return actorRuntime
                .getOrCreateLocalActor(
                    request.targetActor().actorId(),
                    registration.actorType())
                .thenCompose(actor -> {
                    if (actor.isEmpty()) {
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "Spot actor join target actor is not available: "
                                + request.targetActor().actorId()));
                    }
                    actorRuntime.markJoined(
                        actor.get(),
                        request.targetActor(),
                        backendSpot.routingId(),
                        spotFor(backendSpot.routingId()));
                    try {
                        Object handler = handlerFactory.create(registration.handlerType());
                        Object requestObject =
                            serializer.deserialize(payload, registration.requestType());
                        Object result = registration.handlerMethod().invoke(
                            handler,
                            actorJoinArguments(
                                registration.handlerMethod(),
                                spot,
                                actor.get(),
                                requestObject));
                        CompletionStage<?> stage =
                            result instanceof CompletionStage<?> completionStage
                                ? completionStage
                                : CompletableFuture.completedFuture(result);
                        return stage
                            .thenCompose(reply -> notifyActorJoined(actor.get())
                                .thenApply(ignored -> reply))
                            .whenComplete((reply, error) -> {
                                if (error != null) {
                                    actorRuntime.markLeft(actor.get());
                                }
                            })
                            .thenApply(serializer::serialize);
                    } catch (IllegalAccessException | InvocationTargetException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "failed to invoke Spot actor join handler: "
                                + registration.handlerType().getName()
                                + "."
                                + registration.handlerMethod().getName(),
                            ex instanceof InvocationTargetException invocation
                                ? invocation.getCause()
                                : ex));
                    } catch (RuntimeException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(ex);
                    }
                });
        }

        private CompletionStage<Void> notifyActorJoined(ZLinkActor actor) {
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            return notifyActorLifecycle(actor, result, actorJoinedHandlers);
        }

        private CompletionStage<Void> notifyActorLifecycle(
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result,
            List<SpotActorLifecycleHandlerRegistration> registrations) {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            for (SpotActorLifecycleHandlerRegistration registration : registrations) {
                if (!matchesActorLifecycleTarget(registration, spot, actor)) {
                    continue;
                }
                tail = tail.thenCompose(ignored ->
                    invokeActorJoinedHandler(registration, spot, actor, result));
            }
            return tail;
        }

        private CompletionStage<Void> invokeActorJoinedHandler(
            SpotActorLifecycleHandlerRegistration registration,
            Object spotSurface,
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object invocationResult =
                    registration.handlerMethod().invoke(
                        handler,
                        actorLifecycleArguments(
                            registration.handlerMethod(),
                            spotSurface,
                            actor,
                            result));
                if (invocationResult instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor joined handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Void> invokeActorSendHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(
                    handler,
                    actorPacketArguments(
                        registration.handlerMethod(),
                        spot,
                        actor,
                        new DefaultSpotActorSendContext(registration.packetName()),
                        message));
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor send handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Optional<Message>> invokeActorRequestHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(
                    handler,
                    actorPacketArguments(
                        registration.handlerMethod(),
                        spot,
                        actor,
                        new DefaultSpotActorRequestContext(registration.packetName()),
                        message));
                CompletionStage<?> stage = result instanceof CompletionStage<?> completionStage
                    ? completionStage
                    : CompletableFuture.completedFuture(result);
                return stage.thenApply(reply -> Optional.of(serializer.serialize(reply)));
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor request handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        @Override
        public void close() {
            if (spot == null) {
                closeResources();
                return;
            }
            try {
                awaitClosing(spot.onClosingAsync());
            } finally {
                closeResources();
            }
        }

        private void closeResources() {
            if (pendingActorHeader != null) {
                pendingActorHeader.close();
                pendingActorHeader = null;
            }
            context.closeTimers();
            backendSpot.close();
        }
    }

    private void awaitClosing(CompletionStage<Void> closingStage) {
        try {
            closingStage.toCompletableFuture()
                .get(defaultTimeout.toMillis(), TimeUnit.MILLISECONDS);
        } catch (TimeoutException ex) {
            throw new ZLinkConfigurationException(
                "SPOT closing hook did not complete before timeout.",
                ex);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new ZLinkConfigurationException(
                "SPOT closing hook was interrupted.",
                ex);
        } catch (java.util.concurrent.ExecutionException ex) {
            throw new ZLinkConfigurationException(
                "SPOT closing hook failed.",
                ex.getCause());
        }
    }
}
