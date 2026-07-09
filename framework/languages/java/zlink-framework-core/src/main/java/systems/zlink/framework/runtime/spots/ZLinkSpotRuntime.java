package systems.zlink.framework.runtime.spots;

import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_ENTRY_SPOT_ACTOR_REQUEST_HANDLER;
import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_ENTRY_SPOT_ACTOR_SEND_HANDLER;
import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_SPOT_ACTOR_REQUEST_HANDLER;
import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_SPOT_ACTOR_SEND_HANDLER;
import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_SPOT_PACKET_HANDLER;
import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_SPOT_REQUEST_HANDLER;
import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_SPOT_SUBSCRIPTION_HANDLER;
import static systems.zlink.framework.runtime.handlers.ZLinkHandlerInterfaceNames.KOTLIN_SPOT_TIMER_HANDLER;

import systems.zlink.framework.runtime.backend.*;

import systems.zlink.framework.ZLinkAwait;
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
import systems.zlink.contracts.errors.ZlinkCloseException;
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
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkDispatchFailure;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.channels.ChannelRegistration;
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
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.messaging.ZLinkPacketNames;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
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
import systems.zlink.framework.spots.SpotRemoteRef;
import systems.zlink.framework.spots.SpotRemoteRefResolver;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkTimerOverrunPolicy;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

public final class ZLinkSpotRuntime implements ZLinkSpotManager, AutoCloseable {
    private static final int ACTOR_RECV_INFO_NO_BIND = 1;
    private static final String FRAMEWORK_ERROR_REPLY_MARKER = "ZLinkFrameworkError";

    private static final String REMOTE_BOUND_SESSION_BIND_PACKET_NAME =
        "zlink.framework.actor.bound_session.bind";

