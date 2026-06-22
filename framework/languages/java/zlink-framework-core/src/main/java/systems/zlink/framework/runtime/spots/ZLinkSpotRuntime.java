package systems.zlink.framework.runtime.spots;

import systems.zlink.framework.runtime.backend.*;

import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.EnumSet;
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
import java.util.concurrent.Executor;
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
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.spot.SpotRouteBridgeEndpointCapabilities;
import systems.zlink.contracts.service.spot.SpotRouteBridgeEndpointOptions;
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
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowPhase;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorEvent;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.channels.ChannelKind;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkGenericTypeResolver;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkWorkerCall;
import systems.zlink.framework.spots.ZLinkWorkerTask;
import systems.zlink.framework.spots.ZLinkSpotCreateState;
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
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

public final class ZLinkSpotRuntime implements ZLinkSpotManager, AutoCloseable {
    private static final String KOTLIN_SPOT_PACKET_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler";
    private static final String KOTLIN_SPOT_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler";
    private static final String KOTLIN_SPOT_SUBSCRIPTION_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler";
    private static final String KOTLIN_SPOT_TIMER_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotTimerHandler";
    private static final String KOTLIN_ENTRY_SPOT_ACTOR_SEND_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorSendHandler";
    private static final String KOTLIN_ENTRY_SPOT_ACTOR_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler";
    private static final String KOTLIN_SPOT_ACTOR_SEND_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorSendHandler";
    private static final String KOTLIN_SPOT_ACTOR_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler";
    private static final String REMOTE_BOUND_SESSION_BIND_PACKET_NAME =
        "zlink.framework.actor.bound_session.bind";

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
    private final List<ZLinkBackendSpotRouteBridge> attachedChannelBridges = new ArrayList<>();
    private final Map<String, ZLinkBackendSpotNode> nodesByName = new HashMap<>();
    private final Map<String, ZLinkBackendDiscovery> spotDiscoveriesByMesh = new HashMap<>();
    private final List<SpotDiscoveryBinding> spotDiscoveryBindings = new ArrayList<>();
    private final Set<String> discoveredPubSubPeers = new HashSet<>();
    private final Set<String> discoveredRouterPeers = new HashSet<>();
    private final Map<String, Integer> discoveredRouterPeerSightings = new HashMap<>();
    private final Map<String, Set<String>> discoveredRouterPeerRidKeys = new HashMap<>();
    private final Set<RoutingId> connectedRouterPeerRids = ConcurrentHashMap.newKeySet();
    private final Map<String, ZLinkBackendSpotNode> publisherNodesByChannel = new HashMap<>();
    private final Map<String, ZLinkBackendSpot> publisherSpotsByChannel = new HashMap<>();
    private final Set<Class<? extends ZLinkSpot<?>>> registeredSpotTypes = new HashSet<>();
    private final Map<RoutingId, SpotActivation> spots = new HashMap<>();
    private final List<EntrySpotActivation> entrySpots = new ArrayList<>();
    private final ZLinkBackendSpotNode primaryNode;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final ZLinkDispatchErrorReporter dispatchErrors;
    private final Executor handlerExecutor;
    private final List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers;
    private final ZLinkScannedHandlerCatalog handlerCatalog;
    private final Map<String, List<SpotActorPacketHandlerRegistration>> actorPacketHandlers;
    private final Duration defaultRequestTimeout;
    private final ZLinkChannelRuntime channels;
    private ZLinkActorRuntime actorRuntime;
    private final Map<String, SpotNodeRegistration> acceptedRouteNodesByChannel = new HashMap<>();
    private final List<String> egressChannels = new ArrayList<>();
    private final List<String> routeMeshChannels = new ArrayList<>();
    private final ThreadLocal<DefaultSpotOutbound> currentOutbound = new ThreadLocal<>();
    private final Map<RoutingId, CompletionStage<ZLinkSpotCreateResult>> pendingSpotCreates =
        new HashMap<>();
    private volatile boolean closing;
    private final ZLinkWorkerPool workerPool;
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
        this.handlerExecutor = java.util.Objects.requireNonNull(
            registration.handlerExecutor(),
            "handlerExecutor");
        this.dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.dispatchOptions(),
            handlerFactory,
            this.handlerExecutor);
        this.suspendHandlerInvokers = registration.suspendHandlerInvokers();
        this.workerPool = new ZLinkWorkerPool(
            registration.workers().minThreads(),
            registration.workers().maxThreads(),
            registration.workers().idleTimeout(),
            registration.workers().maxQueueLength());
        this.handlerCatalog = ZLinkHandlerScanner.scan(registration.handlerPackageMarkers());
        this.actorPacketHandlers = new HashMap<>(actorPacketHandlersByPacket(handlerCatalog));
        prepareHandlerSerializerTypes();
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkSpotBackendAdapter spotAdapter =
            backendFactory.createSpotAdapter(adapterOptions);
        this.context = channelAdapter.createContext();
        this.defaultRequestTimeout = registration.defaultRequestTimeout();
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
                for (Class<? extends ZLinkEntrySpot<?>> entrySpotType : nodeRegistration.entrySpots()) {
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
            for (SpotNodeRegistration.RouterManualConnection connection
                    : nodeRegistration.routerManualConnections()) {
                node.connectPeer(connection.endpoint());
                if (hasRoutingId(connection.peerRoutingId())) {
                    connectedRouterPeerRids.add(connection.peerRoutingId());
                }
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

    private static Map<String, List<SpotActorPacketHandlerRegistration>> actorPacketHandlersByPacket(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, List<SpotActorPacketHandlerRegistration>> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (handler.surface() != ZLinkScannedHandlerSurface.SPOT
                || (handler.kind() != ZLinkScannedHandlerKind.ACTOR_SEND
                    && handler.kind() != ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
                continue;
            }
            SpotActorPacketHandlerRegistration registration =
                createActorPacketRegistration(handler);
            addActorPacketRegistration(handlers, registration);
        }
        Map<String, List<SpotActorPacketHandlerRegistration>> snapshot = new HashMap<>();
        for (Map.Entry<String, List<SpotActorPacketHandlerRegistration>> entry : handlers.entrySet()) {
            snapshot.put(entry.getKey(), List.copyOf(entry.getValue()));
        }
        return Map.copyOf(snapshot);
    }

    private static void addActorPacketRegistration(
        Map<String, List<SpotActorPacketHandlerRegistration>> handlers,
        SpotActorPacketHandlerRegistration registration) {
        List<SpotActorPacketHandlerRegistration> packetHandlers =
            handlers.computeIfAbsent(registration.packetName(), ignored -> new ArrayList<>());
        for (SpotActorPacketHandlerRegistration existing : packetHandlers) {
            if (existing.spotType() == registration.spotType()
                && existing.actorType() == registration.actorType()
                && existing.kind() == registration.kind()) {
                throw new ZLinkConfigurationException(
                    "duplicate Spot actor packet handler packet: " + registration.packetName());
            }
        }
        packetHandlers.add(registration);
    }

    private static SpotActorPacketHandlerRegistration createActorPacketRegistration(
        ZLinkScannedHandler handler) {
        if (handler.handlerMethod() != null) {
            ActorMessageShape shape = actorPacketHandlerShape(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
                    ? ZLinkSpotActorRequestContext.class
                    : ZLinkSpotActorSendContext.class);
            return new SpotActorPacketHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.spotType() != null ? handler.spotType() : shape.spotType(),
                shape.actorType(),
                handler.messageType(),
                handler.replyType(),
                handler.packetName(),
                handler.kind());
        }
        ParameterizedType matched = findActorPacketInterface(
            handler.handlerType(),
            handler.kind());
        if (matched == null) {
            throw new ZLinkConfigurationException(
                "Spot actor packet interface handler does not match its scanned kind: "
                    + handler.handlerType().getName());
        }
        Type[] arguments = matched.getActualTypeArguments();
        return new SpotActorPacketHandlerRegistration(
            handler.handlerType(),
            null,
            requireClassArgument(handler.handlerType(), arguments[0]),
            requireClassArgument(handler.handlerType(), arguments[1]),
            handler.messageType(),
            handler.replyType(),
            handler.packetName(),
            handler.kind());
    }

    private static ParameterizedType findActorPacketInterface(
        Class<?> handlerType,
        ZLinkScannedHandlerKind kind) {
        if (kind == ZLinkScannedHandlerKind.ACTOR_REQUEST) {
            ParameterizedType entry =
                findInterface(handlerType, ZLinkEntrySpotActorRequestHandler.class);
            if (entry != null) {
                return entry;
            }
            ParameterizedType spot =
                findInterface(handlerType, ZLinkSpotActorRequestHandler.class);
            if (spot != null) {
                return spot;
            }
            entry = findInterface(handlerType, KOTLIN_ENTRY_SPOT_ACTOR_REQUEST_HANDLER);
            return entry != null
                ? entry
                : findInterface(handlerType, KOTLIN_SPOT_ACTOR_REQUEST_HANDLER);
        }
        ParameterizedType entry =
            findInterface(handlerType, ZLinkEntrySpotActorSendHandler.class);
        if (entry != null) {
            return entry;
        }
        ParameterizedType spot =
            findInterface(handlerType, ZLinkSpotActorSendHandler.class);
        if (spot != null) {
            return spot;
        }
        entry = findInterface(handlerType, KOTLIN_ENTRY_SPOT_ACTOR_SEND_HANDLER);
        return entry != null
            ? entry
            : findInterface(handlerType, KOTLIN_SPOT_ACTOR_SEND_HANDLER);
    }

    private void prepareHandlerSerializerTypes() {
        for (List<SpotActorPacketHandlerRegistration> registrations : actorPacketHandlers.values()) {
            for (SpotActorPacketHandlerRegistration registration : registrations) {
                serializer.prepare(registration.messageType());
                serializer.prepare(registration.replyType());
            }
        }
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType) {
        return create(spotType, new Message());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        Message request) {
        requireRegistered(spotType);
        ZLinkBackendSpot spot = primaryNode.createSpot();
        RoutingId spotRid = spot.routingId();
        if (spots.containsKey(spotRid)) {
            spot.close();
            throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
        }
        return activateAsync(spotType, spot, request)
            .thenApply(result -> {
                if (!result.response().accepted()) {
                    return new ZLinkSpotCreateResult(
                        spotRid,
                        ZLinkSpotCreateState.REJECTED,
                        result.response().reply());
                }
                SpotActivation activation = result.activation();
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(
                    spotRid,
                    ZLinkSpotCreateState.CREATED,
                    result.response().reply());
            });
        }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid) || pendingSpotCreates.containsKey(spotRid)) {
            throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
        }
        ZLinkBackendSpot spot = primaryNode.createSpot();
        spot.setRoutingId(spotRid);
        CompletionStage<ZLinkSpotCreateResult> create = activateAsync(spotType, spot, new Message())
            .thenApply(result -> {
                if (!result.response().accepted()) {
                    return new ZLinkSpotCreateResult(
                        spotRid,
                        ZLinkSpotCreateState.REJECTED,
                        result.response().reply());
                }
                SpotActivation activation = result.activation();
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(
                    spotRid,
                    ZLinkSpotCreateState.CREATED,
                    result.response().reply());
            })
            .whenComplete((ignored, error) -> pendingSpotCreates.remove(spotRid));
        pendingSpotCreates.put(spotRid, create);
        return create;
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        return getOrCreate(spotType, spotRid, new Message());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        Message request) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid)) {
            return CompletableFuture.completedFuture(
                new ZLinkSpotCreateResult(spotRid, ZLinkSpotCreateState.EXISTING, null));
        }
        CompletionStage<ZLinkSpotCreateResult> pending = pendingSpotCreates.get(spotRid);
        if (pending != null) {
            return pending.thenApply(result -> result.state() == ZLinkSpotCreateState.CREATED
                ? new ZLinkSpotCreateResult(
                    result.spotRid(),
                    ZLinkSpotCreateState.EXISTING,
                    result.reply())
                : result);
        }
        ZLinkBackendSpot spot = primaryNode.createSpot();
        spot.setRoutingId(spotRid);
        CompletionStage<ZLinkSpotCreateResult> create = activateAsync(spotType, spot, request)
            .thenApply(result -> {
                if (!result.response().accepted()) {
                    return new ZLinkSpotCreateResult(
                        spotRid,
                        ZLinkSpotCreateState.REJECTED,
                        result.response().reply());
                }
                SpotActivation activation = result.activation();
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(
                    spotRid,
                    ZLinkSpotCreateState.CREATED,
                    result.response().reply());
            })
            .whenComplete((ignored, error) -> pendingSpotCreates.remove(spotRid));
        pendingSpotCreates.put(spotRid, create);
        return create;
    }

    @Override
    public CompletionStage<Optional<ZLinkSpotInfo>> find(RoutingId spotRid) {
        requireRoutingId(spotRid);
        return CompletableFuture.completedFuture(
            spots.containsKey(spotRid)
                ? Optional.of(new ZLinkSpotInfo(spotRid))
                : Optional.empty());
    }

    @Override
    public CompletionStage<List<ZLinkSpotInfo>> list() {
        return CompletableFuture.completedFuture(
            spots.keySet().stream().map(ZLinkSpotInfo::new).toList());
    }

    @Override
    public CompletionStage<Boolean> close(RoutingId spotRid) {
        requireRoutingId(spotRid);
        if (actorRuntime != null && actorRuntime.hasActorsInSpot(spotRid)) {
            return CompletableFuture.completedFuture(false);
        }
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
        for (ZLinkBackendSpotRouteBridge bridge : attachedChannelBridges) {
            bridge.close();
        }
        attachedChannelBridges.clear();
        for (ZLinkBackendDiscovery discovery : attachedChannelDiscoveries) {
            discovery.close();
        }
        attachedChannelDiscoveries.clear();
        timerExecutor.shutdownNow();
        workerPool.close();
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
        this.actorRuntime.setCreatedNotifier(this::notifyEntrySpotActorCreated);
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
        Optional<ZLinkActor> localActor = actorRuntime.localActor(actorRef.actorId());
        if (localActor.isEmpty()) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "local actor is not available: " + actorRef.actorId()));
        }
        ZLinkActor actor = localActor.get();
        Optional<RoutingId> joinedSpotRid = actorRuntime.spotRid(actor);
        if (joinedSpotRid.isPresent()
            && currentSpotSurface(actor) == null
            && spotSurfaceFor(joinedSpotRid.get()) == null
            && actorRuntime.canRouteRemoteJoinedSpot(joinedSpotRid.get())) {
            return actorRuntime.dispatchRemoteJoinedActor(
                actorRuntime.currentRef(actor),
                joinedSpotRid.get(),
                header,
                payload);
        }
        Object spotSurface = localActorSpotSurface(actor);
        boolean isRequest = header.requestSequence().isPresent()
            || header.kind() == ZLinkStreamMessageKind.REQUEST;
        SpotActorPacketHandlerRegistration handler =
            resolveActorPacketHandler(
                header.packetName(),
                spotSurface,
                isRequest
                    ? ZLinkScannedHandlerKind.ACTOR_REQUEST
                    : ZLinkScannedHandlerKind.ACTOR_SEND);
        if (handler == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor packet handler is not registered: " + header.packetName()));
        }
        if (isRequest != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor packet kind does not match handler kind: " + header.packetName()));
        }
        if (!handler.actorType().isInstance(actor)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor packet handler target type does not match actor: " + actorRef.actorId()));
        }
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
    private void requireRegistered(Class<? extends ZLinkSpot<?>> spotType) {
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

    private CompletionStage<SpotActivationCreateResult> activateAsync(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkBackendSpot backendSpot,
        Message request) {
        Message effectiveRequest = request == null ? new Message() : request;
        DefaultSpotContext spotContext =
            new DefaultSpotContext(primaryNode.routingId(), backendSpot);
        ZLinkSpot<?> spot = tryCreateSpot(spotType, spotContext);
        if (spot == null) {
            return CompletableFuture.completedFuture(new SpotActivationCreateResult(
                new SpotActivation(null, backendSpot, spotContext),
                ZLinkSpotCreateResponse.accept()));
        }
        spotContext.setSpot(spot);
        spot.configure();
        spotContext.closeRegistration();
        spotContext.bindSubscriptions(backendSpot);
        return withCurrentOutbound(spotContext.outbound, () ->
                ZLinkHandlerStages.fromSupplier(() -> spot.onCreate(effectiveRequest)))
            .thenCompose(response -> {
                ZLinkSpotCreateResponse effectiveResponse =
                    response == null ? ZLinkSpotCreateResponse.accept() : response;
                if (!effectiveResponse.accepted()) {
                    spotContext.closeTimers();
                    backendSpot.close();
                    return CompletableFuture.completedFuture(
                        new SpotActivationCreateResult(null, effectiveResponse));
                }
                return withCurrentOutbound(spotContext.outbound, () ->
                        ZLinkHandlerStages.fromRunnable(spot::onInitialize))
                    .thenApply(ignored -> {
                        SpotActivation activation = new SpotActivation(spot, backendSpot, spotContext);
                        backendSpot.onDispatchEvent(activation::handleDispatchEvent);
                        return new SpotActivationCreateResult(activation, effectiveResponse);
                    });
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
        Class<? extends ZLinkEntrySpot<?>> entrySpotType) {
        DefaultEntrySpotContext entryContext = new DefaultEntrySpotContext(
            nodeRid,
            backendSpot);
        ZLinkEntrySpot<?> entrySpot = createEntrySpot(entrySpotType, entryContext);
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
        entryContext.enqueueDispatch(() ->
                withCurrentOutbound(entryContext.outbound, () ->
                    ZLinkHandlerStages.fromRunnable(entrySpot::onInitialize)))
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
        CompletableFuture<T> result = new CompletableFuture<>();
        try {
            handlerExecutor.execute(() -> {
                DefaultSpotOutbound previous = currentOutbound.get();
                currentOutbound.set(outbound);
                try {
                    action.get().whenComplete((value, error) -> {
                        if (error != null) {
                            result.completeExceptionally(error);
                        } else {
                            result.complete(value);
                        }
                    });
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                } finally {
                    if (previous == null) {
                        currentOutbound.remove();
                    } else {
                        currentOutbound.set(previous);
                    }
                }
            });
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        }
        return result;
    }

    private DefaultSpotOutbound requireCurrentOutbound() {
        DefaultSpotOutbound outbound = currentOutbound.get();
        if (outbound == null) {
            throw new ZLinkConfigurationException(
                "ZLinkSpotOutbound can only be used inside an active Spot callback");
        }
        return outbound;
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> notifySpotActorLifecycle(
        Object spotSurface,
        ZLinkActor actor,
        boolean joined) {
        if (spotSurface instanceof ZLinkSpot spot) {
            return withCurrentOutbound(
                ((DefaultSpotContext) spot.context()).outbound,
                () -> joined
                    ? ZLinkHandlerStages.fromRunnable(() ->
                        spot.onJoinedActor(actor, NONE_CANCELLATION))
                    : ZLinkHandlerStages.fromRunnable(() ->
                        spot.onLeaveActor(actor, NONE_CANCELLATION)));
        }
        if (spotSurface instanceof ZLinkEntrySpot entrySpot) {
            return joined
                ? ZLinkHandlerStages.fromRunnable(() ->
                    entrySpot.onJoinedActor(actor, NONE_CANCELLATION))
                : ZLinkHandlerStages.fromRunnable(() ->
                    entrySpot.onLeaveActor(actor, NONE_CANCELLATION));
        }
        return CompletableFuture.completedFuture(null);
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> notifySpotActorDisconnected(ZLinkActor actor) {
        Object spotSurface = localActorSpotSurface(actor);
        if (spotSurface instanceof ZLinkSpot spot) {
            if (spot.context() instanceof DefaultSpotContext context) {
                return withCurrentOutbound(
                    context.outbound,
                    () -> ZLinkHandlerStages.fromRunnable(() ->
                        spot.onDisconnectActor(actor, NONE_CANCELLATION)));
            }
            return ZLinkHandlerStages.fromRunnable(() ->
                spot.onDisconnectActor(actor, NONE_CANCELLATION));
        }
        if (spotSurface instanceof ZLinkEntrySpot entrySpot) {
            return ZLinkHandlerStages.fromRunnable(() ->
                entrySpot.onDisconnectActor(actor, NONE_CANCELLATION));
        }
        return CompletableFuture.completedFuture(null);
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> notifyEntrySpotActorCreated(
        RoutingId nodeRid,
        ZLinkActor actor) {
        for (EntrySpotActivation activation : entrySpots) {
            if (activation.context.nodeRid().equals(nodeRid)) {
                ZLinkEntrySpot rawEntrySpot = activation.entrySpot;
                return activation.context.enqueueDispatch(() ->
                    ZLinkHandlerStages.fromRunnable(() ->
                        rawEntrySpot.onCreateActor(actor, NONE_CANCELLATION)));
            }
        }
        return CompletableFuture.completedFuture(null);
    }

    private ZLinkSpot<?> spotFor(RoutingId spotRid) {
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

    private EntrySpotActivation entrySpotActivationFor(RoutingId spotRid) {
        for (EntrySpotActivation activation : entrySpots) {
            if (activation.backendSpot.routingId().equals(spotRid)) {
                return activation;
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

    private SpotActorPacketHandlerRegistration resolveActorPacketHandler(
        String packetName,
        Object spotSurface,
        ZLinkScannedHandlerKind kind) {
        List<SpotActorPacketHandlerRegistration> handlers = actorPacketHandlers.get(packetName);
        if (handlers == null || handlers.isEmpty()) {
            return null;
        }
        Class<?> spotType = spotSurface == null ? null : spotSurface.getClass();
        SpotActorPacketHandlerRegistration fallback = null;
        for (SpotActorPacketHandlerRegistration handler : handlers) {
            if (handler.kind() != kind) {
                continue;
            }
            if (spotType != null && handler.spotType() == spotType) {
                return handler;
            }
            if (fallback == null) {
                fallback = handler;
            }
        }
        return fallback;
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
        Object message = serializer.deserialize(payload, registration.messageType());
        if (registration.handlerMethod() == null) {
            return invokeActorSendInterfaceHandler(
                registration,
                spotSurface,
                actor,
                new DefaultSpotActorSendContext(registration.packetName()),
                message,
                "failed to invoke local session actor send handler");
        }
        return invokeVoidMethodHandler(
            registration.handlerType(),
            registration.handlerMethod(),
            actorPacketArguments(
                registration.handlerMethod(),
                spotSurface,
                actor,
                new DefaultSpotActorSendContext(registration.packetName()),
                message),
            "failed to invoke local session actor send handler");
    }

    private CompletionStage<Optional<Message>> invokeLocalActorRequestHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        Object message = serializer.deserialize(payload, registration.messageType());
        if (registration.handlerMethod() == null) {
            return invokeActorRequestInterfaceHandler(
                    registration,
                    spotSurface,
                    actor,
                    new DefaultSpotActorRequestContext(registration.packetName()),
                    message,
                    "failed to invoke local session actor request handler")
                .thenApply(reply -> Optional.of(serializer.serialize(reply)));
        }
        return invokeReplyMethodHandler(
                registration.handlerType(),
                registration.handlerMethod(),
                actorPacketArguments(
                    registration.handlerMethod(),
                    spotSurface,
                    actor,
                    new DefaultSpotActorRequestContext(registration.packetName()),
                    message),
                "failed to invoke local session actor request handler")
            .thenApply(reply -> Optional.of(serializer.serialize(reply)));
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
        ZLinkBackendSpotRouteBridge bridge = node.createRouteBridge();
        attachedChannelBridges.add(bridge);
        if (!attached.manualConnections().isEmpty()) {
            for (String endpoint : attached.manualConnections()) {
                dealer.connect(endpoint);
            }
            bridge.attachDealerChannel(
                attached.channelName(),
                dealer,
                new SpotRouteBridgeEndpointOptions()
                    .capabilities(SpotRouteBridgeEndpointCapabilities.ROUTE_ONLY));
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
        bridge.attachDealerChannel(
            attached.channelName(),
            dealer,
            new SpotRouteBridgeEndpointOptions()
                .capabilities(SpotRouteBridgeEndpointCapabilities.ROUTE_ONLY));
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
        if (frameworkRegistration.registrySpotRemoteAddresses() != null) {
            discovery.setSpotOwnerSyncEnabled(true);
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
                if (peer.autoConnectType() != AutoConnectType.SPOT_MESH
                    || !binding.meshName().equals(peer.channelName())
                    || peer.endpoint() == null
                    || peer.endpoint().isBlank()
                    || peer.endpoint().equals(binding.routerBind())
                    || peer.endpoint().equals(binding.pubBind())) {
                    continue;
                }
                if (peer.serviceRole() == ServiceRole.ROUTER) {
                    if (binding.routerEnabled() && !binding.pubSubEnabled()) {
                        connectDiscoveredRouterPeer(
                            binding,
                            peer.routingId(),
                            peer.endpoint());
                    }
                    continue;
                }
                if (peer.serviceRole() != ServiceRole.SPOT) {
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
            if (peer.autoConnectType() == AutoConnectType.SPOT_MESH
                && peer.serviceRole() == ServiceRole.ROUTER
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
        String ridKey = hasRoutingId(peerRoutingId) ? peerRoutingId.toString() : null;
        if (!discoveredRouterPeers.add(key)) {
            int sightings = discoveredRouterPeerSightings.merge(key, 1, Integer::sum);
            if (ridKey != null
                && discoveredRouterPeerRidKeys
                    .computeIfAbsent(key, ignored -> new HashSet<>())
                    .add(ridKey)) {
                try {
                    binding.node().connectPeer(peerRoutingId, endpoint);
                } catch (ZlinkConnectException ex) {
                    if (!isIdempotentConnectFailure(ex)) {
                        removeDiscoveredRouterPeerRidKey(key, ridKey);
                        return;
                    }
                } catch (RuntimeException ex) {
                    removeDiscoveredRouterPeerRidKey(key, ridKey);
                    return;
                }
            }
            if (hasRoutingId(peerRoutingId)
                && sightings >= ROUTER_PEER_CONVERGENCE_SNAPSHOTS) {
                connectedRouterPeerRids.add(peerRoutingId);
            }
            return;
        }
        discoveredRouterPeerSightings.put(key, 1);
        try {
            if (hasRoutingId(peerRoutingId)) {
                binding.node().connectPeer(peerRoutingId, endpoint);
                discoveredRouterPeerRidKeys
                    .computeIfAbsent(key, ignored -> new HashSet<>())
                    .add(ridKey);
            } else {
                binding.node().connectPeer(endpoint);
            }
            if (hasRoutingId(peerRoutingId)) {
                connectedRouterPeerRids.add(peerRoutingId);
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
            discoveredRouterPeerRidKeys.remove(key);
        } catch (RuntimeException ex) {
            discoveredRouterPeers.remove(key);
            discoveredRouterPeerSightings.remove(key);
            discoveredRouterPeerRidKeys.remove(key);
        }
    }

    private void removeDiscoveredRouterPeerRidKey(String key, String ridKey) {
        Set<String> ridKeys = discoveredRouterPeerRidKeys.get(key);
        if (ridKeys == null) {
            return;
        }
        ridKeys.remove(ridKey);
        if (ridKeys.isEmpty()) {
            discoveredRouterPeerRidKeys.remove(key);
        }
    }

    private static boolean isIdempotentConnectFailure(ZlinkConnectException ex) {
        int errno = ex.getNativeErrno();
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
        if (channels.attachSpotRouteBridgeToServer(acceptance.channelName(), node)) {
            return;
        }

        throw new ZLinkConfigurationException(
            "accepted SPOT route channel requires a configured channel server or route mesh socket: "
                + acceptance.channelName());
    }

    private ZLinkSpot<?> tryCreateSpot(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkSpotContext context) {
        try {
            return (ZLinkSpot<?>) ZLinkHandlerFactory.services(handlerFactory)
                .add(ZLinkSpotContext.class, context)
                .create(spotType);
        } catch (RuntimeException ex) {
            throw new ZLinkConfigurationException(
                "failed to create spot: " + spotType.getName(),
                ex);
        }
    }

    private ZLinkEntrySpot<?> createEntrySpot(
        Class<? extends ZLinkEntrySpot<?>> entrySpotType,
        ZLinkEntrySpotContext context) {
        try {
            return (ZLinkEntrySpot<?>) ZLinkHandlerFactory.services(handlerFactory)
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
        private final ZLinkAsyncSerialQueue dispatchQueue = new ZLinkAsyncSerialQueue();
        private final List<DefaultSpotContext> timerContexts = new ArrayList<>();
        private final List<Class<?>> handlerTypes = new ArrayList<>();
        private final List<Class<?>> packetHandlerTypes = new ArrayList<>();
        private final List<SpotSubscriptionRegistration> subscriptionHandlerTypes = new ArrayList<>();
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkEntrySpot<?> entrySpot;

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

        void setEntrySpot(ZLinkEntrySpot<?> entrySpot) {
            this.entrySpot = entrySpot;
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return outbound;
        }

        @Override
        public CompletionStage<Void> destroyActor(ZLinkActor actor) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "Entry Spot actor destroy requires the actor runtime"));
            }
            return actorRuntime.destroyFromEntrySpot(
                nodeRid,
                actor);
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
            // Timer contexts share the Entry Spot serial line so timer callbacks
            // never overlap actor packets or lifecycle callbacks.
            DefaultSpotContext timerContext =
                new DefaultSpotContext(nodeRid, backendSpot, dispatchQueue);
            timerContext.setSpot(new EntrySpotTimerSurface(this));
            timerContexts.add(timerContext);
            return timerContext.addTimer(name, period, handlerType, options);
        }

        void closeTimers() {
            timerContexts.forEach(DefaultSpotContext::closeTimers);
            timerContexts.clear();
        }

        CompletionStage<Void> enqueueDispatch(Supplier<CompletionStage<Void>> operation) {
            return dispatchQueue.enqueue(operation);
        }

        @Override
        public <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work) {
            java.util.Objects.requireNonNull(work, "work");
            // Completion callbacks enter the Entry Spot serial line through
            // the handler executor: never on the worker thread, with the
            // entry spot outbound context available.
            return new DefaultZLinkWorkerCall<>(workerPool, work, operation ->
                enqueueDispatch(() -> withCurrentOutbound(outbound, operation)));
        }

        void closeRegistration() {
            registrationOpen = false;
            registerScannedSpotHandlers(
                entrySpot.getClass(),
                packetHandlers,
                subscriptionHandlers,
                this::addTimer);
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

    private final class EntrySpotTimerSurface implements ZLinkSpot<ZLinkActor> {
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
        public CompletionStage<Void> leaveActor(
            ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Boolean> close() {
            return CompletableFuture.completedFuture(false);
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
        private final ZLinkEntrySpot<?> entrySpot;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultEntrySpotContext context;
        private ZLinkBackendActorReceived pendingActorHeader;

        EntrySpotActivation(
            ZLinkEntrySpot<?> entrySpot,
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
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.RECEIVED,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    received.requestSeq().isPresent()
                        ? ZLinkDispatchMessageKind.REQUEST
                        : ZLinkDispatchMessageKind.SEND,
                    packet.packetName(), null, null,
                    received.requestSeq().map(String::valueOf).orElse(null),
                    null, backendSpot.routingId().toString(), null, null));
            }
            if (ZLinkActorSpotRoutePackets.BOUND_SESSION_SEND_PACKET_NAME.equals(packet.packetName())) {
                if (received.requestSeq().isPresent()) {
                    handleRoutedBoundSessionSendRequestParts(received.parts())
                        .thenAccept(received::reply)
                        .whenComplete((ignored, error) -> received.close());
                } else {
                    handleRoutedBoundSessionSendParts(received.parts());
                    received.close();
                }
                return;
            }
            if (ZLinkActorSpotRoutePackets.ACTOR_PACKET_NAME.equals(packet.packetName())) {
                handleRoutedActorPacketParts(received.parts())
                    .thenAccept(reply -> reply.ifPresent(message -> received.reply(List.of(message))))
                    .whenComplete((ignored, error) -> received.close());
                return;
            }
            SpotPacketHandlerRegistration handler =
                context.packetHandler(packet.packetName());
            if (handler == null) {
                if (received.requestSeq().isPresent()) {
                    received.reply(List.of(Message.from((
                        "No SPOT route request handler is registered for '"
                            + packet.packetName() + "'.").getBytes(StandardCharsets.UTF_8))));
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.REQUEST,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.REPLY_ERROR,
                        packet.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        null,
                        received.routingId().orElse(null),
                        received.requestSeq().map(Object::toString).orElse(null),
                        null);
                } else {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.SEND,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.DROP,
                        packet.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        null,
                        received.routingId().orElse(null),
                        null,
                        null);
                }
                received.close();
                return;
            }
            if (received.requestSeq().isPresent()) {
                if (!handler.request()) {
                    received.reply(List.of(Message.from((
                        "SPOT route handler for '" + packet.packetName()
                            + "' does not accept requests.").getBytes(StandardCharsets.UTF_8))));
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.REQUEST,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.REPLY_ERROR,
                        packet.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        null,
                        received.routingId().orElse(null),
                        received.requestSeq().map(Object::toString).orElse(null),
                        null);
                    received.close();
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                context.enqueueDispatch(() ->
                    withCurrentOutbound(context.outbound, () ->
                        invokeSpotRequestHandler(handler, entrySpot, payloadCopy))
                        .thenAccept(reply -> received.reply(List.of(reply)))
                        .whenComplete((ignored, error) -> {
                            payloadCopy.close();
                            received.close();
                        }));
                return;
            }
            try (received) {
                if (handler.request()) {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.SEND,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.DROP,
                        packet.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        null,
                        received.routingId().orElse(null),
                        null,
                        null);
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                context.enqueueDispatch(() ->
                    withCurrentOutbound(context.outbound, () ->
                        invokeSpotPacketHandler(handler, entrySpot, payloadCopy))
                        .whenComplete((ignored, error) -> payloadCopy.close()));
            }
        }

        private void handleRoutedBoundSessionSendParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.BoundSessionSend send =
                ZLinkActorSpotRoutePackets.decodeBoundSessionSend(parts);
            sendActorBoundSessionWithRetry(
                primaryNode(),
                send.actorRef(),
                send.actorRef().actorId(),
                send.frame().toByteArray(),
                "routed actor bound session send failed")
                .whenComplete((ignored, error) -> send.close());
        }

        private CompletionStage<List<Message>> handleRoutedBoundSessionSendRequestParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.BoundSessionSend send =
                ZLinkActorSpotRoutePackets.decodeBoundSessionSend(parts);
            return sendActorBoundSessionWithRetry(
                primaryNode(),
                send.actorRef(),
                send.actorRef().actorId(),
                send.frame().toByteArray(),
                "routed actor bound session send failed")
                .thenApply(ignored -> List.of(Message.from(new byte[0])))
                .whenComplete((ignored, error) -> {
                    send.close();
                });
        }

        private CompletionStage<Optional<Message>> handleRoutedActorPacketParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.ActorPacket packet =
                ZLinkActorSpotRoutePackets.decodeActorPacket(parts);
            return dispatchLocalSessionActor(packet.actorRef(), packet.header(), packet.payload())
                .whenComplete((ignored, error) -> packet.close());
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
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                        ZLinkDispatchMessageKind.PUBLISH,
                        ZLinkDispatchErrorReason.INVALID_FRAME,
                        ZLinkDispatchErrorAction.DROP,
                        null,
                        null,
                        received.topic(),
                        backendSpot.routingId(),
                        null,
                        null,
                        null,
                        null);
                    return;
                }
                ParsedPacket packet = parsePacket(received.parts());
                if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
                    dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowPhase.RECEIVED,
                        ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                        ZLinkDispatchMessageKind.PUBLISH,
                        packet.packetName(), null, received.topic(),
                        null, null, backendSpot.routingId().toString(), null, null));
                }
                boolean dispatched = false;
                for (SpotSubscriptionHandlerRegistration handler :
                    context.subscriptionHandlers(received.topic())) {
                    if (!handler.packetName().equals(packet.packetName())) {
                        continue;
                    }
                    dispatched = true;
                    Message payloadCopy = Message.from(packet.payload());
                    context.enqueueDispatch(() ->
                        withCurrentOutbound(context.outbound, () ->
                            invokeSpotSubscriptionHandler(handler, entrySpot, payloadCopy))
                            .whenComplete((ignored, error) -> payloadCopy.close()));
                }
                if (dispatched && dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.DISPATCHED)) {
                    dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowPhase.DISPATCHED,
                        ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                        ZLinkDispatchMessageKind.PUBLISH,
                        packet.packetName(), null, received.topic(),
                        null, null, backendSpot.routingId().toString(), null, null));
                }
                if (!dispatched) {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                        ZLinkDispatchMessageKind.PUBLISH,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.DROP,
                        packet.packetName(),
                        null,
                        received.topic(),
                        backendSpot.routingId(),
                        null,
                        null,
                        null,
                        null);
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
                context.enqueueDispatch(
                    () -> notifySpotActorLifecycle(entrySpot, actor, false));
                return;
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            actorRuntime.markJoined(
                actor,
                actorRef,
                spotRid,
                spotSurfaceFor(spotRid) instanceof ZLinkSpot<?> spot ? spot : null);
            context.enqueueDispatch(
                () -> notifySpotActorLifecycle(entrySpot, actor, true));
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
                if (actorRuntime == null) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                Optional<ZLinkActor> localActor =
                    actorRuntime.localActor(headerPart.actor().actorId());
                if (localActor.isEmpty()) {
                    boolean request = packetHeader.requestSeq().isPresent();
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ACTOR,
                        request
                            ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                            : ZLinkDispatchMessageKind.ACTOR_SEND,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        request
                            ? ZLinkDispatchErrorAction.REPLY_ERROR
                            : ZLinkDispatchErrorAction.DROP,
                        packetHeader.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        headerPart.actor().actorId(),
                        headerPart.sourceNodeRid(),
                        packetHeader.requestSeq().map(Object::toString).orElse(null),
                        null);
                    if (request) {
                        ZLinkBackendActorReceived headerCopy = pendingHeader
                            ? headerPart
                            : copyActorReceived(headerPart);
                        replyActorDispatchError(
                            packetHeader,
                            headerCopy,
                            headerPart.actor().actorId(),
                            new ZLinkConfigurationException(
                                "SPOT actor is not registered locally: "
                                    + headerPart.actor().actorId()),
                            "actor missing error reply failed");
                    } else if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                ZLinkActor actor = localActor.get();
                Object spotSurface = localActorSpotSurface(actor);
                Optional<RoutingId> joinedSpotRid = actorRuntime.spotRid(actor);
                if (joinedSpotRid.isPresent()
                    && !actorRuntime.currentRef(actor).nodeRid().equals(primaryNode.routingId())
                    && spotSurfaceFor(joinedSpotRid.get()) == null
                    && actorRuntime.canRouteRemoteJoinedSpot(joinedSpotRid.get())) {
                    ZLinkBackendActorReceived headerCopy = pendingHeader
                        ? headerPart
                        : copyActorReceived(headerPart);
                    Message payloadCopy = bodyPart == null
                        ? Message.from(new byte[0])
                        : Message.from(bodyPart.message());
                    context.enqueueDispatch(() -> dispatchRemoteJoinedActorPacket(
                        actor,
                        joinedSpotRid.get(),
                        packetHeader,
                        headerCopy,
                        payloadCopy));
                    continue;
                }
                SpotActorPacketHandlerRegistration handler =
                    resolveActorPacketHandler(
                        packetHeader.packetName(),
                        spotSurface,
                        packetHeader.requestSeq().isPresent()
                            ? ZLinkScannedHandlerKind.ACTOR_REQUEST
                            : ZLinkScannedHandlerKind.ACTOR_SEND);
                if (handler == null) {
                    boolean request = packetHeader.requestSeq().isPresent();
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ACTOR,
                        request
                            ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                            : ZLinkDispatchMessageKind.ACTOR_SEND,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        request
                            ? ZLinkDispatchErrorAction.REPLY_ERROR
                            : ZLinkDispatchErrorAction.DROP,
                        packetHeader.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        actor.actorId(),
                        headerPart.sourceNodeRid(),
                        packetHeader.requestSeq().map(Object::toString).orElse(null),
                        null);
                    if (request) {
                        ZLinkBackendActorReceived headerCopy = pendingHeader
                            ? headerPart
                            : copyActorReceived(headerPart);
                        replyActorDispatchError(
                            packetHeader,
                            headerCopy,
                            actor.actorId(),
                            new ZLinkConfigurationException(
                                "No SPOT actor request handler is registered for '"
                                    + packetHeader.packetName() + "'."),
                            "actor dispatch error reply failed");
                    } else if (pendingHeader) {
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
                context.enqueueDispatch(
                    () -> dispatchActorPacket(
                        handler,
                        spotSurface,
                        actor,
                        packetHeader,
                        headerCopy,
                        payloadCopy));
            }
        }

        private CompletionStage<Void> dispatchRemoteJoinedActorPacket(
            ZLinkActor actor,
            RoutingId spotRid,
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            if (headerPart.sourceNodeRid() == null || headerPart.sourceSessionRid() == null) {
                payload.close();
                headerPart.close();
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "remote joined actor packet is missing source session route: " + actor.actorId()));
            }
            ZLinkStreamHeader header = new ZLinkStreamHeader(
                packetHeader.requestSeq().isPresent()
                    ? ZLinkStreamMessageKind.REQUEST
                    : ZLinkStreamMessageKind.SEND,
                ZLinkStreamCodec.fromValue(packetHeader.codec()),
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                packetHeader.requestSeq(),
                packetHeader.packetName(),
                Map.of());
            try (Message headerPartMessage = Message.from(ZLinkStreamHeaderCodec.encode(header));
                 Message body = Message.from(payload)) {
                ZLinkBackendActorRef targetActor = actorRuntime.currentRef(actor);
                boolean forwarded = primaryNode.forwardActorBoundSession(
                    targetActor,
                    headerPart.sourceNodeRid(),
                    headerPart.sourceSessionRid(),
                    List.of(headerPartMessage, body),
                    SendFlags.NONE);
                if (!forwarded) {
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "remote joined actor packet forward failed: " + actor.actorId()));
                }
                return CompletableFuture.completedFuture(null);
            } finally {
                payload.close();
                headerPart.close();
            }
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            Object spotSurface,
            ZLinkActor actor,
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            actorRuntime.bindNativeSession(
                actor,
                primaryNode,
                headerPart.actor(),
                headerPart.sourceNodeRid(),
                headerPart.sourceSessionRid());
            boolean actorIsRequest = handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST;
            String actorCorr = packetHeader.requestSeq().map(String::valueOf).orElse(null);
            String actorPacketName = packetHeader.packetName();
            String actorId = actor.actorId();
            ZLinkDispatchMessageKind actorKind = actorIsRequest
                ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                : ZLinkDispatchMessageKind.ACTOR_SEND;
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowPhase.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.RECEIVED,
                    ZLinkDispatchErrorSurface.SPOT_ACTOR,
                    actorKind,
                    actorPacketName, null, null, actorCorr, null, null, actorId, null));
            }
            CompletionStage<Optional<Message>> stage = withCurrentOutbound(
                context.outbound,
                () -> handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
                    ? invokeActorRequestHandler(handler, spotSurface, actor, payload)
                    : invokeActorSendHandler(handler, spotSurface, actor, payload)
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
                    if (error == null) {
                        ZLinkMessageFlowPhase phase = actorIsRequest
                            ? ZLinkMessageFlowPhase.REPLIED
                            : ZLinkMessageFlowPhase.DISPATCHED;
                        if (dispatchErrors.flow().enabled(phase)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                phase,
                                ZLinkDispatchErrorSurface.SPOT_ACTOR,
                                actorKind,
                                actorPacketName, null, null, actorCorr, null, null, actorId, null));
                        }
                    }
                });
        }

        private void replyActorDispatchError(
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            String actorId,
            Throwable error,
            String failureMessage) {
            byte[] frameBytes;
            try (Message frame = encodeActorErrorFrame(packetHeader, error)) {
                frameBytes = frame.toByteArray();
            } catch (RuntimeException ex) {
                headerPart.close();
                return;
            }
            context.enqueueDispatch(() -> sendActorBoundSessionWithRetry(
                    primaryNode,
                    new ZLinkBackendActorRef(
                        headerPart.actor().nodeRid(),
                        actorId,
                        headerPart.actor().epoch()),
                    actorId,
                    frameBytes,
                    failureMessage)
                .whenComplete((ignored, sendError) -> headerPart.close()));
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
                Message payloadCopy = request.parts().isEmpty()
                    ? Message.from(new byte[0])
                    : Message.from(request.parts().get(0).toByteArray());
                try {
                    acceptEntryActorJoin(request, payloadCopy)
                        .whenComplete((response, error) -> {
                            try {
                                if (error != null) {
                                    try (Message emptyReply = Message.from(new byte[0])) {
                                        backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                                    }
                                    return;
                                }
                                ZLinkSpotActorJoinResponse effective =
                                    response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                                Message reply = effective.reply() == null
                                    ? Message.from(new byte[0])
                                    : Message.from(effective.reply().toByteArray());
                                backendSpot.replyActorJoin(request, effective.accepted() ? 0 : 1, List.of(reply));
                                reply.close();
                            } finally {
                                payloadCopy.close();
                            }
                        });
                } finally {
                    request.parts().forEach(Message::close);
                }
            }
        }

        private CompletionStage<ZLinkSpotActorJoinResponse> acceptEntryActorJoin(
            ZLinkBackendActorJoinRequest request,
            Message payload) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Entry Spot actor join"));
            }
            return actorRuntime
                .getOrCreateLocalActor(
                    request.targetActor().actorId(),
                    ZLinkActor.class)
                .thenCompose(actor -> {
                    if (actor.isEmpty()) {
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "Entry Spot actor join target actor is not available: "
                                + request.targetActor().actorId()));
                    }
                    CompletableFuture<ZLinkSpotActorJoinResponse> admission =
                        new CompletableFuture<>();
                    context.enqueueDispatch(() ->
                            ZLinkHandlerStages.fromSupplier(() ->
                                    ((ZLinkEntrySpot) entrySpot).onActorJoin(
                                        actor.get(),
                                        payload,
                                        NONE_CANCELLATION))
                                .thenAccept(admission::complete))
                        .whenComplete((ignored, error) -> {
                            if (error != null) {
                                admission.completeExceptionally(error);
                            }
                        });
                    return admission
                        .thenCompose(response -> {
                            ZLinkSpotActorJoinResponse effective =
                                response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                            if (!effective.accepted()) {
                                return CompletableFuture.completedFuture(effective);
                            }
                            actorRuntime.markJoined(
                                actor.get(),
                                request.targetActor(),
                                backendSpot.routingId(),
                                null);
                            return context.enqueueDispatch(() ->
                                    notifySpotActorLifecycle(entrySpot, actor.get(), true))
                                .thenApply(ignored -> effective)
                                .whenComplete((reply, error) -> {
                                    if (error != null) {
                                        actorRuntime.markLeft(actor.get());
                                    }
                                });
                        });
                });
        }

        private CompletionStage<Void> invokeActorSendHandler(
            SpotActorPacketHandlerRegistration registration,
            Object spotSurface,
            ZLinkActor actor,
            Message payload) {
            Object message = serializer.deserialize(payload, registration.messageType());
            if (registration.handlerMethod() == null) {
                return invokeActorSendInterfaceHandler(
                    registration,
                    spotSurface,
                    actor,
                    new DefaultSpotActorSendContext(registration.packetName()),
                    message,
                    "failed to invoke Spot actor send handler");
            }
            return invokeVoidMethodHandler(
                registration.handlerType(),
                    registration.handlerMethod(),
                    actorPacketArguments(
                        registration.handlerMethod(),
                        spotSurface,
                        actor,
                        new DefaultSpotActorSendContext(registration.packetName()),
                        message),
                "failed to invoke Spot actor send handler");
        }

        private CompletionStage<Optional<Message>> invokeActorRequestHandler(
            SpotActorPacketHandlerRegistration registration,
            Object spotSurface,
            ZLinkActor actor,
            Message payload) {
            Object message = serializer.deserialize(payload, registration.messageType());
            if (registration.handlerMethod() == null) {
                return invokeActorRequestInterfaceHandler(
                        registration,
                        spotSurface,
                        actor,
                        new DefaultSpotActorRequestContext(registration.packetName()),
                        message,
                        "failed to invoke Spot actor request handler")
                    .thenApply(reply -> Optional.of(serializer.serialize(reply)));
            }
            return invokeReplyMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    actorPacketArguments(
                        registration.handlerMethod(),
                        spotSurface,
                        actor,
                        new DefaultSpotActorRequestContext(registration.packetName()),
                        message),
                    "failed to invoke Spot actor request handler")
                .thenApply(reply -> Optional.of(serializer.serialize(reply)));
        }

        @Override
        public void close() {
            try {
                awaitClosing(context.enqueueDispatch(() ->
                    withCurrentOutbound(context.outbound, () ->
                        ZLinkHandlerStages.fromRunnable(entrySpot::onClosing))));
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

    private record SpotActivationCreateResult(
        SpotActivation activation,
        ZLinkSpotCreateResponse response) {
    }

    private final class DefaultSpotContext implements ZLinkSpotContext {
        private static final CancellationToken NONE = () -> false;
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotOutbound outbound;
        private final List<ManagedTimer> timers = new ArrayList<>();
        private final ZLinkAsyncSerialQueue dispatchQueue;
        private final List<Class<?>> handlerTypes = new ArrayList<>();
        private final List<Class<?>> packetHandlerTypes = new ArrayList<>();
        private final List<SpotSubscriptionRegistration> subscriptionHandlerTypes = new ArrayList<>();
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkSpot<?> spot;

        DefaultSpotContext(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this(nodeRid, backendSpot, new ZLinkAsyncSerialQueue());
        }

        DefaultSpotContext(
            RoutingId nodeRid,
            ZLinkBackendSpot backendSpot,
            ZLinkAsyncSerialQueue dispatchQueue) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.dispatchQueue = dispatchQueue;
            this.outbound = new DefaultSpotOutbound(nodeRid, backendSpot);
        }

        void setSpot(ZLinkSpot<?> spot) {
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
        public CompletionStage<Void> leaveActor(ZLinkActor actor) {
            if (actor == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor is required"));
            }
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor leave"));
            }
            actorRuntime.markLeft(actor);
            return notifySpotActorLifecycle(this.spot, actor, false);
        }

        @Override
        public CompletionStage<Boolean> close() {
            return ZLinkSpotRuntime.this.close(backendSpot.routingId());
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

        CompletionStage<Void> enqueueDispatch(Supplier<CompletionStage<Void>> operation) {
            return dispatchQueue.enqueue(operation);
        }

        @Override
        public <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work) {
            java.util.Objects.requireNonNull(work, "work");
            // Completion callbacks enter the Spot serial line through the
            // handler executor, exactly like packet handlers: they never run
            // on the worker thread and they see the spot outbound context.
            return new DefaultZLinkWorkerCall<>(workerPool, work, operation ->
                enqueueDispatch(() -> withCurrentOutbound(outbound, operation)));
        }

        void closeRegistration() {
            registrationOpen = false;
            registerScannedSpotHandlers(
                spot.getClass(),
                packetHandlers,
                subscriptionHandlers,
                this::addTimer);
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
                enqueueDispatch(() ->
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
    private CompletionStage<Void> invokeTimerHandler(Class<?> handlerType, ZLinkSpot<?> spot, ZLinkTimerTick tick) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {spot, tick}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to create timer handler: " + handlerType.getName(),
                ex));
        }
    }

    private CompletionStage<Void> invokeVoidMethodHandler(
        Class<?> handlerType,
        Method method,
        Object[] arguments,
        String failureMessage) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker
                .invoke(handler, method, arguments, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                failureMessage + ": " + handlerType.getName() + "." + method.getName(),
                ex));
        }
    }

    private CompletionStage<Object> invokeReplyMethodHandler(
        Class<?> handlerType,
        Method method,
        Object[] arguments,
        String failureMessage) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker.invoke(
                handler,
                method,
                arguments,
                suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                failureMessage + ": " + handlerType.getName() + "." + method.getName(),
                ex));
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeActorSendInterfaceHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        ZLinkSpotActorSendContext dispatchContext,
        Object message,
        String failureMessage) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(
                    handler,
                    "handle",
                    new Object[] {
                        spotSurface,
                        actor,
                        dispatchContext,
                        message,
                        dispatchContext.cancellationToken()
                    },
                    suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                failureMessage + ": " + registration.handlerType().getName(),
                ex));
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Object> invokeActorRequestInterfaceHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        ZLinkSpotActorRequestContext dispatchContext,
        Object message,
        String failureMessage) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker.invokeHandler(
                handler,
                "handle",
                new Object[] {
                    spotSurface,
                    actor,
                    dispatchContext,
                    message,
                    dispatchContext.cancellationToken()
                },
                suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                failureMessage + ": " + registration.handlerType().getName(),
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
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(), new Object[] {spot, message}, suspendHandlerInvokers)
                    .thenApply(ignored -> null);
            }
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {spot, message}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT packet handler: "
                    + registration.handlerType().getName(),
                ex));
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
            if (registration.handlerMethod() != null) {
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(), new Object[] {spot, message}, suspendHandlerInvokers)
                    .thenApply(serializer::serialize);
            }
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {spot, message}, suspendHandlerInvokers)
                .thenApply(serializer::serialize);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT request handler: "
                    + registration.handlerType().getName(),
                ex));
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
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(), new Object[] {spot, message}, suspendHandlerInvokers)
                    .thenApply(ignored -> null);
            }
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {spot, message}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT subscription handler: "
                    + registration.handlerType().getName(),
                ex));
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
        public ZLinkSendCall sendToSpot(RoutingId spotRid, Object message) {
            requireRoutingId(spotRid);
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            if (channels != null && !egressChannels.isEmpty()) {
                if (egressChannels.size() > 1) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT outbound requires a single egress channel until remote address resolution is implemented");
                }
                return new EgressSpotSendCall(
                    channels,
                    egressChannels.get(0),
                    spotRid,
                    encoded.payload(),
                    Optional.of(encoded.packetName()));
            }
            return new SpotToSpotSendCall(
                backendSpot,
                nodeRid,
                spotRid,
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }

        @Override
        public ZLinkRequestCall requestToSpot(RoutingId spotRid, Object request) {
            requireRoutingId(spotRid);
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, request);
            if (channels != null && !egressChannels.isEmpty()) {
                if (egressChannels.size() > 1) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT outbound requires a single egress channel until remote address resolution is implemented");
                }
                return new EgressSpotRequestCall(
                    channels,
                    egressChannels.get(0),
                    spotRid,
                    encoded.payload(),
                    Optional.of(encoded.packetName()),
                    defaultRequestTimeout);
            }
            return new SpotToSpotRequestCall(
                backendSpot,
                nodeRid,
                spotRid,
                encoded.payload(),
                Optional.of(encoded.packetName()),
                defaultRequestTimeout);
        }

        @Override
        public ZLinkPublishCall publish(String topic, Object message) {
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            return new SpotPublishCall(
                backendSpot,
                topic,
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }

        @Override
        public ZLinkSendCall sendToChannel(String channelName, Object message) {
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            return new SpotChannelSendCall(
                backendSpot,
                channelName,
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }

        @Override
        public ZLinkRequestCall requestToChannel(String channelName, Object request) {
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, request);
            return new SpotChannelRequestCall(
                backendSpot,
                channelName,
                encoded.payload(),
                Optional.of(encoded.packetName()),
                defaultRequestTimeout);
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
        public CompletionStage<Void> submit() {
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
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
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
        public ZLinkSendCall sendToSpot(RoutingId spotRid, Object message) {
            return requireCurrentOutbound().sendToSpot(spotRid, message);
        }

        @Override
        public ZLinkRequestCall requestToSpot(
            RoutingId spotRid,
            Object request) {
            return requireCurrentOutbound().requestToSpot(spotRid, request);
        }

        @Override
        public ZLinkPublishCall publish(String topic, Object message) {
            return requireCurrentOutbound().publish(topic, message);
        }

        @Override
        public ZLinkSendCall sendToChannel(
            String channelName,
            Object message) {
            return requireCurrentOutbound().sendToChannel(channelName, message);
        }

        @Override
        public ZLinkRequestCall requestToChannel(
            String channelName,
            Object request) {
            return requireCurrentOutbound().requestToChannel(channelName, request);
        }
    }

    private final class DefaultSpotPublisherClient implements ZLinkSpotPublisherClient {
        @Override
        public ZLinkPublishCall publishSpot(
            String channelName,
            String topic,
            Object message) {
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
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            return new ExternalSpotPublishCall(
                channelName,
                topic,
                encoded.payload(),
                Optional.of(encoded.packetName()));
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
        public CompletionStage<Void> submit() {
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
        public CompletionStage<Void> submit() {
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
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
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
            // The backend reply callback completes the internal future on the
            // backend socket thread. Hop to the handler executor so user
            // continuations never run on the backend callback thread.
            return result.thenApplyAsync(reply -> reply, handlerExecutor);
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
        public CompletionStage<Void> submit() {
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
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
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
        long deadline = System.nanoTime() + defaultRequestTimeout.toNanos();
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
                    if (node.sendActorBoundSession(actor, List.of(frame), SendFlags.NONE)) {
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

    private void reportDispatchError(
        ZLinkDispatchErrorSurface surface,
        ZLinkDispatchMessageKind messageKind,
        ZLinkDispatchErrorReason reason,
        ZLinkDispatchErrorAction action,
        String packetName,
        String channelName,
        String topic,
        RoutingId spotRid,
        String actorId,
        RoutingId sourceRid,
        String correlationId,
        Throwable exception) {
        dispatchErrors.report(new ZLinkMessageDispatchErrorEvent(
            surface,
            messageKind,
            reason,
            action,
            packetName == null || packetName.isBlank() ? null : packetName,
            channelName,
            topic,
            spotRid == null ? null : spotRid.toString(),
            actorId,
            sourceRid == null ? null : sourceRid.toString(),
            correlationId,
            exception));
    }

    private static String errorMessage(Throwable error) {
        Throwable current = error;
        while ((current instanceof CompletionException
            || current instanceof InvocationTargetException)
            && current.getCause() != null) {
            current = current.getCause();
        }
        if (current instanceof ZLinkConfigurationException
            && current.getCause() != null
            && current.getMessage() != null
            && current.getMessage().startsWith("failed to invoke ")) {
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
        public CompletionStage<Void> submit() {
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

    private interface ScannedTimerRegistrar {
        CompletionStage<ZLinkTimer> addTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options);
    }

    private void registerScannedSpotHandlers(
        Class<?> spotType,
        Map<String, SpotPacketHandlerRegistration> packetHandlers,
        Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers,
        ScannedTimerRegistrar timerRegistrar) {
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (!isScannedHandlerForSpot(handler, spotType)) {
                continue;
            }
            if (handler.kind() == ZLinkScannedHandlerKind.SEND
                || handler.kind() == ZLinkScannedHandlerKind.REQUEST) {
                addConfiguredPacketHandler(
                    packetHandlers,
                    createScannedSpotPacketRegistration(handler));
                continue;
            }
            if (handler.kind() == ZLinkScannedHandlerKind.PUBLISH) {
                SpotSubscriptionHandlerRegistration registration =
                    createScannedSpotSubscriptionRegistration(handler);
                subscriptionHandlers
                    .computeIfAbsent(registration.topic(), ignored -> new ArrayList<>())
                    .add(registration);
                continue;
            }
            if (handler.kind() == ZLinkScannedHandlerKind.TIMER) {
                timerRegistrar.addTimer(
                    handler.timerName(),
                    handler.timerPeriod(),
                    handler.handlerType(),
                    new ZLinkTimerOptions());
            }
        }
    }

    private static boolean isScannedHandlerForSpot(
        ZLinkScannedHandler handler,
        Class<?> spotType) {
        return handler.surface() == ZLinkScannedHandlerSurface.SPOT
            && handler.spotType() == spotType
            && (handler.kind() == ZLinkScannedHandlerKind.SEND
                || handler.kind() == ZLinkScannedHandlerKind.REQUEST
                || handler.kind() == ZLinkScannedHandlerKind.PUBLISH
                || handler.kind() == ZLinkScannedHandlerKind.TIMER);
    }

    private static SpotPacketHandlerRegistration createScannedSpotPacketRegistration(
        ZLinkScannedHandler handler) {
        return new SpotPacketHandlerRegistration(
            handler.handlerType(),
            handler.handlerMethod(),
            handler.spotType(),
            handler.messageType(),
            handler.replyType(),
            handler.packetName(),
            handler.kind() == ZLinkScannedHandlerKind.REQUEST);
    }

    private static SpotSubscriptionHandlerRegistration createScannedSpotSubscriptionRegistration(
        ZLinkScannedHandler handler) {
        return new SpotSubscriptionHandlerRegistration(
            handler.topic(),
            handler.handlerType(),
            handler.handlerMethod(),
            handler.spotType(),
            handler.messageType(),
            handler.packetName());
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
        if (isActorPacketHandlerType(handlerType)) {
            addConfiguredActorPacketHandler(handlerType);
            matched = true;
        }
        for (Method method : handlerType.getMethods()) {
            rejectConflictingSpotActorAnnotations(handlerType, method);

            if (method.getAnnotation(ZLinkSpotActorSend.class) != null) {
                addConfiguredActorPacketHandler(
                    handlerType,
                    expectedSpotType,
                    method,
                    ZLinkScannedHandlerKind.ACTOR_SEND);
                matched = true;
            }
            if (method.getAnnotation(ZLinkSpotActorRequest.class) != null) {
                addConfiguredActorPacketHandler(
                    handlerType,
                    expectedSpotType,
                    method,
                    ZLinkScannedHandlerKind.ACTOR_REQUEST);
                matched = true;
            }
        }
        if (!matched) {
            throw new ZLinkConfigurationException(
                "SPOT handler must declare a SPOT or actor handler contract: "
                    + handlerType.getName());
        }
    }

    private static void rejectConflictingSpotActorAnnotations(Class<?> handlerType, Method method) {
        if (method.getAnnotation(ZLinkSpotActorSend.class) != null
            && method.getAnnotation(ZLinkSpotActorRequest.class) != null) {
            throw new ZLinkConfigurationException(
                "SPOT actor handler method cannot declare both send and request annotations: "
                    + handlerType.getName() + "." + method.getName());
        }
    }

    private static boolean isSpotPacketHandlerType(Class<?> handlerType) {
        if (findInterface(handlerType, ZLinkSpotPacketHandler.class) != null
            || findInterface(handlerType, ZLinkSpotRequestHandler.class) != null
            || findInterface(handlerType, KOTLIN_SPOT_PACKET_HANDLER) != null
            || findInterface(handlerType, KOTLIN_SPOT_REQUEST_HANDLER) != null) {
            return true;
        }
        for (Method method : handlerType.getMethods()) {
            if (method.getAnnotation(ZLinkSpotRequest.class) != null) {
                return true;
            }
        }
        return false;
    }

    private static boolean isActorPacketHandlerType(Class<?> handlerType) {
        return findInterface(handlerType, ZLinkEntrySpotActorSendHandler.class) != null
            || findInterface(handlerType, ZLinkEntrySpotActorRequestHandler.class) != null
            || findInterface(handlerType, ZLinkSpotActorSendHandler.class) != null
            || findInterface(handlerType, ZLinkSpotActorRequestHandler.class) != null
            || findInterface(handlerType, KOTLIN_ENTRY_SPOT_ACTOR_SEND_HANDLER) != null
            || findInterface(handlerType, KOTLIN_ENTRY_SPOT_ACTOR_REQUEST_HANDLER) != null
            || findInterface(handlerType, KOTLIN_SPOT_ACTOR_SEND_HANDLER) != null
            || findInterface(handlerType, KOTLIN_SPOT_ACTOR_REQUEST_HANDLER) != null;
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

    private void addConfiguredActorPacketHandler(
        Class<?> handlerType,
        Class<?> expectedSpotType,
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
                expectedSpotType,
                shape.actorType(),
                shape.messageType(),
                replyType,
                packetName,
                kind);
        addActorPacketRegistration(actorPacketHandlers, registration);
    }

    private void addConfiguredActorPacketHandler(Class<?> handlerType) {
        addConfiguredActorPacketHandler(
            handlerType,
            ZLinkScannedHandlerKind.ACTOR_SEND,
            findInterface(handlerType, ZLinkEntrySpotActorSendHandler.class),
            findInterface(handlerType, ZLinkSpotActorSendHandler.class));
        addConfiguredActorPacketHandler(
            handlerType,
            ZLinkScannedHandlerKind.ACTOR_SEND,
            findInterface(handlerType, KOTLIN_ENTRY_SPOT_ACTOR_SEND_HANDLER),
            findInterface(handlerType, KOTLIN_SPOT_ACTOR_SEND_HANDLER));
        addConfiguredActorPacketHandler(
            handlerType,
            ZLinkScannedHandlerKind.ACTOR_REQUEST,
            findInterface(handlerType, ZLinkEntrySpotActorRequestHandler.class),
            findInterface(handlerType, ZLinkSpotActorRequestHandler.class));
        addConfiguredActorPacketHandler(
            handlerType,
            ZLinkScannedHandlerKind.ACTOR_REQUEST,
            findInterface(handlerType, KOTLIN_ENTRY_SPOT_ACTOR_REQUEST_HANDLER),
            findInterface(handlerType, KOTLIN_SPOT_ACTOR_REQUEST_HANDLER));
    }

    private void addConfiguredActorPacketHandler(
        Class<?> handlerType,
        ZLinkScannedHandlerKind kind,
        ParameterizedType entryInterface,
        ParameterizedType spotInterface) {
        ParameterizedType matched = entryInterface != null ? entryInterface : spotInterface;
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> actorType = requireClassArgument(handlerType, arguments[1]);
        Class<?> messageType = requireClassArgument(handlerType, arguments[2]);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.ACTOR_REQUEST
            ? requireClassArgument(handlerType, arguments[3])
            : Void.class;
        String packetName = resolvePacketName(messageType);
        SpotActorPacketHandlerRegistration registration =
            new SpotActorPacketHandlerRegistration(
                handlerType,
                null,
                requireClassArgument(handlerType, arguments[0]),
                actorType,
                messageType,
                replyType,
                packetName,
                kind);
        addActorPacketRegistration(actorPacketHandlers, registration);
    }

    private static SpotPacketHandlerRegistration createSpotPacketRegistration(
        Class<?> handlerType,
        Class<?> expectedSpotType) {
        ParameterizedType packet = findInterface(handlerType, ZLinkSpotPacketHandler.class);
        if (packet == null) {
            packet = findInterface(handlerType, KOTLIN_SPOT_PACKET_HANDLER);
        }
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
        if (request == null) {
            request = findInterface(handlerType, KOTLIN_SPOT_REQUEST_HANDLER);
        }
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
            Class<?>[] parameters = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
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
        if (subscription == null) {
            subscription = findInterface(handlerType, KOTLIN_SPOT_SUBSCRIPTION_HANDLER);
        }
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
            Class<?>[] parameters = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
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

    private static ActorMessageShape actorPacketHandlerShape(
        Class<?> handlerType,
        Method method,
        Class<? extends ZLinkHandlerContext> contextType) {
        Class<?>[] parameters = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        if (parameters.length == 2) {
            return new ActorMessageShape(null, parameters[0], parameters[1]);
        }
        if (parameters.length == 5
            && parameters[2].isAssignableFrom(contextType)
            && parameters[4] == CancellationToken.class) {
            return new ActorMessageShape(parameters[0], parameters[1], parameters[3]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor packet handler method must have actor/message or spot, actor, context, message, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private record ActorMessageShape(Class<?> spotType, Class<?> actorType, Class<?> messageType) {
    }

    static Object[] actorPacketArguments(
        Method method,
        Object spot,
        ZLinkActor actor,
        ZLinkHandlerContext context,
        Object message) {
        Class<?>[] parameterTypes = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        if (parameterTypes.length == 2) {
            return new Object[] {actor, message};
        }
        return new Object[] {spot, actor, context, message, context.cancellationToken()};
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
        return ZLinkGenericTypeResolver.findInterface(type, targetRawType);
    }

    private static ParameterizedType findInterface(Class<?> type, String targetRawTypeName) {
        return ZLinkGenericTypeResolver.findInterface(type, targetRawTypeName);
    }

    private static Class<?> requireClassArgument(Class<?> handlerType, Type argument) {
        return ZLinkGenericTypeResolver.requireClassArgument(handlerType, argument);
    }

    private static Class<?> resolveReplyType(Class<?> handlerType, Method method) {
        if (ZLinkHandlerMethodInvoker.isKotlinSuspendMethod(method)) {
            return ZLinkHandlerMethodInvoker.kotlinSuspendReplyType(handlerType, method);
        }
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
        private final ZLinkSpot<?> spot;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotContext context;
        private ZLinkBackendActorReceived pendingActorHeader;

        SpotActivation(
            ZLinkSpot<?> spot,
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
            context.enqueueDispatch(() -> dispatchEventAsync(info)
                .whenComplete((ignored, error) -> {
                    for (ZLinkBackendActorReceived actorMessage : info.actorMessages()) {
                        actorMessage.close();
                    }
                }));
        }

        private CompletionStage<Void> dispatchEventAsync(ZLinkBackendSpotDispatchInfo info) {
            if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                return drainRoutesAsync();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE) {
                return drainSubscriptionsAsync();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE) {
                return drainUnhandledActorJoinsAsync();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                return dispatchActorMessagesAsync(info.actorMessages());
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE) {
                return drainActorLifecycleEventsAsync();
            }
            return CompletableFuture.completedFuture(null);
        }

        private CompletionStage<Void> drainRoutesAsync() {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            while (true) {
                ZLinkBackendReceived received =
                    backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return tail;
                }
                tail = tail.thenCompose(ignored -> dispatchRouteAsync(received));
            }
        }

        private CompletionStage<Void> dispatchRouteAsync(ZLinkBackendReceived received) {
            if (received.parts().isEmpty()) {
                received.close();
                return CompletableFuture.completedFuture(null);
            }
            ParsedPacket packet = parsePacket(received.parts());
            if (ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME.equals(packet.packetName())) {
                return dispatchRoutedActorJoinAsync(received, packet);
            }
            SpotPacketHandlerRegistration handler =
                context.packetHandler(packet.packetName());
            if (handler == null) {
                if (received.requestSeq().isPresent()) {
                    received.reply(List.of(Message.from((
                        "No SPOT route request handler is registered for '"
                            + packet.packetName() + "'.").getBytes(StandardCharsets.UTF_8))));
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.REQUEST,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.REPLY_ERROR,
                        packet.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        null,
                        received.routingId().orElse(null),
                        received.requestSeq().map(Object::toString).orElse(null),
                        null);
                } else {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.SEND,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.DROP,
                        packet.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        null,
                        received.routingId().orElse(null),
                        null,
                        null);
                }
                received.close();
                return CompletableFuture.completedFuture(null);
            }
            if (received.requestSeq().isPresent()) {
                if (!handler.request()) {
                    received.reply(List.of(Message.from((
                        "SPOT route handler for '" + packet.packetName()
                            + "' does not accept requests.").getBytes(StandardCharsets.UTF_8))));
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.REQUEST,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.REPLY_ERROR,
                        packet.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        null,
                        received.routingId().orElse(null),
                        received.requestSeq().map(Object::toString).orElse(null),
                        null);
                    received.close();
                    return CompletableFuture.completedFuture(null);
                }
                Message payloadCopy = Message.from(packet.payload());
                return withCurrentOutbound(context.outbound, () ->
                    invokeSpotRequestHandler(handler, spot, payloadCopy))
                    .thenAccept(reply -> received.reply(List.of(reply)))
                    .whenComplete((ignored, error) -> {
                        payloadCopy.close();
                        received.close();
                    });
            }
            if (handler.request()) {
                reportDispatchError(
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    ZLinkDispatchMessageKind.SEND,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    packet.packetName(),
                    null,
                    null,
                    backendSpot.routingId(),
                    null,
                    received.routingId().orElse(null),
                    null,
                    null);
                received.close();
                return CompletableFuture.completedFuture(null);
            }
            Message payloadCopy = Message.from(packet.payload());
            received.close();
            return withCurrentOutbound(context.outbound, () ->
                invokeSpotPacketHandler(handler, spot, payloadCopy))
                .whenComplete((ignored, error) -> payloadCopy.close());
        }

        private CompletionStage<Void> drainSubscriptionsAsync() {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            while (true) {
                ZLinkBackendTopicMessage received =
                    backendSpot.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return tail;
                }
                tail = tail.thenCompose(ignored -> dispatchSubscriptionAsync(received));
            }
        }

        private CompletionStage<Void> dispatchSubscriptionAsync(ZLinkBackendTopicMessage received) {
            try {
                if (received.parts().isEmpty()) {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                        ZLinkDispatchMessageKind.PUBLISH,
                        ZLinkDispatchErrorReason.INVALID_FRAME,
                        ZLinkDispatchErrorAction.DROP,
                        null,
                        null,
                        received.topic(),
                        backendSpot.routingId(),
                        null,
                        null,
                        null,
                        null);
                    return CompletableFuture.completedFuture(null);
                }
                ParsedPacket packet = parsePacket(received.parts());
                CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
                boolean dispatched = false;
                for (SpotSubscriptionHandlerRegistration handler :
                    context.subscriptionHandlers(received.topic())) {
                    if (!handler.packetName().equals(packet.packetName())) {
                        continue;
                    }
                    dispatched = true;
                    Message payloadCopy = Message.from(packet.payload());
                    tail = tail.thenCompose(ignored ->
                        withCurrentOutbound(context.outbound, () ->
                            invokeSpotSubscriptionHandler(handler, spot, payloadCopy))
                            .whenComplete((ignored2, error) -> payloadCopy.close()));
                }
                if (!dispatched) {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                        ZLinkDispatchMessageKind.PUBLISH,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        ZLinkDispatchErrorAction.DROP,
                        packet.packetName(),
                        null,
                        received.topic(),
                        backendSpot.routingId(),
                        null,
                        null,
                        null,
                        null);
                }
                return tail;
            } finally {
                received.parts().forEach(Message::close);
            }
        }

        private CompletionStage<Void> drainActorLifecycleEventsAsync() {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            while (true) {
                if (closing) {
                    return tail;
                }
                ZLinkBackendActorLifecycleEvent event =
                    backendSpot.recvActorLifecycle(ZLinkBackendRecvMode.DONT_WAIT);
                if (event == null) {
                    return tail;
                }
                if (actorRuntime == null) {
                    continue;
                }
                ZLinkBackendActorRef actorRef = event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
                    ? event.info().previousActor()
                    : event.info().currentActor();
                Optional<ZLinkActor> actor = actorRuntime.localActor(actorRef.actorId());
                if (actor.isPresent()) {
                    tail = tail.thenCompose(ignored -> dispatchActorLifecycleAsync(event, actorRef, actor.get()));
                }
            }
        }

        private CompletionStage<Void> dispatchActorLifecycleAsync(
            ZLinkBackendActorLifecycleEvent event,
            ZLinkBackendActorRef actorRef,
            ZLinkActor actor) {
            if (closing) {
                return CompletableFuture.completedFuture(null);
            }
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT) {
                actorRuntime.markLeft(actor);
                return actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> notifySpotActorLifecycle(spot, actor, false));
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            actorRuntime.markJoined(
                actor,
                actorRef,
                spotRid,
                spotSurfaceFor(spotRid) instanceof ZLinkSpot<?> spot ? spot : null);
            return actorRuntime.submitActorDispatch(
                actor.actorId(),
                () -> notifySpotActorLifecycle(spot, actor, true));
        }

        private CompletionStage<Void> dispatchActorMessagesAsync(List<ZLinkBackendActorReceived> actorMessages) {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
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
                    return tail;
                }
                if (pendingHeader) {
                    pendingActorHeader = null;
                }
                ActorPacketHeader packetHeader = decodeActorPacketHeader(headerPart);
                if (actorRuntime == null) {
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                Optional<ZLinkActor> localActor =
                    actorRuntime.localActor(headerPart.actor().actorId());
                if (localActor.isEmpty()) {
                    boolean request = packetHeader.requestSeq().isPresent();
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ACTOR,
                        request
                            ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                            : ZLinkDispatchMessageKind.ACTOR_SEND,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        request
                            ? ZLinkDispatchErrorAction.REPLY_ERROR
                            : ZLinkDispatchErrorAction.DROP,
                        packetHeader.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        headerPart.actor().actorId(),
                        headerPart.sourceNodeRid(),
                        packetHeader.requestSeq().map(Object::toString).orElse(null),
                        null);
                    if (request) {
                        ZLinkBackendActorReceived headerCopy = pendingHeader
                            ? headerPart
                            : copyActorReceived(headerPart);
                        replyActorDispatchError(
                            packetHeader,
                            headerCopy,
                            headerPart.actor().actorId(),
                            new ZLinkConfigurationException(
                                "SPOT actor is not registered locally: "
                                    + headerPart.actor().actorId()),
                            "user actor missing error reply failed");
                    } else if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                ZLinkActor actor = localActor.get();
                SpotActorPacketHandlerRegistration handler =
                    resolveActorPacketHandler(
                        packetHeader.packetName(),
                        spot,
                        packetHeader.requestSeq().isPresent()
                            ? ZLinkScannedHandlerKind.ACTOR_REQUEST
                            : ZLinkScannedHandlerKind.ACTOR_SEND);
                if (handler == null) {
                    boolean request = packetHeader.requestSeq().isPresent();
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ACTOR,
                        request
                            ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                            : ZLinkDispatchMessageKind.ACTOR_SEND,
                        ZLinkDispatchErrorReason.HANDLER_MISSING,
                        request
                            ? ZLinkDispatchErrorAction.REPLY_ERROR
                            : ZLinkDispatchErrorAction.DROP,
                        packetHeader.packetName(),
                        null,
                        null,
                        backendSpot.routingId(),
                        actor.actorId(),
                        headerPart.sourceNodeRid(),
                        packetHeader.requestSeq().map(Object::toString).orElse(null),
                        null);
                    if (request) {
                        ZLinkBackendActorReceived headerCopy = pendingHeader
                            ? headerPart
                            : copyActorReceived(headerPart);
                        replyActorDispatchError(
                            packetHeader,
                            headerCopy,
                            actor.actorId(),
                            new ZLinkConfigurationException(
                                "No SPOT actor request handler is registered for '"
                                    + packetHeader.packetName() + "'."),
                            "user actor dispatch error reply failed");
                    } else if (pendingHeader) {
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
                tail = tail.thenCompose(ignored -> actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> dispatchActorPacket(
                        handler,
                        actor,
                        packetHeader,
                        headerCopy,
                        payloadCopy)));
            }
            return tail;
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            ZLinkActor actor,
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            actorRuntime.bindNativeSession(
                actor,
                primaryNode,
                headerPart.actor(),
                headerPart.sourceNodeRid(),
                headerPart.sourceSessionRid());
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

        private void replyActorDispatchError(
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            String actorId,
            Throwable error,
            String failureMessage) {
            byte[] frameBytes;
            try (Message frame = encodeActorErrorFrame(packetHeader, error)) {
                frameBytes = frame.toByteArray();
            } catch (RuntimeException ex) {
                headerPart.close();
                return;
            }
            context.enqueueDispatch(() -> sendActorBoundSessionWithRetry(
                    primaryNode,
                    new ZLinkBackendActorRef(
                        headerPart.actor().nodeRid(),
                        actorId,
                        headerPart.actor().epoch()),
                    actorId,
                    frameBytes,
                    failureMessage)
                .whenComplete((ignored, sendError) -> headerPart.close()));
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

        private CompletionStage<Void> drainUnhandledActorJoinsAsync() {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            while (true) {
                ZLinkBackendActorJoinRequest request =
                    backendSpot.recvActorJoin(ZLinkBackendRecvMode.DONT_WAIT);
                if (request == null) {
                    return tail;
                }
                tail = tail.thenCompose(ignored -> dispatchActorJoinAsync(request));
            }
        }

        private CompletionStage<Void> dispatchActorJoinAsync(ZLinkBackendActorJoinRequest request) {
            Message payloadCopy = actorJoinPayload(request.parts());
            request.parts().forEach(Message::close);
            return withCurrentOutbound(context.outbound, () ->
                invokeActorJoinCallback(request, payloadCopy))
                .handle((response, error) -> {
                    if (error != null) {
                        try (Message emptyReply = Message.from(new byte[0])) {
                            backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                        }
                        return null;
                    }
                    ZLinkSpotActorJoinResponse effective =
                        response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                    Message reply = effective.reply() == null
                        ? Message.from(new byte[0])
                        : Message.from(effective.reply().toByteArray());
                    try {
                        backendSpot.replyActorJoin(
                            request,
                            effective.accepted() ? 0 : 1,
                            List.of(reply));
                    } finally {
                        reply.close();
                    }
                    return null;
                })
                .thenApply(ignored -> (Void) null)
                .whenComplete((ignored, error) -> payloadCopy.close());
        }

        private Message actorJoinPayload(List<Message> parts) {
            if (parts.isEmpty()) {
                return Message.from(new byte[0]);
            }
            if (parts.size() >= 3
                && ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME.equals(parts.get(0).toUtf8String())) {
                return Message.from(parts.get(2).toByteArray());
            }
            return Message.from(parts.get(0).toByteArray());
        }

        @SuppressWarnings({"rawtypes", "unchecked"})
        private CompletionStage<Void> dispatchRoutedActorJoinAsync(
            ZLinkBackendReceived received,
            ParsedPacket packet) {
            return handleRoutedActorJoinParts(null, null, received.parts())
                .thenAccept(replyParts -> {
                    try {
                        received.reply(replyParts);
                    } finally {
                        replyParts.forEach(Message::close);
                    }
                })
                .whenComplete((ignored, error) -> received.close());
        }

        @SuppressWarnings({"rawtypes", "unchecked"})
        private CompletionStage<Optional<Message>> handleRoutedActorPacketParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.ActorPacket packet =
                ZLinkActorSpotRoutePackets.decodeActorPacket(parts);
            return dispatchLocalSessionActor(packet.actorRef(), packet.header(), packet.payload())
                .whenComplete((ignored, error) -> packet.close());
        }

        @SuppressWarnings({"rawtypes", "unchecked"})
        private CompletionStage<List<Message>> handleRoutedActorJoinParts(
            String routeChannelName,
            RoutingId sourcePeerRid,
            List<Message> parts) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for routed Spot actor join"));
            }
            ParsedPacket packet = parsePacket(parts);
            ZLinkActorSpotRoutePackets.JoinRequest joinRequest =
                ZLinkActorSpotRoutePackets.decodeJoinRequest(packet.payload());
            Message joinPayload = parts.size() > 2
                ? Message.from(parts.get(2).toByteArray())
                : Message.from(new byte[0]);
            return actorRuntime.getOrCreate(joinRequest.actorId(), joinRequest.actorType())
                .thenCompose(actor -> {
                    final long[] routedBindingToken = {-1};
                    ZLinkBackendActorRef localActorRef = actorRuntime.actorRef(actor);
                    if (joinRequest.hasSourceSessionRoute()) {
                        RoutingId sourceNodeRid = sourcePeerRid == null
                            ? joinRequest.sourceNodeRid()
                            : sourcePeerRid;
                        routedBindingToken[0] = actorRuntime.bindNativeSession(
                            actor,
                            primaryNode,
                            localActorRef,
                            sourceNodeRid,
                            joinRequest.sourceSessionRid());
                    } else if (routeChannelName != null) {
                        routedBindingToken[0] = actorRuntime.bindRoutedSession(
                            actor,
                            routeChannelName,
                            sourcePeerRid == null ? joinRequest.actorRef().nodeRid() : sourcePeerRid,
                            joinRequest.sourceEntrySpotRid(),
                            joinRequest.actorRef());
                    }
                    return ZLinkHandlerStages
                        .fromSupplier(() -> ((ZLinkSpot) spot).onActorJoin(actor, joinPayload, NONE_CANCELLATION))
                        .thenCompose(response -> {
                            ZLinkSpotActorJoinResponse effective =
                                response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                            if (!effective.accepted()) {
                                if (routedBindingToken[0] >= 0) {
                                    actorRuntime.clearSessionBinding(actor, routedBindingToken[0]);
                                }
                                return CompletableFuture.completedFuture(effective);
                            }
                            actorRuntime.markJoined(
                                actor,
                                localActorRef,
                                backendSpot.routingId(),
                                spotFor(backendSpot.routingId()));
                            return notifySpotActorLifecycle(spot, actor, true)
                                .thenApply(ignored -> effective)
                                .whenComplete((reply, error) -> {
                                    if (error != null) {
                                        actorRuntime.markLeft(actor);
                                        if (routedBindingToken[0] >= 0) {
                                            actorRuntime.clearSessionBinding(actor, routedBindingToken[0]);
                                        }
                                    }
                                });
                        })
                        .whenComplete((ignored, error) -> {
                            if (error != null && routedBindingToken[0] >= 0) {
                                actorRuntime.clearSessionBinding(actor, routedBindingToken[0]);
                            }
                        })
                        .thenApply(response -> {
                            ZLinkSpotActorJoinResponse effective =
                                response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                            return ZLinkActorSpotRoutePackets.encodeJoinReply(
                                effective.accepted(),
                                localActorRef,
                                effective.reply());
                        });
                })
                .handle((reply, error) -> {
                    try {
                        if (error != null) {
                            throw new CompletionException(error);
                        } else {
                            try (reply) {
                                return List.of(Message.from(reply.toByteArray()));
                            }
                        }
                    } finally {
                        joinPayload.close();
                    }
                });
        }

        @SuppressWarnings({"rawtypes", "unchecked"})
        private CompletionStage<ZLinkSpotActorJoinResponse> invokeActorJoinCallback(
            ZLinkBackendActorJoinRequest request,
            Message payload) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor join callback"));
            }
            return actorRuntime
                .getOrCreateLocalActor(
                    request.targetActor().actorId(),
                    ZLinkActor.class)
                .thenCompose(actor -> {
                    if (actor.isEmpty()) {
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "Spot actor join callback target actor is not available: "
                                + request.targetActor().actorId()));
                    }
                    return ZLinkHandlerStages
                        .fromSupplier(() -> ((ZLinkSpot) spot).onActorJoin(actor.get(), payload, NONE_CANCELLATION))
                        .thenCompose(response -> {
                            ZLinkSpotActorJoinResponse effective =
                                response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                            if (!effective.accepted()) {
                                return CompletableFuture.completedFuture(effective);
                            }
                            actorRuntime.markJoined(
                                actor.get(),
                                request.targetActor(),
                                backendSpot.routingId(),
                                spotFor(backendSpot.routingId()));
                            return notifySpotActorLifecycle(spot, actor.get(), true)
                                .thenApply(ignored -> effective)
                                .whenComplete((reply, error) -> {
                                    if (error != null) {
                                        actorRuntime.markLeft(actor.get());
                                    }
                                });
                        });
                });
        }

        private CompletionStage<Void> invokeActorSendHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            Object message = serializer.deserialize(payload, registration.messageType());
            if (registration.handlerMethod() == null) {
                return invokeActorSendInterfaceHandler(
                    registration,
                    spot,
                    actor,
                    new DefaultSpotActorSendContext(registration.packetName()),
                    message,
                    "failed to invoke Spot actor send handler");
            }
            return invokeVoidMethodHandler(
                registration.handlerType(),
                registration.handlerMethod(),
                actorPacketArguments(
                    registration.handlerMethod(),
                    spot,
                    actor,
                    new DefaultSpotActorSendContext(registration.packetName()),
                    message),
                "failed to invoke Spot actor send handler");
        }

        private CompletionStage<Optional<Message>> invokeActorRequestHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            Object message = serializer.deserialize(payload, registration.messageType());
            if (registration.handlerMethod() == null) {
                return invokeActorRequestInterfaceHandler(
                        registration,
                        spot,
                        actor,
                        new DefaultSpotActorRequestContext(registration.packetName()),
                        message,
                        "failed to invoke Spot actor request handler")
                    .thenApply(reply -> Optional.of(serializer.serialize(reply)));
            }
            return invokeReplyMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    actorPacketArguments(
                        registration.handlerMethod(),
                        spot,
                        actor,
                        new DefaultSpotActorRequestContext(registration.packetName()),
                        message),
                    "failed to invoke Spot actor request handler")
                .thenApply(reply -> Optional.of(serializer.serialize(reply)));
        }

        @Override
        public void close() {
            if (spot == null) {
                closeResources();
                return;
            }
            try {
                awaitClosing(withCurrentOutbound(context.outbound, () ->
                    ZLinkHandlerStages.fromRunnable(spot::onClosing)));
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
                .get(defaultRequestTimeout.toMillis(), TimeUnit.MILLISECONDS);
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