    private static final boolean STREAM_TRACE =
        "1".equals(System.getenv("ZLINK_JAVA_STREAM_TRACE"));
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
    private final boolean ownsContext;
    private final ZLinkFrameworkRegistration frameworkRegistration;
    private final List<ZLinkBackendSpotNode> nodes = new ArrayList<>();
    private final Map<String, ZLinkBackendSpotNode> nodesByName = new HashMap<>();
    private final Map<RoutingId, SpotNodeLocationMetadata> locationMetadataByNodeRid = new HashMap<>();
    private final Map<String, ZLinkBackendSpotNode> publisherNodesByChannel = new HashMap<>();
    private final Map<String, ZLinkBackendSpot> publisherSpotsByChannel = new HashMap<>();
    private final Set<Class<? extends ZLinkSpot<?>>> registeredSpotTypes = new HashSet<>();
    private final Map<RoutingId, SpotActivation> spots = new ConcurrentHashMap<>();
    private final List<EntrySpotActivation> entrySpots = new ArrayList<>();
    private final ZLinkBackendSpotNode primaryNode;
    private final String primaryNodeSourceName;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final ZLinkDispatchErrorReporter dispatchErrors;
    private final ZLinkRuntimeEventDispatcher eventDispatcher;
    private final Executor handlerExecutor;
    private final List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers;
    private final ZLinkScannedHandlerCatalog handlerCatalog;
    private final Map<String, List<SpotActorPacketHandlerRegistration>> actorPacketHandlers;
    private final Duration defaultRequestTimeout;
    private final ZLinkChannelRuntime channels;
    private ZLinkActorRuntime actorRuntime;
    private final List<ChannelRegistration> routeMeshChannels = new ArrayList<>();
    private final List<String> routerSpotNodeNames = new ArrayList<>();
    private final Set<RoutingId> manualRouterPeerNodeRids = ConcurrentHashMap.newKeySet();
    private final Set<RoutingId> autoConnectedRouterPeerNodeRids = ConcurrentHashMap.newKeySet();
    private final Set<String> suppressedActorLifecycleCallbacks = ConcurrentHashMap.newKeySet();
    private final ThreadLocal<DefaultSpotOutbound> currentOutbound = new ThreadLocal<>();
    private final ThreadLocal<DefaultEntrySpotContext> currentEntrySpotDispatch = new ThreadLocal<>();
    private SpotRemoteRefResolver remoteAddressResolver;
    private ZLinkLocationLifecycle locationLifecycle;
    private final Map<RoutingId, CompletionStage<ZLinkSpotCreateResult>> pendingSpotCreates =
        new ConcurrentHashMap<>();
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
        this(
            backendFactory,
            adapterOptions,
            registration,
            channels,
            serializer,
            handlerFactory,
            null);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        this(
            backendFactory,
            adapterOptions,
            registration,
            channels,
            null,
            true,
            serializer,
            handlerFactory,
            eventDispatcher);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkBackendContext context,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        this(
            backendFactory,
            adapterOptions,
            registration,
            channels,
            context,
            false,
            serializer,
            handlerFactory,
            eventDispatcher);
    }

    private ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkBackendContext context,
        boolean ownsContext,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        if (registration.spotNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one SpotNode is required");
        }
        this.frameworkRegistration = registration;
        this.channels = channels;
        this.serializer = java.util.Objects.requireNonNull(serializer, "serializer");
        this.handlerFactory = handlerFactory;
        this.eventDispatcher = eventDispatcher;
        this.handlerExecutor = java.util.Objects.requireNonNull(
            registration.handlerExecutor(),
            "handlerExecutor");
        this.dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.dispatchOptions(),
            handlerFactory,
            this.handlerExecutor,
            eventDispatcher);
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
        this.context = context == null ? channelAdapter.createContext() : context;
        this.ownsContext = ownsContext;
        this.defaultRequestTimeout = registration.defaultRequestTimeout();
        Map<String, ZLinkBackendSpotNode> routeBridgeNodesByName = new HashMap<>();
        for (SpotNodeRegistration nodeRegistration : registration.spotNodes()) {
            ZLinkBackendSpotNode node =
                spotAdapter.createSpotNode(this.context, resolveSpotNodeMode(nodeRegistration));
            if (nodeRegistration.nodeRoutingId() != null) {
                node.setRoutingId(nodeRegistration.nodeRoutingId());
                if (nodeRegistration.pubSubEnabled()) {
                    node.setPublisherRoutingId(deriveRoutingId(
                        nodeRegistration.nodeRoutingId(),
                        "pub"));
                    node.setSubscriberRoutingId(deriveRoutingId(
                        nodeRegistration.nodeRoutingId(),
                        "sub"));
                }
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
            for (SpotNodeRegistration.RouterManualConnection connection
                    : nodeRegistration.routerManualConnections()) {
                if (connection.peerRoutingId() != null) {
                    node.connectPeer(connection.peerRoutingId(), connection.endpoint());
                    manualRouterPeerNodeRids.add(connection.peerRoutingId());
                } else {
                    node.connectPeer(connection.endpoint());
                }
            }
            for (String endpoint : nodeRegistration.pubSubManualConnections()) {
                node.connectPeer(endpoint);
            }
            nodes.add(node);
            nodesByName.put(nodeRegistration.nodeName(), node);
            registeredSpotTypes.addAll(nodeRegistration.spotFactories());
            if (nodeRegistration.pubSubEnabled()) {
                publisherNodesByChannel.put(nodeRegistration.meshName(), node);
            }
            if (nodeRegistration.routerEnabled()) {
                routeBridgeNodesByName.put(nodeRegistration.nodeName(), node);
                routerSpotNodeNames.add(nodeRegistration.nodeName());
                if (channels != null) {
                    channels.registerSpotRouterNode(nodeRegistration.meshName(), node);
                    if (!nodeRegistration.meshName().equals(nodeRegistration.nodeName())) {
                        channels.registerSpotRouterNode(nodeRegistration.nodeName(), node);
                    }
                }
            }
            locationMetadataByNodeRid.put(
                node.routingId(),
                new SpotNodeLocationMetadata(
                    nodeRegistration.meshName(),
                    node.routingId(),
                    nodeRegistration.routerBind()));
        }
        for (var channel : registration.channels()) {
            if (channel.kind() == ChannelKind.ROUTE_MESH) {
                routeMeshChannels.add(channel);
            }
        }
        attachRouteMeshSpotBridges(routeBridgeNodesByName);
        this.primaryNode = nodes.get(0);
        this.primaryNodeSourceName = registration.spotNodes().get(0).nodeName();
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

    private static RoutingId deriveRoutingId(RoutingId base, String suffix) {
        byte[] baseBytes = base.toBytes();
        byte[] suffixBytes = suffix.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        if (baseBytes.length + 1 + suffixBytes.length > RoutingId.MAX_LENGTH) {
            throw new ZLinkConfigurationException(
                "derived routing id must be at most 255 bytes");
        }
        byte[] bytes = new byte[baseBytes.length + 1 + suffixBytes.length];
        System.arraycopy(baseBytes, 0, bytes, 0, baseBytes.length);
        System.arraycopy(suffixBytes, 0, bytes, baseBytes.length + 1, suffixBytes.length);
        return RoutingId.from(bytes);
    }

    private String spotPublisherChannelName(RoutingId nodeRid) {
        SpotNodeLocationMetadata metadata = locationMetadataByNodeRid.get(nodeRid);
        if (metadata == null || !publisherNodesByChannel.containsKey(metadata.meshName())) {
            return null;
        }
        return metadata.meshName();
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
                if (existing.handlerType() == registration.handlerType()) {
                    return;
                }
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
        return create(spotType, ZLinkMessage.empty());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkMessage request) {
        requireRegistered(spotType);
        return createBackendSpotAsync()
            .thenCompose(spot -> {
                RoutingId spotRid = spot.routingId();
                if (spots.containsKey(spotRid)) {
                    spot.close();
                    throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
                }
                return activateAsync(spotType, spot, request)
                    .thenCompose(result -> createResultAsync(spotRid, spotType, result));
            });
        }

    private CompletionStage<ZLinkSpotCreateResult> createResultAsync(
        RoutingId spotRid,
        Class<? extends ZLinkSpot<?>> spotType,
        SpotActivationCreateResult result) {
        if (!result.response().accepted()) {
            return CompletableFuture.completedFuture(new ZLinkSpotCreateResult(
                spotRid,
                ZLinkSpotCreateState.REJECTED,
                result.response().reply()));
        }
        SpotActivation activation = result.activation();
        return claimUserSpotLocationAsync(spotRid, spotType)
            .thenApply(status -> {
                if (status != ZLinkLocationWriteStatus.STORED) {
                    throw spotCreateLocationFailure(spotRid, status);
                }
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(
                    spotRid,
                    ZLinkSpotCreateState.CREATED,
                    result.response().reply());
            })
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    activation.close();
                }
            });
        }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        return createWithRoutingId(spotType, spotRid, ZLinkMessage.empty());
    }

    private CompletionStage<ZLinkSpotCreateResult> createWithRoutingId(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        ZLinkMessage request) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid) || pendingSpotCreates.containsKey(spotRid)) {
            throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
        }
        CompletionStage<ZLinkSpotCreateResult> create = createBackendSpotAsync(spotRid)
            .thenCompose(spot -> activateAsync(spotType, spot, request))
            .thenCompose(result -> createResultAsync(spotRid, spotType, result))
            .whenComplete((ignored, error) -> pendingSpotCreates.remove(spotRid));
        pendingSpotCreates.put(spotRid, create);
        return create;
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        return getOrCreate(spotType, spotRid, ZLinkMessage.empty());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        ZLinkMessage request) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        SpotActivation existing = spots.get(spotRid);
        if (existing != null) {
            if (existing.spot.getClass() != spotType) {
                throw new ZLinkConfigurationException(
                    "spot type mismatch: " + spotRid);
            }
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
        CompletionStage<ZLinkSpotCreateResult> create = createBackendSpotAsync(spotRid)
            .thenCompose(spot -> activateAsync(spotType, spot, request))
            .thenCompose(result -> createResultAsync(spotRid, spotType, result))
            .whenComplete((ignored, error) -> pendingSpotCreates.remove(spotRid));
        pendingSpotCreates.put(spotRid, create);
        return create;
    }

    private CompletionStage<ZLinkBackendSpot> createBackendSpotAsync() {
        return CompletableFuture.supplyAsync(primaryNode::createSpot, handlerExecutor);
    }

    private CompletionStage<ZLinkBackendSpot> createBackendSpotAsync(RoutingId spotRid) {
        return CompletableFuture.supplyAsync(() -> {
            ZLinkBackendSpot spot = primaryNode.createSpot();
            spot.setRoutingId(spotRid);
            return spot;
        }, handlerExecutor);
    }

    private CompletionStage<ZLinkLocationWriteStatus> claimUserSpotLocationAsync(
        RoutingId spotRid,
        Class<? extends ZLinkSpot<?>> spotType) {
        if (locationLifecycle == null) {
            return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
        }
        SpotNodeLocationMetadata metadata = locationMetadataByNodeRid.get(primaryNode.routingId());
        if (metadata == null) {
            CompletableFuture<ZLinkLocationWriteStatus> failed = new CompletableFuture<>();
            failed.completeExceptionally(new IllegalStateException("Location runtime is not available."));
            return failed;
        }
        return locationLifecycle.claimSpotAsync(
            metadata.meshName(),
            spotRid,
            spotType.getName(),
            metadata.nodeRid(),
            ZLinkSpotKind.USER,
            metadata.routeEndpoint(),
            () -> close(spotRid));
    }

    private CompletionStage<ZLinkLocationWriteStatus> claimEntrySpotLocationAsync(
        SpotNodeLocationMetadata metadata) {
        if (metadata.routeEndpoint() == null || metadata.routeEndpoint().isBlank()) {
            return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
        }
        if (locationLifecycle == null) {
            return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
        }
        return locationLifecycle.claimSpotAsync(
                metadata.meshName(),
                metadata.nodeRid(),
                null,
                metadata.nodeRid(),
                ZLinkSpotKind.ENTRY,
                metadata.routeEndpoint(),
                null)
            .thenApply(status -> {
                if (status != ZLinkLocationWriteStatus.STORED) {
                    boot("entrySpot location claim status=" + status
                        + " mesh=" + metadata.meshName()
                        + " nodeRid=" + metadata.nodeRid().toHex()
                        + " endpoint=" + metadata.routeEndpoint());
                }
                return status;
            });
    }

    private CompletionStage<Void> releaseSpotLocationAsync(RoutingId spotRid) {
        if (locationLifecycle == null) {
            return CompletableFuture.completedFuture(null);
        }
        SpotNodeLocationMetadata metadata = locationMetadataByNodeRid.get(primaryNode.routingId());
        if (metadata == null) {
            return CompletableFuture.completedFuture(null);
        }
        return locationLifecycle.releaseSpotAsync(metadata.meshName(), spotRid);
    }

    private CompletionStage<Void> releaseEntrySpotLocation(SpotNodeLocationMetadata metadata) {
        if (locationLifecycle == null || metadata.routeEndpoint() == null || metadata.routeEndpoint().isBlank()) {
            return CompletableFuture.completedFuture(null);
        }
        return locationLifecycle.releaseSpotAsync(metadata.meshName(), metadata.nodeRid());
    }

    private ZLinkFrameworkException spotCreateLocationFailure(
        RoutingId spotRid,
        ZLinkLocationWriteStatus status) {
        String message = status == ZLinkLocationWriteStatus.REJECTED_CONFLICT
            ? "SPOT '" + spotRid + "' location is owned by another runtime."
            : "SPOT '" + spotRid + "' location claim failed because the location store is unavailable.";
        return new ZLinkFrameworkException(ZLinkFrameworkErrorKind.SPOT_CREATE_FAILED, message);
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
        return releaseSpotLocationAsync(spotRid)
            .whenComplete((ignored, error) -> removed.close())
            .thenApply(ignored -> true);
    }

    @Override
    public void close() {
        beginClose();
        RuntimeException firstFailure = null;
        for (EntrySpotActivation entrySpot : entrySpots) {
            SpotNodeLocationMetadata metadata = locationMetadataByNodeRid.get(entrySpot.context.nodeRid());
            if (metadata != null) {
                firstFailure = closeRuntimeComponent(
                    () -> releaseEntrySpotLocation(metadata).toCompletableFuture().join(),
                    firstFailure);
            }
            firstFailure = closeRuntimeComponent(entrySpot::close, firstFailure);
        }
        entrySpots.clear();
        for (SpotActivation spot : spots.values()) {
            firstFailure = closeRuntimeComponent(
                () -> releaseSpotLocationAsync(spot.backendSpot.routingId()).toCompletableFuture().join(),
                firstFailure);
            firstFailure = closeRuntimeComponent(spot::close, firstFailure);
        }
        spots.clear();
        for (ZLinkBackendSpot publisherSpot : publisherSpotsByChannel.values()) {
            firstFailure = closeRuntimeComponent(publisherSpot::close, firstFailure);
        }
        publisherSpotsByChannel.clear();
        for (ZLinkBackendSpotNode node : nodes) {
            firstFailure = closeRuntimeComponent(node::close, firstFailure);
        }
        timerExecutor.shutdownNow();
        workerPool.close();
        if (ownsContext) {
            firstFailure = closeRuntimeComponent(context::close, firstFailure);
        }
        if (firstFailure != null) {
            throw firstFailure;
        }
    }

    public void beginClose() {
        closing = true;
    }

    private static RuntimeException closeRuntimeComponent(
        Runnable close,
        RuntimeException firstFailure) {
        try {
            close.run();
        } catch (ZlinkCloseException ignored) {
        } catch (RuntimeException error) {
            if (firstFailure == null) {
                return error;
            }
            firstFailure.addSuppressed(error);
        }
        return firstFailure;
    }

    public ZLinkBackendSpotNode primaryNode() {
        return primaryNode;
    }

    public ZLinkBackendSpotNode node(String nodeName) {
        ZLinkBackendSpotNode node = nodesByName.get(nodeName);
        if (node == null) {
            throw new ZLinkConfigurationException("unknown SpotNode: " + nodeName);
        }
        return node;
    }

    private ZLinkBackendSpotNode nodeByRid(RoutingId nodeRid) {
        for (ZLinkBackendSpotNode node : nodes) {
            if (node.routingId().equals(nodeRid)) {
                return node;
            }
        }
        throw new ZLinkConfigurationException("unknown SpotNode routing id: " + nodeRid);
    }

    public void attachActorRuntime(ZLinkActorRuntime actorRuntime) {
        this.actorRuntime = actorRuntime;
        this.actorRuntime.setCreatedNotifier(this::notifyEntrySpotActorCreated);
        this.actorRuntime.setActorCreateContextSupplier(() -> currentEntrySpotDispatch.get());
        this.actorRuntime.setDisconnectedNotifier(this::notifySpotActorDisconnected);
        this.actorRuntime.setSpotResolver(this::spotFor);
        this.actorRuntime.setSpotMeshResolver(this::meshNameForSpot);
    }

    public Map<String, ZLinkBackendSpotNode> nodesByName() {
        return Map.copyOf(nodesByName);
    }

    public boolean isSessionRelayRouteReady(RoutingId nodeRid) {
        if (!hasRoutingId(nodeRid)) {
            return true;
        }
        for (ZLinkBackendSpotNode node : nodes) {
            if (nodeRid.equals(node.routingId())) {
                return true;
            }
        }
        return manualRouterPeerNodeRids.contains(nodeRid)
            || autoConnectedRouterPeerNodeRids.contains(nodeRid);
    }

    public void markAutoConnectedRouterPeer(RoutingId nodeRid) {
        if (hasRoutingId(nodeRid)) {
            autoConnectedRouterPeerNodeRids.add(nodeRid);
        }
    }

    public void unmarkAutoConnectedRouterPeer(RoutingId nodeRid) {
        if (hasRoutingId(nodeRid)) {
            autoConnectedRouterPeerNodeRids.remove(nodeRid);
        }
    }

    private static boolean hasRoutingId(RoutingId routingId) {
        return routingId != null && routingId.size() > 0;
    }

    private static void boot(String step) {
        System.out.println("[boot] component=spot-runtime step=" + step);
    }

    public ZLinkSpotOutbound outbound() {
        return new AmbientSpotOutbound();
    }

    public ZLinkSpotPublisherClient publisherClient() {
        return new DefaultSpotPublisherClient();
    }

    public void setRemoteAddressResolver(SpotRemoteRefResolver remoteAddressResolver) {
        this.remoteAddressResolver = java.util.Objects.requireNonNull(
            remoteAddressResolver,
            "remoteAddressResolver");
    }

    public void setLocationLifecycle(ZLinkLocationLifecycle lifecycle) {
        this.locationLifecycle = lifecycle;
    }

    public CompletionStage<Void> claimEntrySpotLocationsAsync() {
        if (locationLifecycle == null) {
            return CompletableFuture.completedFuture(null);
        }
        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (SpotNodeLocationMetadata metadata : locationMetadataByNodeRid.values()) {
            chain = chain.thenCompose(ignored -> claimEntrySpotLocationAsync(metadata)
                .thenApply(status -> null));
        }
        return chain;
    }

    private CompletionStage<SpotRemoteRef> resolveSpotRemoteRefAsync(
        String configuredRouterChannelId,
        RoutingId spotRid) {
        if (remoteAddressResolver != null) {
            return remoteAddressResolver.resolveSpotRemoteRefAsync(spotRid);
        }
        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
            "SPOT remote address resolution requires a location resolver/store backed resolver"));
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
            ZLinkConfigurationException error = new ZLinkConfigurationException(
                "actor packet handler is not registered: " + header.packetName());
            reportDispatchError(
                ZLinkDispatchErrorSurface.SPOT_ACTOR,
                isRequest
                    ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                    : ZLinkDispatchMessageKind.ACTOR_SEND,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                isRequest
                    ? ZLinkDispatchErrorAction.REPLY_ERROR
                    : ZLinkDispatchErrorAction.DROP,
                header.packetName(),
                null,
                null,
                joinedSpotRid.orElse(null),
                actor.actorId(),
                null,
                header.requestSequence().map(Object::toString).orElse(null),
                error);
            return CompletableFuture.failedFuture(error);
        }
        if (isRequest != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
            ZLinkConfigurationException error = new ZLinkConfigurationException(
                "actor packet kind does not match handler kind: " + header.packetName());
            reportDispatchError(
                ZLinkDispatchErrorSurface.SPOT_ACTOR,
                isRequest
                    ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                    : ZLinkDispatchMessageKind.ACTOR_SEND,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                isRequest
                    ? ZLinkDispatchErrorAction.REPLY_ERROR
                    : ZLinkDispatchErrorAction.DROP,
                header.packetName(),
                null,
                null,
                joinedSpotRid.orElse(null),
                actor.actorId(),
                null,
                header.requestSequence().map(Object::toString).orElse(null),
                error);
            return CompletableFuture.failedFuture(error);
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
        ZLinkMessage request) {
        ZLinkMessage effectiveRequest = request == null ? ZLinkMessage.empty() : request;
        DefaultSpotContext spotContext =
            new DefaultSpotContext(
                primaryNode.routingId(),
                backendSpot,
                spotPublisherChannelName(primaryNode.routingId()));
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

    public void drainRoutedDispatchQueues() {
        if (closing) {
            return;
        }
        for (EntrySpotActivation activation : entrySpots) {
            activation.drainPolledDispatchQueues();
        }
        for (SpotActivation activation : spots.values()) {
            activation.drainPolledDispatchQueues();
        }
    }

    private <T> CompletionStage<T> withCurrentOutbound(
        DefaultSpotOutbound outbound,
        Supplier<CompletionStage<T>> action) {
        return withCurrentOutbound(outbound, true, action);
    }

    private <T> CompletionStage<T> withCurrentOutbound(
        DefaultSpotOutbound outbound,
        boolean preserveYieldTurn,
        Supplier<CompletionStage<T>> action) {
        CompletableFuture<T> result = new CompletableFuture<>();
        ZLinkYieldTurn turn = preserveYieldTurn ? ZLinkFrameworkTurns.captureCurrent() : null;
        DefaultEntrySpotContext entryDispatchContext = currentEntrySpotDispatch.get();
        try {
            handlerExecutor.execute(() -> {
                ZLinkFrameworkTurns.runWithTurn(turn, () -> {
                    DefaultSpotOutbound previous = currentOutbound.get();
                    DefaultEntrySpotContext previousEntryDispatch = currentEntrySpotDispatch.get();
                    currentOutbound.set(outbound);
                    if (entryDispatchContext == null) {
                        currentEntrySpotDispatch.remove();
                    } else {
                        currentEntrySpotDispatch.set(entryDispatchContext);
                    }
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
                        if (previousEntryDispatch == null) {
                            currentEntrySpotDispatch.remove();
                        } else {
                            currentEntrySpotDispatch.set(previousEntryDispatch);
                        }
                    }
                    return null;
                });
            });
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        }
        return result;
    }

    private CompletionStage<Void> dispatchActorPacketToHandler(
        DefaultSpotOutbound outbound,
        boolean preserveYieldTurn,
        SpotActorPacketHandlerRegistration handler,
        Object spotSurface,
        ZLinkActor actor,
        ActorPacketFrames.Header packetHeader,
        ZLinkBackendActorReceived headerPart,
        Message payload,
        String replyFailureMessage) {
        boolean noBindRequest = isNoBindActorRequest(packetHeader, headerPart);
        traceActorSession("dispatch-actor-packet"
            + " actor=" + actor.actorId()
            + " packet=" + packetHeader.packetName()
            + " requestSeq=" + packetHeader.requestSeq().map(Object::toString).orElse(null)
            + " sourceNode=" + headerPart.sourceNodeRid()
            + " sourceSession=" + headerPart.sourceSessionRid()
            + " noBind=" + noBindRequest
            + " hasBound=" + actorRuntime.hasBoundSession(actor));
        if (packetHeader.requestSeq().isPresent()
            && !noBindRequest
            && !actorRuntime.hasBoundSession(
                actor,
                headerPart.sourceNodeRid(),
                headerPart.sourceSessionRid())) {
            traceActorSession("bind-native-session"
                + " actor=" + actor.actorId()
                + " sourceNode=" + headerPart.sourceNodeRid()
                + " sourceSession=" + headerPart.sourceSessionRid());
            actorRuntime.bindNativeSession(
                actor,
                primaryNode,
                headerPart.actor(),
                headerPart.sourceNodeRid(),
                headerPart.sourceSessionRid());
        }
        boolean actorIsRequest = handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST;
        String actorPacketName = packetHeader.packetName();
        String actorId = actor.actorId();
        ZLinkDispatchMessageKind actorKind = actorIsRequest
            ? ZLinkDispatchMessageKind.ACTOR_REQUEST
            : ZLinkDispatchMessageKind.ACTOR_SEND;
        if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.RECEIVED,
                ZLinkDispatchErrorSurface.SPOT_ACTOR,
                actorKind,
                actorPacketName, null, null,
                packetHeader.requestSeq().map(String::valueOf).orElse(null),
                null, null, actorId, null));
        }
        CompletionStage<Optional<Message>> stage = withCurrentOutbound(
            outbound,
            preserveYieldTurn,
            () -> actorRuntime.runActorDispatchTurn(
                actor.actorId(),
                () -> actorIsRequest
                    ? invokeActorRequestHandler(handler, spotSurface, actor, payload)
                    : invokeActorSendHandler(handler, spotSurface, actor, payload)
                        .thenApply(ignored -> Optional.empty())));
        return stage.handle((reply, error) -> {
                if (error != null) {
                    reportDispatchError(
                        ZLinkDispatchErrorSurface.SPOT_ACTOR,
                        actorKind,
                        ZLinkDispatchErrorReason.HANDLER_EXCEPTION,
                        actorIsRequest
                            ? ZLinkDispatchErrorAction.REPLY_ERROR
                            : ZLinkDispatchErrorAction.DROP,
                        actorPacketName,
                        null,
                        null,
                        null,
                        actorId,
                        headerPart.sourceNodeRid(),
                        packetHeader.requestSeq().map(String::valueOf).orElse(null),
                        error);
                    return Optional.of(new ActorDispatchReply(
                        ActorPacketFrames.encodeError(packetHeader, error),
                        true));
                }
                return reply.map(message -> new ActorDispatchReply(message, false));
            })
            .thenCompose(reply -> {
                if (reply.isEmpty()) {
                    return systems.zlink.framework.ZLinkSubmitStage.completed();
                }
                byte[] frameBytes;
                try (Message frame = reply.get().streamFrame()
                    ? reply.get().message()
                    : ActorPacketFrames.encodeReply(packetHeader, reply.get().message())) {
                    frameBytes = frame.toByteArray();
                }
                if (noBindRequest) {
                    try (Message frame = Message.from(frameBytes)) {
                        primaryNode.replyActorNoBind(
                            new ZLinkBackendActorRef(
                                headerPart.actor().nodeRid(),
                                actor.actorId(),
                                headerPart.actor().generation()),
                            headerPart.sourceNodeRid(),
                            headerPart.sourceSessionRid(),
                            headerPart.requestId(),
                            headerPart.flags(),
                            List.of(frame));
                    }
                    return systems.zlink.framework.ZLinkSubmitStage.completed();
                }
                return sendActorBoundSessionWithRetry(
                    primaryNode,
                    new ZLinkBackendActorRef(
                        headerPart.actor().nodeRid(),
                        actor.actorId(),
                        headerPart.actor().generation()),
                    actor.actorId(),
                    frameBytes,
                    replyFailureMessage);
            })
            .whenComplete((ignored, error) -> {
                payload.close();
                headerPart.close();
                if (error == null) {
                    ZLinkMessageFlowOutcome phase = actorIsRequest
                        ? ZLinkMessageFlowOutcome.REPLIED
                        : ZLinkMessageFlowOutcome.DISPATCHED;
                    if (dispatchErrors.flow().enabled(phase)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            phase,
                            ZLinkDispatchErrorSurface.SPOT_ACTOR,
                            actorKind,
                            actorPacketName, null, null,
                            packetHeader.requestSeq().map(String::valueOf).orElse(null),
                            null, null, actorId, null));
                    }
                }
            });
    }

    private CompletionStage<Void> invokeActorSendHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        Object message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
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
        Object message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        if (registration.handlerMethod() == null) {
            return invokeActorRequestInterfaceHandler(
                    registration,
                    spotSurface,
                    actor,
                    new DefaultSpotActorRequestContext(registration.packetName()),
                    message,
                    "failed to invoke Spot actor request handler")
                .thenApply(reply -> Optional.of(ZLinkMessagePayloads.message(serializer.serialize(reply))));
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
            .thenApply(reply -> Optional.of(ZLinkMessagePayloads.message(serializer.serialize(reply))));
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
        return systems.zlink.framework.ZLinkSubmitStage.completed();
    }

    private boolean isAlreadyJoinedTo(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid) {
        if (actorRuntime == null || actor == null || actorRef == null || spotRid == null) {
            return false;
        }
        Optional<RoutingId> currentSpotRid = actorRuntime.spotRid(actor);
        if (currentSpotRid.isEmpty() || !spotRid.equals(currentSpotRid.get())) {
            return false;
        }
        ZLinkBackendActorRef currentRef = actorRuntime.currentRef(actor);
        return actorRef.equals(currentRef);
    }

    private boolean isJoinedToDifferentSpot(ZLinkActor actor, RoutingId spotRid) {
        if (actorRuntime == null || actor == null || spotRid == null) {
            return false;
        }
        Optional<RoutingId> currentSpotRid = actorRuntime.spotRid(actor);
        return currentSpotRid.isPresent() && !spotRid.equals(currentSpotRid.get());
    }

    private CompletionStage<Void> notifySpotActorLifecycleAndSuppressBackendEvent(
        Object spotSurface,
        ZLinkActor actor,
        RoutingId spotRid,
        boolean joined) {
        suppressActorLifecycleCallback(
            joined ? ZLinkBackendActorLifecycleEventKind.JOINED : ZLinkBackendActorLifecycleEventKind.LEFT,
            actor,
            spotRid);
        return notifySpotActorLifecycle(spotSurface, actor, joined);
    }

    private void suppressActorLifecycleCallback(
        ZLinkBackendActorLifecycleEventKind kind,
        ZLinkActor actor,
        RoutingId spotRid) {
        if (kind != null && actor != null && spotRid != null) {
            suppressedActorLifecycleCallbacks.add(actorLifecycleCallbackKey(kind, actor.actorId(), spotRid));
        }
    }

    private boolean consumeSuppressedActorLifecycleCallback(
        ZLinkBackendActorLifecycleEventKind kind,
        ZLinkActor actor,
        RoutingId spotRid) {
        return kind != null
            && actor != null
            && spotRid != null
            && suppressedActorLifecycleCallbacks.remove(actorLifecycleCallbackKey(kind, actor.actorId(), spotRid));
    }

    private static String actorLifecycleCallbackKey(
        ZLinkBackendActorLifecycleEventKind kind,
        String actorId,
        RoutingId spotRid) {
        return kind.name() + "|" + actorId + "|" + spotRid;
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
        return systems.zlink.framework.ZLinkSubmitStage.completed();
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> notifyEntrySpotActorCreated(
        RoutingId nodeRid,
        ZLinkActor actor,
        ZLinkMessage createRequest,
        Object createContext) {
        for (EntrySpotActivation activation : entrySpots) {
            if (activation.context.nodeRid().equals(nodeRid)) {
                ZLinkEntrySpot rawEntrySpot = activation.entrySpot;
                if (createContext == activation.context) {
                    return ZLinkHandlerStages.fromRunnable(() ->
                        rawEntrySpot.onCreateActor(actor, createRequest, NONE_CANCELLATION));
                }
                return activation.context.enqueueDispatch(() ->
                    ZLinkHandlerStages.fromRunnable(() ->
                        rawEntrySpot.onCreateActor(actor, createRequest, NONE_CANCELLATION)));
            }
        }
        return systems.zlink.framework.ZLinkSubmitStage.completed();
    }

    private ZLinkSpot<?> spotFor(RoutingId spotRid) {
        SpotActivation activation = spots.get(spotRid);
        return activation == null ? null : activation.spot;
    }

    private String meshNameForSpot(RoutingId spotRid) {
        if (spotRid == null) {
            return null;
        }
        SpotNodeLocationMetadata primaryMetadata = locationMetadataByNodeRid.get(primaryNode.routingId());
        if (primaryMetadata != null && spots.containsKey(spotRid)) {
            return primaryMetadata.meshName();
        }
        SpotNodeLocationMetadata nodeMetadata = locationMetadataByNodeRid.get(spotRid);
        return nodeMetadata == null ? null : nodeMetadata.meshName();
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
            if (handler.spotType() == null) {
                if (fallback == null) {
                    fallback = handler;
                }
                continue;
            }
            if (spotType != null) {
                if (handler.spotType().isAssignableFrom(spotType)) {
                    return handler;
                }
                continue;
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
        Object message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
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
        Object message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        if (registration.handlerMethod() == null) {
            return invokeActorRequestInterfaceHandler(
                    registration,
                    spotSurface,
                    actor,
                    new DefaultSpotActorRequestContext(registration.packetName()),
                    message,
                    "failed to invoke local session actor request handler")
                .thenApply(reply -> Optional.of(ZLinkMessagePayloads.message(serializer.serialize(reply))));
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
            .thenApply(reply -> Optional.of(ZLinkMessagePayloads.message(serializer.serialize(reply))));
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

    private void attachRouteMeshSpotBridges(Map<String, ZLinkBackendSpotNode> routeBridgeNodesByName) {
        if (channels == null || routeMeshChannels.isEmpty() || routeBridgeNodesByName.isEmpty()) {
            return;
        }
        for (ChannelRegistration routeMeshChannel : routeMeshChannels) {
            ZLinkBackendSpotNode routeBridgeNode =
                routeBridgeNodesByName.get(routeMeshChannel.name());
            if (routeBridgeNode == null && routeMeshChannel.routeRoutingId() != null) {
                for (ZLinkBackendSpotNode candidate : routeBridgeNodesByName.values()) {
                    if (routeMeshChannel.routeRoutingId().equals(candidate.routingId())) {
                        routeBridgeNode = candidate;
                        break;
                    }
                }
            }
            if (routeBridgeNode == null && routeBridgeNodesByName.size() == 1) {
                routeBridgeNode = routeBridgeNodesByName.values().iterator().next();
            }
            if (routeBridgeNode != null) {
                channels.attachSpotRouteBridgeToServer(routeMeshChannel.name(), routeBridgeNode);
            }
        }
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
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkEntrySpot<?> entrySpot;

        DefaultEntrySpotContext(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.outbound = new DefaultSpotOutbound(
                nodeRid,
                backendSpot,
                spotPublisherChannelName(nodeRid));
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
                new DefaultSpotContext(
                    nodeRid,
                    backendSpot,
                    spotPublisherChannelName(nodeRid),
                    dispatchQueue);
            timerContext.setSpot(new EntrySpotTimerSurface(this));
            timerContexts.add(timerContext);
            return timerContext.addTimer(name, period, handlerType, options);
        }

        void closeTimers() {
            timerContexts.forEach(DefaultSpotContext::closeTimers);
            timerContexts.clear();
        }

        CompletionStage<Void> enqueueDispatch(Supplier<CompletionStage<Void>> operation) {
            return dispatchQueue.enqueue(() -> {
                DefaultEntrySpotContext previous = currentEntrySpotDispatch.get();
                currentEntrySpotDispatch.set(this);
                try {
                    return operation.get();
                } finally {
                    if (previous == null) {
                        currentEntrySpotDispatch.remove();
                    } else {
                        currentEntrySpotDispatch.set(previous);
                    }
                }
            });
        }

        @Override
        public <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work) {
            java.util.Objects.requireNonNull(work, "work");
            // Completion callbacks enter the Entry Spot serial line through
            // the handler executor: never on the worker thread, with the
            // entry spot outbound context available.
            return new DefaultZLinkWorkerCall<>(
                workerPool,
                work,
                operation -> enqueueDispatch(() -> withCurrentOutbound(outbound, operation)),
                false);
        }

        void closeRegistration() {
            registrationOpen = false;
            registerScannedSpotHandlers(
                entrySpot.getClass(),
                packetHandlers,
                subscriptionHandlers,
                this::addTimer);
            for (Class<?> handlerType : handlerTypes) {
                registerConfiguredSpotHandler(
                    handlerType,
                    entrySpot.getClass(),
                    packetHandlers,
                    subscriptionHandlers);
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
            return systems.zlink.framework.ZLinkSubmitStage.completed();
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
        private final Set<ZLinkBackendReceived> activeRouteReceives =
            java.util.Collections.synchronizedSet(
                java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>()));
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
                traceSpotRouteInbound("entry-recv", backendSpot, received);
                if (channels != null
                    && channels.dispatchSpotRouteBridgePacket(received)) {
                    received.close();
                    continue;
                }
                dispatchRoute(received);
            }
        }

        private void drainPolledDispatchQueues() {
            drainRoutes();
            drainUnhandledActorJoins();
            drainActorLifecycleEvents();
        }

        private void dispatchRoute(ZLinkBackendReceived received) {
            activeRouteReceives.add(received);
            if (isProbeFrame(received.parts())) {
                closeRouteReceived(received);
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
            traceSpotRouteDispatch("entry-dispatch", backendSpot, received, packet);
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.RECEIVED,
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
                        .whenComplete((ignored, error) -> closeRouteReceived(received));
                } else {
                    handleRoutedBoundSessionSendParts(received.parts());
                    closeRouteReceived(received);
                }
                return;
            }
            if (ZLinkActorSpotRoutePackets.ACTOR_PACKET_NAME.equals(packet.packetName())) {
                handleRoutedActorPacketParts(received.parts())
                    .thenAccept(reply -> reply.ifPresent(message -> received.reply(List.of(message))))
                    .whenComplete((ignored, error) -> closeRouteReceived(received));
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
                closeRouteReceived(received);
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
                    closeRouteReceived(received);
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                context.enqueueDispatch(() ->
                    withCurrentOutbound(context.outbound, () ->
                        invokeSpotRequestHandler(handler, entrySpot, payloadCopy))
                        .thenAccept(reply -> received.reply(List.of(reply)))
                        .whenComplete((ignored, error) -> {
                            if (error != null) {
                                if (!closing) {
                                    replySpotRouteDispatchError(
                                        received,
                                        packet.packetName(),
                                        backendSpot.routingId(),
                                        ZLinkDispatchErrorReason.HANDLER_EXCEPTION,
                                        error);
                                }
                            }
                            payloadCopy.close();
                            closeRouteReceived(received);
                            if (error == null
                                && dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLIED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowOutcome.REPLIED,
                                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                                    ZLinkDispatchMessageKind.REQUEST,
                                    packet.packetName(), null, null,
                                    received.requestSeq().map(String::valueOf).orElse(null),
                                    null, backendSpot.routingId().toString(), null, null));
                            }
                        }));
                return;
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
                closeRouteReceived(received);
                return;
            }
            Message payloadCopy = Message.from(packet.payload());
            String sendPacketName = packet.packetName();
            closeRouteReceived(received);
            context.enqueueDispatch(() ->
                withCurrentOutbound(context.outbound, () ->
                    invokeSpotPacketHandler(handler, entrySpot, payloadCopy))
                    .whenComplete((ignored, error) -> {
                        payloadCopy.close();
                        if (error == null
                            && dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.DISPATCHED)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowOutcome.DISPATCHED,
                                ZLinkDispatchErrorSurface.SPOT_ROUTE,
                                ZLinkDispatchMessageKind.SEND,
                                sendPacketName, null, null, null,
                                null, backendSpot.routingId().toString(), null, null));
                        }
                    }));
        }

        private void handleRoutedBoundSessionSendParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.BoundSessionSend send =
                ZLinkActorSpotRoutePackets.decodeBoundSessionSend(parts);
            byte[] frameBytes = send.frame().toByteArray();
            CompletionStage<Void> sendStage = actorRuntime.localActor(send.actorRef().actorId())
                .map(actor -> actorRuntime.sendBoundSessionFrame(actor, frameBytes)
                    .thenCompose(sent -> sent
                        ? CompletableFuture.completedFuture(null)
                        : sendActorBoundSessionWithRetry(
                            primaryNode(),
                            send.actorRef(),
                            send.actorRef().actorId(),
                            frameBytes,
                            "routed actor bound session send failed")))
                .orElseGet(() -> sendActorBoundSessionWithRetry(
                    primaryNode(),
                    send.actorRef(),
                    send.actorRef().actorId(),
                    frameBytes,
                    "routed actor bound session send failed"));
            sendStage
                .whenComplete((ignored, error) -> send.close());
        }

        private CompletionStage<List<Message>> handleRoutedBoundSessionSendRequestParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.BoundSessionSend send =
                ZLinkActorSpotRoutePackets.decodeBoundSessionSend(parts);
            byte[] frameBytes = send.frame().toByteArray();
            CompletionStage<Void> sendStage = actorRuntime.localActor(send.actorRef().actorId())
                .map(actor -> actorRuntime.sendBoundSessionFrame(actor, frameBytes)
                    .thenCompose(sent -> sent
                        ? CompletableFuture.completedFuture(null)
                        : sendActorBoundSessionWithRetry(
                            primaryNode(),
                            send.actorRef(),
                            send.actorRef().actorId(),
                            frameBytes,
                            "routed actor bound session send failed")))
                .orElseGet(() -> sendActorBoundSessionWithRetry(
                    primaryNode(),
                    send.actorRef(),
                    send.actorRef().actorId(),
                    frameBytes,
                    "routed actor bound session send failed"));
            return sendStage
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
                if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
                    dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.RECEIVED,
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
                if (dispatched && dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.DISPATCHED)) {
                    dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.DISPATCHED,
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
                RoutingId spotRid = event.info()
                    .previousSpotRid()
                    .orElse(backendSpot.routingId());
                if (isJoinedToDifferentSpot(actor, spotRid)) {
                    return;
                }
                if (consumeSuppressedActorLifecycleCallback(event.kind(), actor, spotRid)) {
                    return;
                }
                context.enqueueDispatch(
                    () -> actorRuntime.markLeftAsync(actor)
                        .thenCompose(ignored -> notifySpotActorLifecycle(entrySpot, actor, false)));
                return;
            }
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.DISCONNECTED) {
                context.enqueueDispatch(() -> notifySpotActorDisconnected(actor));
                return;
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            if (isJoinedToDifferentSpot(actor, spotRid)) {
                return;
            }
            if (consumeSuppressedActorLifecycleCallback(event.kind(), actor, spotRid)) {
                return;
            }
            if (isAlreadyJoinedTo(actor, actorRef, spotRid)) {
                return;
            }
            context.enqueueDispatch(
                () -> actorRuntime.markJoinedAsync(
                        actor,
                        actorRef,
                        spotRid,
                        spotSurfaceFor(spotRid) instanceof ZLinkSpot<?> spot ? spot : null)
                    .thenCompose(ignored -> notifySpotActorLifecycle(entrySpot, actor, true)));
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
                ActorPacketFrames.Header packetHeader = ActorPacketFrames.decode(headerPart);
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
                if (REMOTE_BOUND_SESSION_BIND_PACKET_NAME.equals(packetHeader.packetName())) {
                    actorRuntime.bindNativeSession(
                        actor,
                        primaryNode,
                        headerPart.actor(),
                        headerPart.sourceNodeRid(),
                        headerPart.sourceSessionRid());
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                if (ZLinkActorSpotRoutePackets.SESSION_DISCONNECTED_PACKET_NAME.equals(packetHeader.packetName())) {
                    notifySpotActorDisconnected(actor).toCompletableFuture().join();
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
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
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
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
            ActorPacketFrames.Header packetHeader,
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
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            } finally {
                payload.close();
                headerPart.close();
            }
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            Object spotSurface,
            ZLinkActor actor,
            ActorPacketFrames.Header packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            return dispatchActorPacketToHandler(
                context.outbound,
                true,
                handler,
                spotSurface,
                actor,
                packetHeader,
                headerPart,
                payload,
                "actor bound session reply failed");
        }

        private void replyActorDispatchError(
            ActorPacketFrames.Header packetHeader,
            ZLinkBackendActorReceived headerPart,
            String actorId,
            Throwable error,
            String failureMessage) {
            byte[] frameBytes;
            try (Message frame = ActorPacketFrames.encodeError(packetHeader, error)) {
                frameBytes = frame.toByteArray();
            } catch (RuntimeException ex) {
                headerPart.close();
                return;
            }
            if (isNoBindActorRequest(packetHeader, headerPart)) {
                context.enqueueDispatch(() -> {
                    try (Message frame = Message.from(frameBytes)) {
                        primaryNode.replyActorNoBind(
                            new ZLinkBackendActorRef(
                                headerPart.actor().nodeRid(),
                                actorId,
                                headerPart.actor().generation()),
                            headerPart.sourceNodeRid(),
                            headerPart.sourceSessionRid(),
                            headerPart.requestId(),
                            headerPart.flags(),
                            List.of(frame));
                        return systems.zlink.framework.ZLinkSubmitStage.completed();
                    } finally {
                        headerPart.close();
                    }
                });
                return;
            }
            context.enqueueDispatch(() -> sendActorBoundSessionWithRetry(
                    primaryNode,
                    new ZLinkBackendActorRef(
                        headerPart.actor().nodeRid(),
                        actorId,
                        headerPart.actor().generation()),
                    actorId,
                    frameBytes,
                    failureMessage)
                .whenComplete((ignored, sendError) -> headerPart.close()));
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
                                    : ZLinkMessagePayloads.message(effective.reply(), serializer);
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
                                                ZLinkMessage.fromEncoded(ZLinkMessagePayloads.encoded(payload), serializer),
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
                            return actorRuntime.markJoinedAsync(
                                    actor.get(),
                                    request.targetActor(),
                                    backendSpot.routingId(),
                                    null)
                                .thenCompose(ignored -> context.enqueueDispatch(() ->
                                    notifySpotActorLifecycleAndSuppressBackendEvent(
                                        entrySpot,
                                        actor.get(),
                                        backendSpot.routingId(),
                                        true)))
                                .thenApply(ignored -> effective)
                                .whenComplete((reply, error) -> {
                                    if (error != null) {
                                        actorRuntime.markLeftAsync(actor.get());
                                    }
                                });
                        });
                });
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
                closeActiveRouteReceives();
                context.closeTimers();
                backendSpot.close();
            }
        }

        private void closeActiveRouteReceives() {
            for (ZLinkBackendReceived received : List.copyOf(activeRouteReceives)) {
                closeRouteReceived(received);
            }
        }

        private void closeRouteReceived(ZLinkBackendReceived received) {
            if (activeRouteReceives.remove(received)) {
                received.close();
            }
        }
    }

    private record SpotActivationCreateResult(
        SpotActivation activation,
        ZLinkSpotCreateResponse response) {
    }

    private record SpotNodeLocationMetadata(
        String meshName,
        RoutingId nodeRid,
        String routeEndpoint) {
    }

    private final class DefaultSpotContext implements ZLinkSpotContext {
        private static final CancellationToken NONE = () -> false;
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotOutbound outbound;
        private final List<ManagedTimer> timers = new ArrayList<>();
        private final ZLinkAsyncSerialQueue dispatchQueue;
        private final List<Class<?>> handlerTypes = new ArrayList<>();
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkSpot<?> spot;

        DefaultSpotContext(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this(nodeRid, backendSpot, spotPublisherChannelName(nodeRid), new ZLinkAsyncSerialQueue());
        }

        DefaultSpotContext(
            RoutingId nodeRid,
            ZLinkBackendSpot backendSpot,
            String publisherChannelName) {
            this(nodeRid, backendSpot, publisherChannelName, new ZLinkAsyncSerialQueue());
        }

        DefaultSpotContext(
            RoutingId nodeRid,
            ZLinkBackendSpot backendSpot,
            String publisherChannelName,
            ZLinkAsyncSerialQueue dispatchQueue) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.dispatchQueue = dispatchQueue;
            this.outbound = new DefaultSpotOutbound(nodeRid, backendSpot, publisherChannelName);
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
            try {
                RoutingId currentSpotRid = actorRuntime.spotRid(actor)
                    .orElseGet(this::spotRid);
                ZLinkBackendActorRef actorRef = actorRuntime.actorRef(actor);
                nodeByRid(nodeRid)
                    .leaveActor(actorRef, currentSpotRid, defaultRequestTimeout)
                    .whenComplete((replyParts, error) -> {
                        if (replyParts != null) {
                            replyParts.forEach(Message::close);
                        }
                    });
                return actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> actorRuntime.markLeftAsync(actor)
                        .thenCompose(ignored -> notifySpotActorLifecycleAndSuppressBackendEvent(
                            this.spot,
                            actor,
                            currentSpotRid,
                            false)));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
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
            ZLinkTimerOptions timerOptions = options == null ? new ZLinkTimerOptions() : options;
            if (timerOptions.overrunPolicy() == null) {
                throw new ZLinkConfigurationException("timer overrun policy is required");
            }
            if (timerOptions.overrunPolicy() == ZLinkTimerOverrunPolicy.CATCH_UP_BOUNDED
                && timerOptions.maxCatchUpTicks() <= 0) {
                throw new ZLinkConfigurationException(
                    "timer maxCatchUpTicks must be greater than zero");
            }
            ManagedTimer timer = new ManagedTimer(name, period, handlerType, timerOptions);
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
            return new DefaultZLinkWorkerCall<>(
                workerPool,
                work,
                operation -> enqueueDispatch(() -> withCurrentOutbound(outbound, operation)),
                true);
        }

        void closeRegistration() {
            registrationOpen = false;
            registerScannedSpotHandlers(
                spot.getClass(),
                packetHandlers,
                subscriptionHandlers,
                this::addTimer);
            for (Class<?> handlerType : handlerTypes) {
                registerConfiguredSpotHandler(
                    handlerType,
                    spot.getClass(),
                    packetHandlers,
                    subscriptionHandlers);
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
            private final ZLinkTimerOptions options;
            private final Instant startedAt = Instant.now();
            private final long startedNanos = System.nanoTime();
            private final long periodNanos;
            private long deliveryIndex;
            private long lastScheduledIndex;
            private volatile boolean disposed;
            private ScheduledFuture<?> future;

            ManagedTimer(
                String name,
                Duration period,
                Class<?> handlerType,
                ZLinkTimerOptions options) {
                this.name = name;
                this.period = period;
                this.handlerType = handlerType;
                this.options = options == null ? new ZLinkTimerOptions() : options;
                this.periodNanos = Math.max(1L, period.toNanos());
            }

            void start() {
                scheduleNext(periodNanos);
            }

            private void scheduleNext(long delayNanos) {
                if (disposed) {
                    return;
                }
                future = timerExecutor.schedule(
                    this::dispatch,
                    Math.max(0L, delayNanos),
                    TimeUnit.NANOSECONDS);
            }

            private void dispatch() {
                if (disposed) {
                    return;
                }
                long startedElapsedNanos = Math.max(0L, System.nanoTime() - startedNanos);
                long scheduledIndex = nextScheduledIndex(startedElapsedNanos);
                long skipped = options.overrunPolicy() == ZLinkTimerOverrunPolicy.DELAY_NEXT_TICK
                    ? 0L
                    : scheduledIndex - lastScheduledIndex - 1L;
                long index = ++deliveryIndex;
                Instant now = Instant.now();
                ZLinkTimerTick tick = new ZLinkTimerTick(
                    name,
                    index,
                    scheduledIndex,
                    period,
                    startedAt.plusNanos(saturatedMultiply(periodNanos, scheduledIndex)),
                    now,
                    Duration.ofNanos(saturatedMultiply(periodNanos, scheduledIndex)),
                    Duration.ofNanos(startedElapsedNanos),
                    Duration.between(startedAt.plusNanos(
                        saturatedMultiply(periodNanos, scheduledIndex)), now),
                    skipped);
                enqueueDispatch(() ->
                    disposed
                        ? CompletableFuture.completedFuture(null)
                        : withCurrentOutbound(DefaultSpotContext.this.outbound, () ->
                            invokeTimerHandler(handlerType, spot, tick)))
                    .whenComplete((ignored, error) -> {
                        boolean stopped = false;
                        if (error == null) {
                            lastScheduledIndex = scheduledIndex;
                        } else {
                            stopped = this.options.stopOnUnhandledException();
                            if (stopped) {
                                close();
                            }
                            publishTimerFailureEvent(
                                DefaultSpotContext.this,
                                this,
                                tick,
                                error,
                                stopped);
                        }
                        if (!stopped) {
                            scheduleAfterDispatch();
                        }
                    });
            }

            private long nextScheduledIndex(long startedElapsedNanos) {
                if (options.overrunPolicy() == ZLinkTimerOverrunPolicy.DELAY_NEXT_TICK) {
                    return deliveryIndex + 1L;
                }
                long next = lastScheduledIndex + 1L;
                long due = Math.max(next, Math.max(1L, startedElapsedNanos / periodNanos));
                if (options.overrunPolicy() == ZLinkTimerOverrunPolicy.SKIP_LATE_TICKS) {
                    return due;
                }
                long available = due - lastScheduledIndex;
                long maxCatchUp = options.maxCatchUpTicks();
                if (available > maxCatchUp) {
                    return due - maxCatchUp + 1L;
                }
                return next;
            }

            private void scheduleAfterDispatch() {
                if (disposed) {
                    return;
                }
                if (options.overrunPolicy() == ZLinkTimerOverrunPolicy.DELAY_NEXT_TICK) {
                    scheduleNext(periodNanos);
                    return;
                }
                long nextScheduledElapsed = saturatedMultiply(
                    periodNanos,
                    lastScheduledIndex + 1L);
                long currentElapsed = Math.max(0L, System.nanoTime() - startedNanos);
                scheduleNext(nextScheduledElapsed - currentElapsed);
            }

            private long saturatedMultiply(long left, long right) {
                if (left <= 0 || right <= 0) {
                    return 0L;
                }
                if (left > Long.MAX_VALUE / right) {
                    return Long.MAX_VALUE;
                }
                return left * right;
            }

            @Override
            public boolean isDisposed() {
                return disposed;
            }

            @Override
            public CompletionStage<Void> cancelAsync() {
                close();
                return systems.zlink.framework.ZLinkSubmitStage.completed();
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

    private void publishTimerFailureEvent(
        DefaultSpotContext context,
        DefaultSpotContext.ManagedTimer timer,
        ZLinkTimerTick tick,
        Throwable error,
        boolean stopped) {
        if (eventDispatcher == null) {
            return;
        }
        Throwable failure = unwrap(error);
        eventDispatcher.publish(new ZLinkSpotEvent(
            primaryNodeSourceName,
            Instant.now(),
            stopped
                ? ZLinkSpotEventKind.TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION
                : ZLinkSpotEventKind.TIMER_HANDLER_FAILED,
            Optional.empty(),
            List.of(),
            List.of(),
            Optional.of(timerDiagnostic(context, timer, tick, failure))));
    }

    private String timerDiagnostic(
        DefaultSpotContext context,
        DefaultSpotContext.ManagedTimer timer,
        ZLinkTimerTick tick,
        Throwable error) {
        return "spotRid=" + context.backendSpot.routingId()
            + "|timer=" + timer.name
            + "|handler=" + timer.handlerType.getName()
            + "|deliveryIndex=" + tick.deliveryIndex()
            + "|scheduledIndex=" + tick.scheduledIndex()
            + "|exception=" + error.getClass().getName()
            + "|message=" + String.valueOf(error.getMessage());
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while ((current instanceof CompletionException
            || current instanceof InvocationTargetException)
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
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
        Object message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
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
        Object message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            if (registration.handlerMethod() != null) {
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(), new Object[] {spot, message}, suspendHandlerInvokers)
                    .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
            }
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {spot, message}, suspendHandlerInvokers)
                .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
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
        Object message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
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
        private final String publisherChannelName;

        DefaultSpotOutbound(
            RoutingId nodeRid,
            ZLinkBackendSpot backendSpot,
            String publisherChannelName) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.publisherChannelName = publisherChannelName;
        }

        @Override
        public ZLinkSendCall sendToSpot(SpotRef spotRef, Object message) {
            requireRoutingId(spotRef.nodeRid());
            requireRoutingId(spotRef.spotRid());
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            if (channels != null && !routeMeshChannels.isEmpty()) {
                return new SpotRefSendCall(
                    channels,
                    spotRef.meshName(),
                    spotRef.nodeRid(),
                    spotRef.spotRid(),
                    encoded.payload(),
                    Optional.of(encoded.packetName()));
            }
            return new SpotToSpotSendCall(
                backendSpot,
                spotRef.nodeRid(),
                spotRef.spotRid(),
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }

        @Override
        public ZLinkYieldRequestCall requestToSpot(SpotRef spotRef, Object request) {
            requireRoutingId(spotRef.nodeRid());
            requireRoutingId(spotRef.spotRid());
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, request);
            if (channels != null && !routeMeshChannels.isEmpty()) {
                return new SpotRefRequestCall(
                    channels,
                    spotRef.meshName(),
                    spotRef.nodeRid(),
                    spotRef.spotRid(),
                    encoded.payload(),
                    Optional.of(encoded.packetName()),
                    defaultRequestTimeout);
            }
            return new SpotToSpotRequestCall(
                backendSpot,
                spotRef.nodeRid(),
                spotRef.spotRid(),
                encoded.payload(),
                Optional.of(encoded.packetName()),
                defaultRequestTimeout);
        }

        @Override
        public ZLinkPublishCall publish(String topic, Object message) {
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            if (publisherChannelName != null
                && publisherNodesByChannel.containsKey(publisherChannelName)) {
                return new ExternalSpotPublishCall(
                    publisherChannelName,
                    topic,
                    encoded.payload(),
                    Optional.of(encoded.packetName()));
            }
            return new SpotPublishCall(
                backendSpot,
                topic,
                encoded.payload(),
                Optional.of(encoded.packetName()));
        }

        @Override
        public ZLinkSendCall sendToChannel(String channelName, Object message) {
            if (channels == null) {
                throw new ZLinkConfigurationException(
                    "channel client is not configured: " + channelName);
            }
            return channels.sendToChannel(channelName, message);
        }

        @Override
        public ZLinkYieldRequestCall requestToChannel(String channelName, Object request) {
            if (channels == null) {
                throw new ZLinkConfigurationException(
                    "channel client is not configured: " + channelName);
            }
            return channels.requestToChannel(channelName, request);
        }
    }

    private final class SpotRefSendCall implements ZLinkSendCall {
        private final ZLinkChannelRuntime channels;
        private final String routerChannelId;
        private final RoutingId targetNodeRid;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;

        SpotRefSendCall(
            ZLinkChannelRuntime channels,
            String routerChannelId,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName) {
            this.channels = channels;
            this.routerChannelId = routerChannelId;
            this.targetNodeRid = targetNodeRid;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
        }

        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new SpotRefSendCall(
                channels,
                routerChannelId,
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
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    ZLinkDispatchMessageKind.SEND,
                    packetName.orElse(null), routerChannelId, null, null, null,
                    spotRid.toString(), null, null));
            }
            ZLinkBackendSpotNode routerNode = nodesByName.get(routerChannelId);
            if (routerNode != null) {
                return new SpotToSpotSendCall(
                    routerNode.entrySpot(),
                    targetNodeRid,
                    spotRid,
                    payload,
                    packetName)
                    .submit();
            }
            List<Message> spotParts = parts(packetName, payload);
            try {
                return systems.zlink.framework.ZLinkSubmitStage.from(
                    channels.sendToSpotViaRouterChannel(
                        routerChannelId,
                        targetNodeRid,
                        spotRid,
                        spotParts));
            } finally {
                spotParts.forEach(Message::close);
            }
        }
    }

    private final class EgressSpotSendCall implements ZLinkSendCall {
        private final ZLinkChannelRuntime channels;
        private final String egressChannelName;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;

        EgressSpotSendCall(
            ZLinkChannelRuntime channels,
            String egressChannelName,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName) {
            this.channels = channels;
            this.egressChannelName = egressChannelName;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
        }

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
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    ZLinkDispatchMessageKind.SEND,
                    packetName.orElse(null), egressChannelName, null, null, null,
                    spotRid.toString(), null, null));
            }
            return systems.zlink.framework.ZLinkSubmitStage.from(
                resolveSpotRemoteRefAsync(egressChannelName, spotRid)
                .thenCompose(address -> {
                ZLinkBackendSpotNode routerNode = nodesByName.get(address.routerChannelId());
                if (routerNode != null) {
                    return new SpotToSpotSendCall(
                        routerNode.entrySpot(),
                        address.targetNodeRid(),
                        address.spotRid(),
                        payload,
                        packetName)
                        .submit();
                }
                List<Message> spotParts = parts(packetName, payload);
                try {
                    return channels.sendToSpotViaRouterChannel(
                        address.routerChannelId(),
                        address.targetNodeRid(),
                        address.spotRid(),
                        spotParts);
                } finally {
                    spotParts.forEach(Message::close);
                }
            }));
        }

    }

    private final class SpotRefRequestCall implements ZLinkYieldRequestCall {
        private final ZLinkChannelRuntime channels;
        private final String routerChannelId;
        private final RoutingId targetNodeRid;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;
        private final ZLinkYieldTurn turn;

        SpotRefRequestCall(
            ZLinkChannelRuntime channels,
            String routerChannelId,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this(channels, routerChannelId, targetNodeRid, spotRid, payload, packetName, timeout, ZLinkFrameworkTurns.captureCurrent());
        }

        SpotRefRequestCall(
            ZLinkChannelRuntime channels,
            String routerChannelId,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout,
            ZLinkYieldTurn turn) {
            this.channels = channels;
            this.routerChannelId = routerChannelId;
            this.targetNodeRid = targetNodeRid;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
            this.turn = turn;
        }

        @Override
        public ZLinkYieldRequestCall packetName(String packetName) {
            return new SpotRefRequestCall(
                channels,
                routerChannelId,
                targetNodeRid,
                spotRid,
                payload,
                Optional.of(packetName),
                timeout,
                turn);
        }

        @Override
        public ZLinkYieldRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkYieldRequestCall timeout(Duration timeout) {
            return new SpotRefRequestCall(
                channels,
                routerChannelId,
                targetNodeRid,
                spotRid,
                payload,
                packetName,
                timeout,
                turn);
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    ZLinkDispatchMessageKind.REQUEST,
                    packetName.orElse(null), routerChannelId, null, null, null,
                    spotRid.toString(), null, null));
            }
            ZLinkBackendSpotNode routerNode = nodesByName.get(routerChannelId);
            if (routerNode != null) {
                return new SpotToSpotRequestCall(
                    routerNode.entrySpot(),
                    targetNodeRid,
                    spotRid,
                    payload,
                    packetName,
                    timeout,
                    turn)
                    .submit(replyType);
            }
            List<Message> spotParts = parts(packetName, payload);
            try {
                return channels.requestToSpotViaRouterChannel(
                    routerChannelId,
                    targetNodeRid,
                    spotRid,
                    spotParts,
                    timeout)
                    .thenApply(replyParts -> {
                        if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                                ZLinkDispatchErrorSurface.SPOT_ROUTE,
                                ZLinkDispatchMessageKind.RESPONSE,
                                packetName.orElse(null), routerChannelId, null, null, null,
                                spotRid.toString(), null, null));
                        }
                        Message emptyReply = null;
                        try {
                            if (isFrameworkErrorReply(replyParts)) {
                                throw new ZLinkFrameworkException(
                                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                    frameworkErrorReplyMessage(replyParts));
                            }
                            Message firstReply = replyParts.isEmpty()
                                ? (emptyReply = Message.from(new byte[0]))
                                : spotRouteReplyPayload(replyParts);
                            try {
                                return ZLinkMessagePayloads.deserialize(serializer, firstReply, replyType);
                            } catch (IllegalArgumentException ex) {
                                throw new IllegalArgumentException(
                                    ex.getMessage()
                                        + " (spot route reply parts="
                                        + describeMessageParts(replyParts)
                                        + ")",
                                    ex);
                            }
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

    private final class EgressSpotRequestCall implements ZLinkYieldRequestCall {
        private final ZLinkChannelRuntime channels;
        private final String egressChannelName;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;
        private final ZLinkYieldTurn turn;

        EgressSpotRequestCall(
            ZLinkChannelRuntime channels,
            String egressChannelName,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this(channels, egressChannelName, spotRid, payload, packetName, timeout, ZLinkFrameworkTurns.captureCurrent());
        }

        EgressSpotRequestCall(
            ZLinkChannelRuntime channels,
            String egressChannelName,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout,
            ZLinkYieldTurn turn) {
            this.channels = channels;
            this.egressChannelName = egressChannelName;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
            this.turn = turn;
        }

        @Override
        public ZLinkYieldRequestCall packetName(String packetName) {
            return new EgressSpotRequestCall(
                channels,
                egressChannelName,
                spotRid,
                payload,
                Optional.of(packetName),
                timeout,
                turn);
        }

        @Override
        public ZLinkYieldRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkYieldRequestCall timeout(Duration timeout) {
            return new EgressSpotRequestCall(
                channels,
                egressChannelName,
                spotRid,
                payload,
                packetName,
                timeout,
                turn);
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    ZLinkDispatchMessageKind.REQUEST,
                    packetName.orElse(null), egressChannelName, null, null, null,
                    spotRid.toString(), null, null));
            }
            return resolveSpotRemoteRefAsync(egressChannelName, spotRid)
                .thenCompose(address -> {
                ZLinkBackendSpotNode routerNode = nodesByName.get(address.routerChannelId());
                if (routerNode != null) {
                    return new SpotToSpotRequestCall(
                        routerNode.entrySpot(),
                        address.targetNodeRid(),
                        address.spotRid(),
                        payload,
                        packetName,
                        timeout,
                        turn)
                        .submit(replyType);
                }
                List<Message> spotParts = parts(packetName, payload);
                try {
                    return channels.requestToSpotViaRouterChannel(
                        address.routerChannelId(),
                        address.targetNodeRid(),
                        address.spotRid(),
                        spotParts,
                        timeout)
                        .thenApply(replyParts -> {
                            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
                                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                    ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                                    ZLinkDispatchMessageKind.RESPONSE,
                                    packetName.orElse(null), egressChannelName, null, null, null,
                                    spotRid.toString(), null, null));
                            }
                            Message emptyReply = null;
                            try {
                                if (isFrameworkErrorReply(replyParts)) {
                                    throw new ZLinkFrameworkException(
                                        ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                        frameworkErrorReplyMessage(replyParts));
                                }
                                Message firstReply = replyParts.isEmpty()
                                    ? (emptyReply = Message.from(new byte[0]))
                                    : spotRouteReplyPayload(replyParts);
                                try {
                                    return ZLinkMessagePayloads.deserialize(serializer, firstReply, replyType);
                                } catch (IllegalArgumentException ex) {
                                    throw new IllegalArgumentException(
                                        ex.getMessage()
                                            + " (spot route reply parts="
                                            + describeMessageParts(replyParts)
                                            + ")",
                                        ex);
                                }
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
            });
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

    private final class AmbientSpotOutbound implements ZLinkSpotOutbound {
        @Override
        public ZLinkSendCall sendToSpot(SpotRef spotRef, Object message) {
            return requireCurrentOutbound().sendToSpot(spotRef, message);
        }

        @Override
        public ZLinkYieldRequestCall requestToSpot(
            SpotRef spotRef,
            Object request) {
            return requireCurrentOutbound().requestToSpot(spotRef, request);
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
        public ZLinkYieldRequestCall requestToChannel(
            String channelName,
            Object request) {
            return requireCurrentOutbound().requestToChannel(channelName, request);
        }
    }

    private final class DefaultSpotPublisherClient implements ZLinkSpotPublisherClient {
        @Override
        public ZLinkPublishCall publish(
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
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                    ZLinkDispatchMessageKind.PUBLISH,
                    packetName.orElse(null), channelName, topic, null, null, null, null, null));
            }
            return systems.zlink.framework.ZLinkSubmitStage.from(CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    publisherSpot(channelName).publish(topic, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            }));
        }
    }

    private final class SpotToSpotSendCall implements ZLinkSendCall {
        private final ZLinkBackendSpot spot;
        private final RoutingId targetNodeRid;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;

        SpotToSpotSendCall(
            ZLinkBackendSpot spot,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName) {
            this.spot = spot;
            this.targetNodeRid = targetNodeRid;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
        }

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
        public systems.zlink.framework.ZLinkSubmitStage submit() {
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    ZLinkDispatchMessageKind.SEND,
                    packetName.orElse(null), null, null, null,
                    targetNodeRid.toString(), spotRid.toString(), null, null));
            }
            return systems.zlink.framework.ZLinkSubmitStage.from(CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.sendToSpot(targetNodeRid, spotRid, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            }));
        }
    }

    private final class SpotToSpotRequestCall implements ZLinkYieldRequestCall {
        private final ZLinkBackendSpot spot;
        private final RoutingId targetNodeRid;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;
        private final ZLinkYieldTurn turn;

        private SpotToSpotRequestCall(
            ZLinkBackendSpot spot,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this(spot, targetNodeRid, spotRid, payload, packetName, timeout, ZLinkFrameworkTurns.captureCurrent());
        }

        private SpotToSpotRequestCall(
            ZLinkBackendSpot spot,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout,
            ZLinkYieldTurn turn) {
            this.spot = spot;
            this.targetNodeRid = targetNodeRid;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
            this.turn = turn;
        }

        @Override
        public ZLinkYieldRequestCall packetName(String packetName) {
            return new SpotToSpotRequestCall(
                spot,
                targetNodeRid,
                spotRid,
                payload,
                Optional.of(packetName),
                timeout,
                turn);
        }

        @Override
        public ZLinkYieldRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkYieldRequestCall timeout(Duration timeout) {
            return new SpotToSpotRequestCall(
                spot,
                targetNodeRid,
                spotRid,
                payload,
                packetName,
                timeout,
                turn);
        }

        @Override
        public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            List<Message> requestParts = parts(packetName, payload);
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.SENT,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    ZLinkDispatchMessageKind.REQUEST,
                    packetName.orElse(null), null, null, null,
                    targetNodeRid.toString(), spotRid.toString(), null, null));
            }
            try {
                spot.requestToSpot(targetNodeRid, spotRid, requestParts, reply -> {
                    if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                            ZLinkDispatchErrorSurface.SPOT_ROUTE,
                            ZLinkDispatchMessageKind.RESPONSE,
                            packetName.orElse(null), null, null, null,
                            targetNodeRid.toString(), spotRid.toString(), null, null));
                    }
                    Message emptyReply = null;
                    try {
                        if (isFrameworkErrorReply(reply.parts())) {
                            result.completeExceptionally(new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                frameworkErrorReplyMessage(reply.parts())));
                            return;
                        }
                        Message firstReply = reply.parts().isEmpty()
                            ? (emptyReply = Message.from(new byte[0]))
                            : spotRouteReplyPayload(reply.parts());
                        try {
                            result.complete(ZLinkMessagePayloads.deserialize(serializer, firstReply, replyType));
                        } catch (IllegalArgumentException ex) {
                            result.completeExceptionally(new IllegalArgumentException(
                                ex.getMessage()
                                    + " (spot route reply parts="
                                    + describeMessageParts(reply.parts())
                                    + ")",
                                ex));
                        }
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
                        traceActorSession("bound-session-send-ok"
                            + " actor=" + actorId
                            + " actorNode=" + actor.nodeRid());
                        result.complete(null);
                        return;
                    }
                    traceActorSession("bound-session-send-retry"
                        + " actor=" + actorId
                        + " actorNode=" + actor.nodeRid());
                } catch (ZlinkSubmitException ex) {
                    if (ex.getResult() != SubmitResult.NOT_CONNECTED
                        && ex.getResult() != SubmitResult.NOT_FOUND
                        && ex.getResult() != SubmitResult.BACKPRESSURED) {
                        result.completeExceptionally(ex);
                        return;
                    }
                    traceActorSession("bound-session-send-submit-retry"
                        + " actor=" + actorId
                        + " actorNode=" + actor.nodeRid()
                        + " result=" + ex.getResult());
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

    private static void traceActorSession(String message) {
        if (!STREAM_TRACE) {
            return;
        }
        System.out.println("[zlink-java-stream-trace] actor-session " + message);
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
            received.requestId(),
            received.flags(),
            Message.from(received.message()),
            received.hasMore());
    }

    private static boolean isNoBindActorRequest(
        ActorPacketFrames.Header packetHeader,
        ZLinkBackendActorReceived headerPart) {
        return packetHeader.requestSeq().isPresent()
            && headerPart.requestId() != 0
            && (headerPart.flags() & ACTOR_RECV_INFO_NO_BIND) != 0;
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
        Throwable error) {
        dispatchErrors.report(new ZLinkDispatchFailure(
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
            errorType(error),
            errorMessage(error)));
    }

    private static String errorType(Throwable error) {
        if (error == null) {
            return null;
        }
        Throwable current = unwrapDispatchError(error);
        return current.getClass().getSimpleName();
    }

    private static String errorMessage(Throwable error) {
        if (error == null) {
            return null;
        }
        Throwable current = unwrapDispatchError(error);
        return current.getMessage() == null
            ? current.getClass().getName()
            : current.getMessage();
    }

    private static Throwable unwrapDispatchError(Throwable error) {
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
        return current;
    }

    private final class SpotPublishCall implements ZLinkPublishCall {
        private final ZLinkBackendSpot spot;
        private final String topic;
        private final Message payload;
        private final Optional<String> packetName;

        SpotPublishCall(
            ZLinkBackendSpot spot,
            String topic,
            Message payload,
            Optional<String> packetName) {
            this.spot = spot;
            this.topic = topic;
            this.payload = payload;
            this.packetName = packetName;
        }

        @Override
        public ZLinkPublishCall packetName(String packetName) {
            return new SpotPublishCall(spot, topic, payload, Optional.of(packetName));
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
                    ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                    ZLinkDispatchMessageKind.PUBLISH,
                    packetName.orElse(null), null, topic, null, null, null, null, null));
            }
            return systems.zlink.framework.ZLinkSubmitStage.from(CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.publish(topic, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            }));
        }
    }

    private static List<Message> parts(Optional<String> packetName, Message payload) {
        return packetName
            .map(name -> List.of(Message.from(name.getBytes(StandardCharsets.UTF_8)), payload))
            .orElseGet(() -> List.of(payload));
    }

    private static Message spotRouteReplyPayload(List<Message> parts) {
        return parts.size() > 1 ? parts.get(parts.size() - 1) : parts.get(0);
    }

    private static String describeMessageParts(List<Message> parts) {
        List<String> descriptions = new ArrayList<>(parts.size());
        for (Message part : parts) {
            byte[] bytes = part.toByteArray();
            String text = new String(
                bytes,
                0,
                Math.min(bytes.length, 64),
                StandardCharsets.UTF_8)
                .replace("\n", "\\n")
                .replace("\r", "\\r");
            descriptions.add(bytes.length + ":" + text);
        }
        return descriptions.toString();
    }

    private static boolean isFrameworkErrorReply(List<Message> parts) {
        return parts.size() >= 2
            && FRAMEWORK_ERROR_REPLY_MARKER.equals(parts.get(0).toUtf8String());
    }

    private void replySpotRouteDispatchError(
        ZLinkBackendReceived received,
        String packetName,
        RoutingId spotRid,
        ZLinkDispatchErrorReason reason,
        Throwable error) {
        Throwable cause = unwrapCompletion(error);
        List<Message> reply = List.of(
            Message.from(FRAMEWORK_ERROR_REPLY_MARKER.getBytes(StandardCharsets.UTF_8)),
            Message.from(errorText(reason, packetName, cause).getBytes(StandardCharsets.UTF_8)));
        try {
            received.reply(reply);
        } catch (RuntimeException ignored) {
        } finally {
            reply.forEach(Message::close);
        }
        reportDispatchError(
            ZLinkDispatchErrorSurface.SPOT_ROUTE,
            ZLinkDispatchMessageKind.REQUEST,
            reason,
            ZLinkDispatchErrorAction.REPLY_ERROR,
            packetName,
            null,
            null,
            spotRid,
            null,
            received.routingId().orElse(null),
            received.requestSeq().map(Object::toString).orElse(null),
            cause);
    }

    private static Throwable unwrapCompletion(Throwable error) {
        if (error instanceof CompletionException && error.getCause() != null) {
            return error.getCause();
        }
        return error;
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

    private static String frameworkErrorReplyMessage(List<Message> parts) {
        return parts.get(1).toUtf8String();
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

    private static void traceSpotRouteInbound(
        String phase,
        ZLinkBackendSpot backendSpot,
        ZLinkBackendReceived received) {
        if (!STREAM_TRACE) {
            return;
        }
        System.out.println("[zlink-java-stream-trace] spot-route " + phase
            + " localSpot=" + backendSpot.routingId()
            + " sourceRid=" + received.routingId().map(Object::toString).orElse(null)
            + " sourceSpot=" + received.spotRid().map(Object::toString).orElse(null)
            + " requestSeq=" + received.requestSeq().map(Object::toString).orElse(null)
            + " result=" + received.result()
            + " parts=" + describeTraceParts(received.parts()));
    }

    private static void traceSpotRouteDispatch(
        String phase,
        ZLinkBackendSpot backendSpot,
        ZLinkBackendReceived received,
        ParsedPacket packet) {
        if (!STREAM_TRACE) {
            return;
        }
        System.out.println("[zlink-java-stream-trace] spot-route " + phase
            + " localSpot=" + backendSpot.routingId()
            + " sourceRid=" + received.routingId().map(Object::toString).orElse(null)
            + " sourceSpot=" + received.spotRid().map(Object::toString).orElse(null)
            + " requestSeq=" + received.requestSeq().map(Object::toString).orElse(null)
            + " packet=" + packet.packetName()
            + " payloadBytes=" + packet.payload().size());
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

    private record ParsedPacket(String packetName, Message payload) {
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

    private static SpotSubscriptionHandlerRegistration createConfiguredSpotSubscriptionRegistration(
        Class<?> handlerType,
        Class<?> expectedSpotType) {
        ZLinkSpotSubscription annotation = handlerType.getAnnotation(ZLinkSpotSubscription.class);
        if (annotation == null) {
            throw new ZLinkConfigurationException(
                "SPOT subscription handler topic is required: " + handlerType.getName());
        }
        return createSpotSubscriptionRegistration(
            requireTopic(annotation.topic()),
            handlerType,
            expectedSpotType);
    }

    private void registerConfiguredSpotHandler(
        Class<?> handlerType,
        Class<?> expectedSpotType,
        Map<String, SpotPacketHandlerRegistration> packetHandlers,
        Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers) {
        boolean matched = false;
        if (isSpotPacketHandlerType(handlerType)) {
            SpotPacketHandlerRegistration registration =
                createSpotPacketRegistration(handlerType, expectedSpotType);
            addConfiguredPacketHandler(packetHandlers, registration);
            matched = true;
        }
        if (isSpotSubscriptionHandlerType(handlerType)) {
            addConfiguredSubscriptionHandler(
                subscriptionHandlers,
                createConfiguredSpotSubscriptionRegistration(handlerType, expectedSpotType));
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
            ZLinkSpotSubscription subscription = method.getAnnotation(ZLinkSpotSubscription.class);
            if (subscription != null) {
                addConfiguredSubscriptionHandler(
                    subscriptionHandlers,
                    createSpotSubscriptionRegistration(
                        requireTopic(subscription.topic()),
                        handlerType,
                        expectedSpotType));
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

    private static boolean isSpotSubscriptionHandlerType(Class<?> handlerType) {
        return findInterface(handlerType, ZLinkSpotSubscriptionHandler.class) != null
            || findInterface(handlerType, KOTLIN_SPOT_SUBSCRIPTION_HANDLER) != null;
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

    private static void addConfiguredSubscriptionHandler(
        Map<String, List<SpotSubscriptionHandlerRegistration>> handlers,
        SpotSubscriptionHandlerRegistration registration) {
        handlers
            .computeIfAbsent(registration.topic(), ignored -> new ArrayList<>())
            .add(registration);
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
        return ZLinkPacketNames.resolve(messageType);
    }

    private static String resolvePacketName(Class<?> messageType, String explicitPacketName) {
        return ZLinkPacketNames.resolve(messageType, explicitPacketName);
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
        private final Set<ZLinkBackendReceived> activeRouteReceives =
            java.util.Collections.synchronizedSet(
                java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>()));
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
            if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                drainRoutesForDispatch();
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
            return systems.zlink.framework.ZLinkSubmitStage.completed();
        }

        private void drainRoutesForDispatch() {
            List<ZLinkBackendReceived> routes = new ArrayList<>();
            while (true) {
                ZLinkBackendReceived received =
                    backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    break;
                }
                if (channels != null
                    && channels.dispatchSpotRouteBridgePacket(received)) {
                    received.close();
                    continue;
                }
                routes.add(received);
            }
            if (routes.isEmpty()) {
                return;
            }
            context.enqueueDispatch(() -> dispatchRoutesAsync(routes));
        }

        private CompletionStage<Void> dispatchRoutesAsync(List<ZLinkBackendReceived> routes) {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            for (ZLinkBackendReceived received : routes) {
                tail = tail.thenCompose(ignored -> dispatchRouteAsync(received));
            }
            return tail;
        }

        private void drainPolledDispatchQueues() {
            context.enqueueDispatch(() -> drainRoutesAsync()
                .thenCompose(ignored -> drainUnhandledActorJoinsAsync())
                .thenCompose(ignored -> drainActorLifecycleEventsAsync()));
        }

        private CompletionStage<Void> drainRoutesAsync() {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            while (true) {
                ZLinkBackendReceived received =
                    backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return tail;
                }
                traceSpotRouteInbound("spot-recv", backendSpot, received);
                if (channels != null
                    && channels.dispatchSpotRouteBridgePacket(received)) {
                    received.close();
                    continue;
                }
                tail = tail.thenCompose(ignored -> dispatchRouteAsync(received));
            }
        }

        private CompletionStage<Void> dispatchRouteAsync(ZLinkBackendReceived received) {
            activeRouteReceives.add(received);
            if (isProbeFrame(received.parts())) {
                closeRouteReceived(received);
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            }
            ParsedPacket packet = parsePacket(received.parts());
            traceSpotRouteDispatch("spot-dispatch", backendSpot, received, packet);
            if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
                dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.RECEIVED,
                    ZLinkDispatchErrorSurface.SPOT_ROUTE,
                    received.requestSeq().isPresent()
                        ? ZLinkDispatchMessageKind.REQUEST
                        : ZLinkDispatchMessageKind.SEND,
                    packet.packetName(), null, null,
                    received.requestSeq().map(String::valueOf).orElse(null),
                    null, backendSpot.routingId().toString(), null, null));
            }
            if (ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME.equals(packet.packetName())) {
                return dispatchRoutedActorJoinAsync(received, packet);
            }
            if (ZLinkActorSpotRoutePackets.BOUND_SESSION_SEND_PACKET_NAME.equals(packet.packetName())) {
                CompletionStage<?> stage = received.requestSeq().isPresent()
                    ? handleRoutedBoundSessionSendRequestParts(received.parts())
                        .thenAccept(received::reply)
                    : handleRoutedBoundSessionSendParts(received.parts());
                return stage
                    .thenApply(ignored -> (Void) null)
                    .whenComplete((ignored, error) -> closeRouteReceived(received));
            }
            if (ZLinkActorSpotRoutePackets.ACTOR_PACKET_NAME.equals(packet.packetName())) {
                return handleRoutedActorPacketParts(received.parts())
                    .thenAccept(reply -> reply.ifPresent(message -> received.reply(List.of(message))))
                    .thenApply(ignored -> (Void) null)
                    .whenComplete((ignored, error) -> closeRouteReceived(received));
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
                closeRouteReceived(received);
                return systems.zlink.framework.ZLinkSubmitStage.completed();
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
                    closeRouteReceived(received);
                    return systems.zlink.framework.ZLinkSubmitStage.completed();
                }
                Message payloadCopy = Message.from(packet.payload());
                return withCurrentOutbound(context.outbound, () ->
                    invokeSpotRequestHandler(handler, spot, payloadCopy))
                    .thenAccept(reply -> received.reply(List.of(reply)))
                    .whenComplete((ignored, error) -> {
                        if (error != null) {
                            if (!closing) {
                                replySpotRouteDispatchError(
                                    received,
                                    packet.packetName(),
                                    backendSpot.routingId(),
                                    ZLinkDispatchErrorReason.HANDLER_EXCEPTION,
                                    error);
                            }
                        }
                        payloadCopy.close();
                        closeRouteReceived(received);
                        if (error == null
                            && dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLIED)) {
                            dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                                ZLinkMessageFlowOutcome.REPLIED,
                                ZLinkDispatchErrorSurface.SPOT_ROUTE,
                                ZLinkDispatchMessageKind.REQUEST,
                                packet.packetName(), null, null,
                                received.requestSeq().map(String::valueOf).orElse(null),
                                null, backendSpot.routingId().toString(), null, null));
                        }
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
                closeRouteReceived(received);
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            }
            Message payloadCopy = Message.from(packet.payload());
            String routeAsyncSendPacket = packet.packetName();
            closeRouteReceived(received);
            return withCurrentOutbound(context.outbound, () ->
                invokeSpotPacketHandler(handler, spot, payloadCopy))
                .whenComplete((ignored, error) -> {
                    payloadCopy.close();
                    if (error == null
                        && dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.DISPATCHED)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowOutcome.DISPATCHED,
                            ZLinkDispatchErrorSurface.SPOT_ROUTE,
                            ZLinkDispatchMessageKind.SEND,
                            routeAsyncSendPacket, null, null, null,
                            null, backendSpot.routingId().toString(), null, null));
                    }
                });
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
                    return systems.zlink.framework.ZLinkSubmitStage.completed();
                }
                ParsedPacket packet = parsePacket(received.parts());
                if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.RECEIVED)) {
                    dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.RECEIVED,
                        ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                        ZLinkDispatchMessageKind.PUBLISH,
                        packet.packetName(), null, received.topic(),
                        null, null, backendSpot.routingId().toString(), null, null));
                }
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
                if (dispatched && dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.DISPATCHED)) {
                    dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.DISPATCHED,
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
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            }
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT) {
                RoutingId spotRid = event.info()
                    .previousSpotRid()
                    .orElse(backendSpot.routingId());
                if (isJoinedToDifferentSpot(actor, spotRid)) {
                    return systems.zlink.framework.ZLinkSubmitStage.completed();
                }
                if (consumeSuppressedActorLifecycleCallback(event.kind(), actor, spotRid)) {
                    return systems.zlink.framework.ZLinkSubmitStage.completed();
                }
                return actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> actorRuntime.markLeftAsync(actor)
                        .thenCompose(ignored -> notifySpotActorLifecycle(spot, actor, false)));
            }
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.DISCONNECTED) {
                return actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> notifySpotActorDisconnected(actor));
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            if (isJoinedToDifferentSpot(actor, spotRid)) {
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            }
            if (consumeSuppressedActorLifecycleCallback(event.kind(), actor, spotRid)) {
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            }
            if (isAlreadyJoinedTo(actor, actorRef, spotRid)) {
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            }
            return actorRuntime.submitActorDispatch(
                actor.actorId(),
                () -> actorRuntime.markJoinedAsync(
                        actor,
                        actorRef,
                        spotRid,
                        spotSurfaceFor(spotRid) instanceof ZLinkSpot<?> spot ? spot : null)
                    .thenCompose(ignored -> notifySpotActorLifecycle(spot, actor, true)));
        }

        private CompletionStage<Void> dispatchActorMessagesAsync(List<ZLinkBackendActorReceived> actorMessages) {
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
                    return systems.zlink.framework.ZLinkSubmitStage.completed();
                }
                if (pendingHeader) {
                    pendingActorHeader = null;
                }
                ActorPacketFrames.Header packetHeader = ActorPacketFrames.decode(headerPart);
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
                if (REMOTE_BOUND_SESSION_BIND_PACKET_NAME.equals(packetHeader.packetName())) {
                    actorRuntime.bindNativeSession(
                        actor,
                        primaryNode,
                        headerPart.actor(),
                        headerPart.sourceNodeRid(),
                        headerPart.sourceSessionRid());
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
                if (ZLinkActorSpotRoutePackets.SESSION_DISCONNECTED_PACKET_NAME.equals(packetHeader.packetName())) {
                    notifySpotActorDisconnected(actor).toCompletableFuture().join();
                    if (pendingHeader) {
                        headerPart.close();
                    }
                    continue;
                }
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
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> dispatchActorPacket(
                        handler,
                        actor,
                        packetHeader,
                        headerCopy,
                        payloadCopy));
            }
            return systems.zlink.framework.ZLinkSubmitStage.completed();
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            ZLinkActor actor,
            ActorPacketFrames.Header packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            return dispatchActorPacketToHandler(
                context.outbound,
                true,
                handler,
                spot,
                actor,
                packetHeader,
                headerPart,
                payload,
                "user actor bound session reply failed");
        }

        private void replyActorDispatchError(
            ActorPacketFrames.Header packetHeader,
            ZLinkBackendActorReceived headerPart,
            String actorId,
            Throwable error,
            String failureMessage) {
            byte[] frameBytes;
            try (Message frame = ActorPacketFrames.encodeError(packetHeader, error)) {
                frameBytes = frame.toByteArray();
            } catch (RuntimeException ex) {
                headerPart.close();
                return;
            }
            if (isNoBindActorRequest(packetHeader, headerPart)) {
                context.enqueueDispatch(() -> {
                    try (Message frame = Message.from(frameBytes)) {
                        primaryNode.replyActorNoBind(
                            new ZLinkBackendActorRef(
                                headerPart.actor().nodeRid(),
                                actorId,
                                headerPart.actor().generation()),
                            headerPart.sourceNodeRid(),
                            headerPart.sourceSessionRid(),
                            headerPart.requestId(),
                            headerPart.flags(),
                            List.of(frame));
                        return systems.zlink.framework.ZLinkSubmitStage.completed();
                    } finally {
                        headerPart.close();
                    }
                });
                return;
            }
            context.enqueueDispatch(() -> sendActorBoundSessionWithRetry(
                    primaryNode,
                    new ZLinkBackendActorRef(
                        headerPart.actor().nodeRid(),
                        actorId,
                        headerPart.actor().generation()),
                    actorId,
                    frameBytes,
                    failureMessage)
                .whenComplete((ignored, sendError) -> headerPart.close()));
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
                        : ZLinkMessagePayloads.message(effective.reply(), serializer);
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
                .thenAccept(received::reply)
                .whenComplete((ignored, error) -> {
                    if (error != null) {
                        replySpotRouteDispatchError(
                            received,
                            packet.packetName(),
                            backendSpot.routingId(),
                            ZLinkDispatchErrorReason.HANDLER_EXCEPTION,
                            error);
                    } else if (dispatchErrors.flow().enabled(ZLinkMessageFlowOutcome.REPLIED)) {
                        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowOutcome.REPLIED,
                            ZLinkDispatchErrorSurface.SPOT_ROUTE,
                            ZLinkDispatchMessageKind.REQUEST,
                            packet.packetName(), null, null,
                            received.requestSeq().map(String::valueOf).orElse(null),
                            null, backendSpot.routingId().toString(), null, null));
                    }
                    closeRouteReceived(received);
                });
        }

        private CompletionStage<Void> handleRoutedBoundSessionSendParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.BoundSessionSend send =
                ZLinkActorSpotRoutePackets.decodeBoundSessionSend(parts);
            byte[] frameBytes = send.frame().toByteArray();
            CompletionStage<Void> sendStage = actorRuntime.localActor(send.actorRef().actorId())
                .map(actor -> actorRuntime.sendBoundSessionFrame(actor, frameBytes)
                    .thenCompose(sent -> sent
                        ? CompletableFuture.completedFuture(null)
                        : sendActorBoundSessionWithRetry(
                            primaryNode(),
                            send.actorRef(),
                            send.actorRef().actorId(),
                            frameBytes,
                            "routed actor bound session send failed")))
                .orElseGet(() -> sendActorBoundSessionWithRetry(
                    primaryNode(),
                    send.actorRef(),
                    send.actorRef().actorId(),
                    frameBytes,
                    "routed actor bound session send failed"));
            return sendStage
                .whenComplete((ignored, error) -> send.close());
        }

        private CompletionStage<List<Message>> handleRoutedBoundSessionSendRequestParts(List<Message> parts) {
            ZLinkActorSpotRoutePackets.BoundSessionSend send =
                ZLinkActorSpotRoutePackets.decodeBoundSessionSend(parts);
            byte[] frameBytes = send.frame().toByteArray();
            CompletionStage<Void> sendStage = actorRuntime.localActor(send.actorRef().actorId())
                .map(actor -> actorRuntime.sendBoundSessionFrame(actor, frameBytes)
                    .thenCompose(sent -> sent
                        ? CompletableFuture.completedFuture(null)
                        : sendActorBoundSessionWithRetry(
                            primaryNode(),
                            send.actorRef(),
                            send.actorRef().actorId(),
                            frameBytes,
                            "routed actor bound session send failed")))
                .orElseGet(() -> sendActorBoundSessionWithRetry(
                    primaryNode(),
                    send.actorRef(),
                    send.actorRef().actorId(),
                    frameBytes,
                    "routed actor bound session send failed"));
            return sendStage
                .thenApply(ignored -> List.of(Message.from(new byte[0])))
                .whenComplete((ignored, error) -> send.close());
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
            return actorRuntime
                .getOrCreateManagedActorWithoutLocationClaim(
                    joinRequest.actorId(),
                    joinRequest.actorType())
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
                    } else if (routeChannelName != null || sourcePeerRid != null) {
                        routedBindingToken[0] = actorRuntime.bindRoutedSession(
                            actor,
                            routeChannelName,
                            sourcePeerRid == null ? joinRequest.actorRef().nodeRid() : sourcePeerRid,
                            joinRequest.sourceEntrySpotRid(),
                            joinRequest.actorRef());
                    } else {
                        routedBindingToken[0] = actorRuntime.bindNativeSession(
                            actor,
                            primaryNode,
                            localActorRef);
                    }
                    return withCurrentOutbound(context.outbound, () ->
                        ZLinkHandlerStages.fromSupplier(() -> ((ZLinkSpot) spot).onActorJoin(
                                actor,
                                ZLinkMessage.fromEncoded(ZLinkMessagePayloads.encoded(joinPayload), serializer),
                                NONE_CANCELLATION)))
                        .thenCompose(response -> {
                            ZLinkSpotActorJoinResponse effective =
                                response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                            if (!effective.accepted()) {
                                if (routedBindingToken[0] >= 0) {
                                    actorRuntime.clearSessionBinding(actor, routedBindingToken[0]);
                                }
                                return CompletableFuture.completedFuture(effective);
                            }
                            return actorRuntime.markJoinedAsync(
                                    actor,
                                    localActorRef,
                                    backendSpot.routingId(),
                                    spotFor(backendSpot.routingId()))
                                .thenCompose(ignored -> notifySpotActorLifecycleAndSuppressBackendEvent(
                                    spot,
                                    actor,
                                    backendSpot.routingId(),
                                    true))
                                .thenApply(ignored -> effective)
                                .whenComplete((reply, error) -> {
                                    if (error != null) {
                                        actorRuntime.markLeftAsync(actor);
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
                            Message reply = effective.reply() == null
                                ? Message.from(new byte[0])
                                : ZLinkMessagePayloads.message(effective.reply(), serializer);
                            try {
                                return ZLinkActorSpotRoutePackets.encodeJoinReply(
                                    effective.accepted(),
                                    localActorRef,
                                    reply);
                            } finally {
                                reply.close();
                            }
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
                        .fromSupplier(() -> ((ZLinkSpot) spot).onActorJoin(
                            actor.get(),
                            ZLinkMessage.fromEncoded(ZLinkMessagePayloads.encoded(payload), serializer),
                            NONE_CANCELLATION))
                        .thenCompose(response -> {
                            ZLinkSpotActorJoinResponse effective =
                                response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                            if (!effective.accepted()) {
                                return CompletableFuture.completedFuture(effective);
                            }
                            return actorRuntime.markJoinedAsync(
                                    actor.get(),
                                    request.targetActor(),
                                    backendSpot.routingId(),
                                    spotFor(backendSpot.routingId()))
                                .thenCompose(ignored -> notifySpotActorLifecycleAndSuppressBackendEvent(
                                    spot,
                                    actor.get(),
                                    backendSpot.routingId(),
                                    true))
                                .thenApply(ignored -> effective)
                                .whenComplete((reply, error) -> {
                                    if (error != null) {
                                        actorRuntime.markLeftAsync(actor.get());
                                    }
                                });
                        });
                });
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
            closeActiveRouteReceives();
            context.closeTimers();
            backendSpot.close();
        }

        private void closeActiveRouteReceives() {
            for (ZLinkBackendReceived received : List.copyOf(activeRouteReceives)) {
                closeRouteReceived(received);
            }
        }

        private void closeRouteReceived(ZLinkBackendReceived received) {
            if (activeRouteReceives.remove(received)) {
                received.close();
            }
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
