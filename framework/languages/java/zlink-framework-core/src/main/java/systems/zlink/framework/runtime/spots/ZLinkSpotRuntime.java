package systems.zlink.framework.runtime.spots;

import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

import systems.zlink.framework.runtime.backend.*;

import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
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
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Supplier;
import java.util.logging.Logger;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkDispatchFailure;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorReplyRoute;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.channels.ChannelRegistration;
import systems.zlink.framework.runtime.channels.ChannelKind;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.messaging.ZLinkFrameworkErrorReply;
import systems.zlink.framework.runtime.messaging.ZLinkPacketNames;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkWorkerCall;
import systems.zlink.framework.spots.ZLinkWorkerTask;
import systems.zlink.framework.spots.ZLinkSpotCreateState;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.spots.ZLinkSpotHandlerRegistry;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

public final class ZLinkSpotRuntime
    extends ZLinkSpotContextHost
    implements ZLinkSpotManager, AutoCloseable {
    private static final Logger LOGGER = Logger.getLogger(ZLinkSpotRuntime.class.getName());
    private static final int ACTOR_RECV_INFO_NO_BIND = 1;

    private static final String REMOTE_BOUND_SESSION_BIND_PACKET_NAME =
        "zlink.framework.actor.bound_session.bind";

    private static final boolean STREAM_TRACE =
        "1".equals(System.getenv("ZLINK_JAVA_STREAM_TRACE"));
    private final ZLinkBackendContext context;
    private final boolean ownsContext;
    private final ZLinkFrameworkRegistration frameworkRegistration;
    private final List<ZLinkInternalSpotNode> nodes = new ArrayList<>();
    private final Map<String, ZLinkInternalSpotNode> nodesByName = new HashMap<>();
    private final ZLinkSpotLocationCoordinator spotLocations =
        new ZLinkSpotLocationCoordinator();
    private final ZLinkSpotLifecycle spotLifecycle;
    private final ZLinkActorSessionCoordinator actorSessions =
        new ZLinkActorSessionCoordinator();
    private final ZLinkActorSpotAdmission actorAdmissions =
        new ZLinkActorSpotAdmission();
    private final ZLinkInternalSpotNode primaryNode;
    private final String primaryNodeSourceName;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkSpotRouteMessages routeMessages;
    private final ZLinkSpotDirectOutbound directOutbound;
    private final ZLinkSpotRoutedOutbound routedOutbound;
    private final ZLinkSpotPublisherRuntime publishers;
    private final ZLinkHandlerActivator handlerFactory;
    private final ZLinkDispatchErrorReporter dispatchErrors;
    private final ZLinkRuntimeEventDispatcher eventDispatcher;
    private final Executor handlerExecutor;
    private final List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers;
    private final ZLinkSpotHandlerInvoker spotHandlerInvoker;
    private final ZLinkSpotActorHandlerCatalog actorHandlers;
    private final ZLinkSpotHandlerLoader handlerLoader;
    private final ZLinkSpotActivationFactory activationFactory;
    private final Duration defaultRequestTimeout;
    private final ZLinkActorBoundSessionSender boundSessionSender;
    private final ZLinkChannelRuntime channels;
    private final List<ChannelRegistration> routeMeshChannels = new ArrayList<>();
    private final Set<RoutingId> manualRouterPeerNodeRids = ConcurrentHashMap.newKeySet();
    private final Set<RoutingId> autoConnectedRouterPeerNodeRids = ConcurrentHashMap.newKeySet();
    private final Set<String> suppressedActorLifecycleCallbacks = ConcurrentHashMap.newKeySet();
    private final ZLinkSpotOutboundScope outboundScope = new ZLinkSpotOutboundScope();
    private volatile boolean closing;
    private volatile boolean draining;
    private int drainInitialUserSpots;
    private final java.util.concurrent.atomic.AtomicBoolean drainRoomsMetricRecorded =
        new java.util.concurrent.atomic.AtomicBoolean();
    private final systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy drainPolicy;
    private final ZLinkWorkerPool workerPool;
    private final ScheduledExecutorService timerExecutor = Executors.newScheduledThreadPool(1, task -> {
        Thread thread = new Thread(task, "zlink-java-spot-timer");
        thread.setDaemon(true);
        return thread;
    });

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration) {
        this(backendFactory, adapterOptions, registration, null);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels) {
        this(backendFactory, adapterOptions, registration, channels, ZLinkHandlerActivator.reflection());
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkHandlerActivator handlerFactory) {
        this(
            backendFactory,
            adapterOptions,
            registration,
            channels,
            new ZLinkStringMessageSerializer(),
            handlerFactory);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory) {
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
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory,
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
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkBackendContext context,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory,
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
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkBackendContext context,
        boolean ownsContext,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        if (registration.spotNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one SpotNode is required");
        }
        this.frameworkRegistration = registration;
        this.channels = channels;
        this.serializer = java.util.Objects.requireNonNull(serializer, "serializer");
        this.routeMessages = new ZLinkSpotRouteMessages(this.serializer);
        this.handlerFactory = handlerFactory;
        this.eventDispatcher = eventDispatcher;
        this.handlerExecutor = systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext
            .propagating(java.util.Objects.requireNonNull(
                registration.handlerExecutor(), "handlerExecutor"));
        this.dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.dispatchOptions(),
            handlerFactory,
            this.handlerExecutor,
            eventDispatcher);
        this.directOutbound = new ZLinkSpotDirectOutbound(
            routeMessages,
            this.handlerExecutor,
            dispatchErrors.flow());
        this.routedOutbound = new ZLinkSpotRoutedOutbound(
            channels,
            routeMessages,
            directOutbound,
            dispatchErrors.flow(),
            nodesByName::get);
        this.publishers = new ZLinkSpotPublisherRuntime(
            serializer,
            routeMessages,
            dispatchErrors.flow());
        this.suspendHandlerInvokers = registration.suspendHandlerInvokers();
        this.spotHandlerInvoker = new ZLinkSpotHandlerInvoker(
            serializer,
            handlerFactory,
            suspendHandlerInvokers);
        this.workerPool = new ZLinkWorkerPool(
            registration.workers().minThreads(),
            registration.workers().maxThreads(),
            registration.workers().idleTimeout(),
            registration.workers().maxQueueLength());
        ZLinkScannedHandlerCatalog scannedHandlers =
            ZLinkHandlerScanner.scan(registration.handlerPackageMarkers());
        this.actorHandlers = new ZLinkSpotActorHandlerCatalog(scannedHandlers, serializer);
        this.handlerLoader = new ZLinkSpotHandlerLoader(scannedHandlers, actorHandlers);
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkSpotBackendAdapter spotAdapter =
            backendFactory.createSpotAdapter(adapterOptions);
        this.context = context == null ? channelAdapter.createContext() : context;
        this.ownsContext = ownsContext;
        this.defaultRequestTimeout = registration.defaultRequestTimeout();
        this.boundSessionSender = new ZLinkActorBoundSessionSender(
            defaultRequestTimeout,
            this::isClosing,
            ZLinkSpotRuntime::traceActorSession);
        Map<String, ZLinkInternalSpotNode> routeBridgeNodesByName = new HashMap<>();
        Set<Class<? extends ZLinkSpot<?>>> initializedSpotTypes = new HashSet<>();
        List<EntrySpotInitialization> entrySpotInitializations =
            new ArrayList<>();
        for (SpotNodeRegistration nodeRegistration : registration.spotNodes()) {
            ZLinkInternalSpotNode node =
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
                entrySpotInitializations.add(new EntrySpotInitialization(
                    node.routingId(),
                    entryBackendSpot,
                    nodeRegistration));
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
            initializedSpotTypes.addAll(nodeRegistration.spotFactories());
            if (nodeRegistration.pubSubEnabled()) {
                publishers.register(nodeRegistration.meshName(), node);
            }
            if (nodeRegistration.routerEnabled()) {
                routeBridgeNodesByName.put(nodeRegistration.nodeName(), node);
                if (channels != null) {
                    channels.registerSpotRouterNode(nodeRegistration.meshName(), node);
                    if (!nodeRegistration.meshName().equals(nodeRegistration.nodeName())) {
                        channels.registerSpotRouterNode(nodeRegistration.nodeName(), node);
                    }
                }
            }
            spotLocations.registerNode(
                nodeRegistration.meshName(),
                node.routingId(),
                nodeRegistration.routerBind(),
                nodeRegistration.pubSubEnabled());
        }
        for (var channel : registration.channels()) {
            if (channel.kind() == ChannelKind.ROUTE_MESH) {
                routeMeshChannels.add(channel);
            }
        }
        attachRouteMeshSpotBridges(routeBridgeNodesByName);
        this.primaryNode = nodes.get(0);
        this.drainPolicy = registration.spotNodes().get(0).drainPolicy();
        this.primaryNodeSourceName = registration.spotNodes().get(0).nodeName();
        this.activationFactory = new ZLinkSpotActivationFactory(
            this,
            workerPool,
            handlerLoader,
            spotHandlerInvoker,
            handlerFactory);
        this.spotLifecycle = new ZLinkSpotLifecycle(
            primaryNode,
            handlerExecutor,
            spotLocations,
            initializedSpotTypes,
            activationFactory::activate,
            actorSessions::hasActorsInSpot);
        for (EntrySpotInitialization initialization : entrySpotInitializations) {
            for (Class<? extends ZLinkEntrySpot<?>> entrySpotType :
                initialization.registration().entrySpots()) {
                spotLifecycle.addEntrySpot(activationFactory.activateEntry(
                    initialization.nodeRid(),
                    initialization.backendSpot(),
                    entrySpotType));
            }
        }
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

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType) {
        requireAcceptingNewState();
        return spotLifecycle.create(spotType, ZLinkMessage.empty());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkMessage request) {
        requireAcceptingNewState();
        return spotLifecycle.create(spotType, request);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        requireAcceptingNewState();
        return spotLifecycle.create(spotType, spotRid, ZLinkMessage.empty());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        requireAcceptingNewState(spotRid);
        return spotLifecycle.getOrCreate(spotType, spotRid, ZLinkMessage.empty());
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        ZLinkMessage request) {
        requireAcceptingNewState(spotRid);
        return spotLifecycle.getOrCreate(spotType, spotRid, request);
    }

    @Override
    public CompletionStage<Optional<ZLinkSpotInfo>> find(RoutingId spotRid) {
        return spotLifecycle.find(spotRid);
    }

    @Override
    public CompletionStage<List<ZLinkSpotInfo>> list() {
        return spotLifecycle.list();
    }

    @Override
    public CompletionStage<Boolean> close(RoutingId spotRid) {
        return spotLifecycle.close(spotRid);
    }

    @Override
    public void close() {
        closeAsync();
    }

    public CompletionStage<Void> closeAsync() {
        beginClose();
        return spotLifecycle.closeAllAsync().handle((ignored, failure) -> {
            RuntimeException firstFailure = failure == null ? null
                : failure instanceof RuntimeException runtime ? runtime : new RuntimeException(failure);
            firstFailure = closeRuntimeComponent(publishers::close, firstFailure);
            for (ZLinkInternalSpotNode node : nodes) {
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
            return null;
        });
    }

    public void beginClose() {
        closing = true;
    }

    public CompletionStage<Void> beginDrain() {
        draining = true;
        drainInitialUserSpots = spotLifecycle.userSpotCount();
        return continueDrain();
    }

    public CompletionStage<Void> continueDrain() {
        CompletionStage<Void> drain = drainPolicy
            == systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy.RELEASE_AND_RECREATE
            ? spotLifecycle.releaseRecreatableSpots()
            : CompletableFuture.completedFuture(null);
        return drain.thenRun(this::recordDrainedRoomsIfComplete);
    }

    public boolean drainComplete() {
        boolean complete = spotLifecycle.userSpotsDrained();
        if (complete) {
            recordDrainedRoomsIfComplete();
        }
        return complete;
    }

    private void recordDrainedRoomsIfComplete() {
        if (!spotLifecycle.userSpotsDrained()
            || !drainRoomsMetricRecorded.compareAndSet(false, true)) {
            return;
        }
        String policy = drainPolicy
            == systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy.RELEASE_AND_RECREATE
            ? "release-and-recreate"
            : "drain-natural";
        for (int index = 0; index < drainInitialUserSpots; index++) {
            systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics.increment(
                "zlink.drain.rooms.drained", java.util.Map.of("policy", policy));
        }
    }

    private void requireAcceptingNewState() {
        if (closing || draining) {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_REJECTED,
                "SPOT creation is rejected while the node is draining");
        }
    }

    private void requireAcceptingNewState(RoutingId spotRid) {
        if ((closing || draining) && !spotLifecycle.hasUserSpot(spotRid)) {
            requireAcceptingNewState();
        }
    }

    boolean isClosing() {
        return closing;
    }

    boolean isDraining() {
        return draining;
    }

    ZLinkActorSessionCoordinator actorSessions() {
        return actorSessions;
    }

    ZLinkActorSpotAdmission actorAdmissions() {
        return actorAdmissions;
    }

    ZLinkMessageSerializer serializerForSpot() {
        return serializer;
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

    public ZLinkInternalSpotNode primaryNode() {
        return primaryNode;
    }

    public ZLinkInternalSpotNode node(String nodeName) {
        ZLinkInternalSpotNode node = nodesByName.get(nodeName);
        if (node == null) {
            throw new ZLinkConfigurationException("unknown SpotNode: " + nodeName);
        }
        return node;
    }

    private ZLinkInternalSpotNode nodeByRid(RoutingId nodeRid) {
        for (ZLinkInternalSpotNode node : nodes) {
            if (node.routingId().equals(nodeRid)) {
                return node;
            }
        }
        throw new ZLinkConfigurationException("unknown SpotNode routing id: " + nodeRid);
    }

    public void attachActorRuntime(ZLinkActorRuntime actorRuntime) {
        actorSessions.attach(
            actorRuntime,
            this::notifyEntrySpotActorCreated,
            systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationContext
                ::currentEntrySpotDispatch,
            this::notifySpotActorDisconnected,
            this::notifySourceActorLeftForRemoteMove,
            this::spotFor,
            this::meshNameForSpot);
        actorAdmissions.attach(actorRuntime, this::isDraining);
    }

    public Map<String, ZLinkInternalSpotNode> nodesByName() {
        return Map.copyOf(nodesByName);
    }

    public boolean isSessionRelayRouteReady(RoutingId nodeRid) {
        if (!hasRoutingId(nodeRid)) {
            return true;
        }
        for (ZLinkInternalSpotNode node : nodes) {
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

    public ZLinkSpotOutbound outbound() {
        return outboundScope.ambient();
    }

    public ZLinkSpotPublisherClient publisherClient() {
        return publishers.client();
    }

    public void setLocationLifecycle(ZLinkLocationLifecycle lifecycle) {
        spotLocations.setLifecycle(lifecycle);
    }

    public CompletionStage<Void> claimEntrySpotLocations() {
        return spotLocations.claimEntrySpotsAsync();
    }

    public CompletionStage<Optional<Message>> dispatchLocalSessionActor(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload) {
        return actorSessions.dispatchLocalSession(
            actorRef,
            header,
            payload,
            spotRid -> spotSurfaceFor(spotRid) != null,
            local -> dispatchLocalSessionActor(
                actorRef,
                header,
                payload,
                local));
    }

    Optional<Message> replyTransferredRequestDirect(
        ZLinkStreamHeader requestHeader,
        ZLinkActorReplyRoute replyRoute,
        Optional<Message> reply) {
        if (replyRoute == null || reply.isEmpty()) {
            return reply;
        }
        traceMessageFlow(
            ZLinkMessageFlowOutcome.REPLIED,
            ZLinkDispatchErrorSurface.SPOT_ACTOR,
            ZLinkDispatchMessageKind.ACTOR_REQUEST,
            "handoff_direct_reply",
            null,
            null,
            Long.toUnsignedString(replyRoute.requestId()),
            replyRoute.sourceNodeRid().toString(),
            null,
            replyRoute.actorRef().actorId());
        try (Message payload = reply.get()) {
            ZLinkStreamHeader responseHeader = ZLinkStreamHeader.createResponse(
                requestHeader,
                requestHeader.codec(),
                java.util.EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                requestHeader.packetName(),
                requestHeader.metadata());
            try (Message frame = Message.from(
                    systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodec.encode(
                        responseHeader,
                        payload.toByteArray()))) {
                try {
                    primaryNode.replyActorNoBind(
                        replyRoute.actorRef(),
                        replyRoute.sourceNodeRid(),
                        replyRoute.sourceSessionRid(),
                        replyRoute.requestId(),
                        replyRoute.flags(),
                        List.of(frame));
                } catch (RuntimeException error) {
                    throw new ZLinkConfigurationException(
                        "handoff direct reply failed sourceNode="
                            + replyRoute.sourceNodeRid()
                            + " sourceSession=" + replyRoute.sourceSessionRid()
                            + " requestId=" + Long.toUnsignedString(replyRoute.requestId()),
                        error);
                }
            }
        }
        return Optional.empty();
    }

    private CompletionStage<Optional<Message>> dispatchLocalSessionActor(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload,
        ZLinkActorSessionCoordinator.LocalDispatch local) {
        ZLinkActor actor = local.actor();
        Object spotSurface = localActorSpotSurface(actor);
        boolean isRequest = header.requestSequence().isPresent()
            || header.kind() == ZLinkStreamMessageKind.REQUEST;
        SpotActorPacketHandlerRegistration handler = resolveActorPacketHandler(
            header.packetName(),
            spotSurface,
            isRequest
                ? ZLinkScannedHandlerKind.ACTOR_REQUEST
                : ZLinkScannedHandlerKind.ACTOR_SEND);
        if (handler == null
            || isRequest != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
            ZLinkConfigurationException error = new ZLinkConfigurationException(
                handler == null
                    ? "actor packet handler is not registered: " + header.packetName()
                    : "actor packet kind does not match handler kind: " + header.packetName());
            reportDispatchError(DispatchFailureReport.of(
                    ZLinkDispatchErrorSurface.SPOT_ACTOR,
                    isRequest
                        ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                        : ZLinkDispatchMessageKind.ACTOR_SEND,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    isRequest
                        ? ZLinkDispatchErrorAction.REPLY_ERROR
                        : ZLinkDispatchErrorAction.DROP)
                .packetName(header.packetName())
                .spotRid(local.joinedSpotRid().orElse(null))
                .actorId(actor.actorId())
                .correlationId(header.requestSequence().map(Object::toString).orElse(null))
                .error(error));
            return CompletableFuture.failedFuture(error);
        }
        if (!handler.actorType().isInstance(actor)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor packet handler target type does not match actor: " + actorRef.actorId()));
        }
        return dispatchLocalSessionActorPacket(handler, spotSurface, actor, payload);
    }

    public void drainRoutedDispatchQueues() {
        if (closing) {
            return;
        }
        spotLifecycle.drainRoutedDispatchQueues();
    }

    private <T> CompletionStage<T> withCurrentOutbound(
        DefaultSpotOutbound outbound,
        Supplier<CompletionStage<T>> action) {
        CompletableFuture<T> result = new CompletableFuture<>();
        var flow = systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.current();
        Object entryDispatchContext = systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.currentEntrySpotDispatch();
        try {
            handlerExecutor.execute(() -> {
                outboundScope.run(outbound, () -> {
                    try (systems.zlink.framework.runtime.internal.handlers
                             .ZLinkSuspendInvocationContext.Scope ignored =
                             systems.zlink.framework.runtime.internal.handlers
                                 .ZLinkSuspendInvocationContext
                                 .enterEntrySpotDispatch(entryDispatchContext)) {
                        action.get().whenComplete((value, error) -> {
                            systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.run(flow, () -> {
                                if (error != null) {
                                    result.completeExceptionally(error);
                                } else {
                                    result.complete(value);
                                }
                            });
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

    private CompletionStage<Void> dispatchActorPacketToHandler(
        DefaultSpotOutbound outbound,
        SpotActorPacketHandlerRegistration handler,
        Object spotSurface,
        ZLinkActor actor,
        ActorPacketFrames.Header packetHeader,
        ZLinkBackendActorReceived headerPart,
        Message payload,
        String replyFailureMessage) {
        var actorFlow = systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.current();
        boolean noBindRequest = isNoBindActorRequest(packetHeader, headerPart);
        traceActorSession("dispatch-actor-packet"
            + " actor=" + actor.actorId()
            + " packet=" + packetHeader.packetName()
            + " requestSeq=" + packetHeader.requestSeq().map(Object::toString).orElse(null)
            + " sourceNode=" + headerPart.sourceNodeRid()
            + " sourceSession=" + headerPart.sourceSessionRid()
            + " noBind=" + noBindRequest
            + " hasBound=" + actorSessions.hasBoundSession(actor));
        boolean actorIsRequest = handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST;
        String actorPacketName = packetHeader.packetName();
        String actorId = actor.actorId();
        ZLinkDispatchMessageKind actorKind = actorIsRequest
            ? ZLinkDispatchMessageKind.ACTOR_REQUEST
            : ZLinkDispatchMessageKind.ACTOR_SEND;
        traceMessageFlow(
            ZLinkMessageFlowOutcome.RECEIVED,
            ZLinkDispatchErrorSurface.SPOT_ACTOR,
            actorKind,
            actorPacketName,
            null,
            null,
            packetHeader.requestSeq().map(String::valueOf).orElse(null),
            null,
            null,
            actorId);
        CompletionStage<Optional<Message>> stage = withCurrentOutbound(
            outbound,
            () -> actorSessions.runPacketTurn(
                actor,
                packetHeader.requestSeq().isPresent(),
                noBindRequest,
                headerPart,
                primaryNode,
                () -> actorIsRequest
                    ? invokeActorRequestHandler(handler, spotSurface, actor, payload)
                    : invokeActorSendHandler(handler, spotSurface, actor, payload)
                        .thenApply(ignored -> Optional.empty())));
        return stage.handle((reply, error) -> {
                if (error != null) {
                    reportDispatchError(DispatchFailureReport.of(
                            ZLinkDispatchErrorSurface.SPOT_ACTOR,
                            actorKind,
                            ZLinkDispatchErrorReason.HANDLER_EXCEPTION,
                            actorIsRequest
                                ? ZLinkDispatchErrorAction.REPLY_ERROR
                                : ZLinkDispatchErrorAction.DROP)
                        .packetName(actorPacketName)
                        .actorId(actorId)
                        .sourceRid(headerPart.sourceNodeRid())
                        .correlationId(packetHeader.requestSeq().map(String::valueOf).orElse(null))
                        .error(error));
                    return Optional.of(new ActorDispatchReply(
                        ActorPacketFrames.encodeError(packetHeader, error),
                        true));
                }
                return reply.map(message -> new ActorDispatchReply(message, false));
            })
            .thenCompose(reply -> {
                if (reply.isEmpty()) {
                    return java.util.concurrent.CompletableFuture.completedFuture(null);
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
                    return java.util.concurrent.CompletableFuture.completedFuture(null);
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
                systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.run(actorFlow, () -> {
                    payload.close();
                    headerPart.close();
                    if (error == null) {
                        ZLinkMessageFlowOutcome phase = actorIsRequest
                            ? ZLinkMessageFlowOutcome.REPLIED
                            : ZLinkMessageFlowOutcome.DISPATCHED;
                        traceMessageFlow(
                            phase,
                            ZLinkDispatchErrorSurface.SPOT_ACTOR,
                            actorKind,
                            actorPacketName,
                            null,
                            null,
                            packetHeader.requestSeq().map(String::valueOf).orElse(null),
                            null,
                            null,
                            actorId);
                    }
                });
            });
    }

    private CompletionStage<Void> invokeActorSendHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        return spotHandlerInvoker.invokeActorSend(
            registration,
            spotSurface,
            actor,
            payload,
            "failed to invoke Spot actor send handler");
    }

    private CompletionStage<Optional<Message>> invokeActorRequestHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        return spotHandlerInvoker.invokeActorRequest(
            registration,
            spotSurface,
            actor,
            payload,
            "failed to invoke Spot actor request handler");
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> notifySpotActorLifecycle(
        Object spotSurface,
        ZLinkActor actor,
        boolean joined) {
        if (spotSurface instanceof ZLinkSpot spot) {
            return withCurrentOutbound(
                ((DefaultSpotContext) spot.context()).dispatchOutbound(),
                () -> joined
                    ? ZLinkHandlerStages.fromStageSupplier(() -> spot.onJoinedActor(actor))
                    : ZLinkHandlerStages.fromStageSupplier(() -> spot.onLeaveActor(actor)));
        }
        if (spotSurface instanceof ZLinkEntrySpot entrySpot) {
            return joined
                ? ZLinkHandlerStages.fromStageSupplier(() -> entrySpot.onJoinedActor(actor))
                : ZLinkHandlerStages.fromStageSupplier(() -> entrySpot.onLeaveActor(actor));
        }
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    private boolean isAlreadyJoinedTo(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid) {
        return actorSessions.isJoinedTo(actor, actorRef, spotRid);
    }

    private boolean isJoinedToDifferentSpot(ZLinkActor actor, RoutingId spotRid) {
        return actorSessions.isJoinedToDifferentSpot(actor, spotRid);
    }

    ZLinkBackendActorRef actorLifecycleRef(ZLinkBackendActorLifecycleEvent event) {
        return event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
            ? event.info().previousActor()
            : event.info().currentActor();
    }

    private RoutingId actorLifecycleSpotRid(
        ZLinkBackendActorLifecycleEvent event,
        RoutingId fallbackSpotRid) {
        return event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
            ? event.info().previousSpotRid().orElse(fallbackSpotRid)
            : event.info().currentSpotRid().orElse(fallbackSpotRid);
    }

    private boolean shouldIgnoreJoinedOrLeftLifecycle(
        ZLinkBackendActorLifecycleEvent event,
        ZLinkBackendActorRef actorRef,
        ZLinkActor actor,
        RoutingId spotRid) {
        if (isJoinedToDifferentSpot(actor, spotRid)) {
            return true;
        }
        if (consumeSuppressedActorLifecycleCallback(event.kind(), actor, spotRid)) {
            return true;
        }
        return event.kind() != ZLinkBackendActorLifecycleEventKind.LEFT
            && isAlreadyJoinedTo(actor, actorRef, spotRid);
    }

    CompletionStage<Void> notifySpotActorLifecycleAndSuppressBackendEvent(
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
                    context.dispatchOutbound(),
                    () -> ZLinkHandlerStages.fromStageSupplier(() ->
                        spot.onDisconnectActor(actor)));
            }
            return ZLinkHandlerStages.fromStageSupplier(() -> spot.onDisconnectActor(actor));
        }
        if (spotSurface instanceof ZLinkEntrySpot entrySpot) {
            return ZLinkHandlerStages.fromStageSupplier(() ->
                entrySpot.onDisconnectActor(actor));
        }
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    private CompletionStage<Void> notifySourceActorLeftForRemoteMove(ZLinkActor actor) {
        Object spotSurface = localActorSpotSurface(actor);
        if (spotSurface == null) {
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        RoutingId spotRid = spotSurface instanceof ZLinkSpot<?> spot
            ? spot.context().spotRid()
            : ((ZLinkEntrySpot<?>) spotSurface).context().spotRid();
        return notifySpotActorLifecycleAndSuppressBackendEvent(
            spotSurface,
            actor,
            spotRid,
            false);
    }

    private CompletionStage<Void> notifyEntrySpotActorCreated(
        RoutingId nodeRid,
        ZLinkActor actor,
        ZLinkMessage createRequest,
        Object createContext) {
        return spotLifecycle.notifyEntrySpotActorCreated(
            nodeRid,
            actor,
            createRequest,
            createContext);
    }

    ZLinkSpot<?> spotFor(RoutingId spotRid) {
        return spotLifecycle.spotFor(spotRid);
    }

    private String meshNameForSpot(RoutingId spotRid) {
        return spotLocations.meshNameForSpot(
            spotRid,
            primaryNode.routingId(),
            spotLifecycle.hasUserSpot(spotRid));
    }

    Object spotSurfaceFor(RoutingId spotRid) {
        return spotLifecycle.spotSurfaceFor(spotRid);
    }

    private EntrySpotActivation entrySpotActivationFor(RoutingId spotRid) {
        return spotLifecycle.entrySpotActivationFor(spotRid);
    }

    public CompletionStage<Message> handleEntryActorTransferRoute(
        RoutingId sourceRoutingId,
        Message envelope) {
        EntrySpotActivation activation = entrySpotActivationFor(
            primaryNode.entrySpot().routingId());
        if (activation == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "Entry Spot activation is not available for actor transfer"));
        }
        return activation.handleInternalActorTransfer(sourceRoutingId, envelope);
    }

    Object localActorSpotSurface(ZLinkActor actor) {
        return actorSessions.spotSurface(
            actor,
            this::spotSurfaceFor,
            spotLifecycle::firstEntrySpot);
    }

    private SpotActorPacketHandlerRegistration resolveActorPacketHandler(
        String packetName,
        Object spotSurface,
        ZLinkScannedHandlerKind kind) {
        List<SpotActorPacketHandlerRegistration> handlers =
            actorHandlers.handlers(packetName);
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
        return spotHandlerInvoker.invokeActorSend(
            registration,
            spotSurface,
            actor,
            payload,
            "failed to invoke local session actor send handler");
    }

    private CompletionStage<Optional<Message>> invokeLocalActorRequestHandler(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload) {
        return spotHandlerInvoker.invokeActorRequest(
            registration,
            spotSurface,
            actor,
            payload,
            "failed to invoke local session actor request handler");
    }

    private void attachRouteMeshSpotBridges(Map<String, ZLinkInternalSpotNode> routeBridgeNodesByName) {
        if (channels == null || routeMeshChannels.isEmpty() || routeBridgeNodesByName.isEmpty()) {
            return;
        }
        for (ChannelRegistration routeMeshChannel : routeMeshChannels) {
            ZLinkInternalSpotNode routeBridgeNode =
                routeBridgeNodesByName.get(routeMeshChannel.name());
            if (routeBridgeNode == null && routeMeshChannel.routeRoutingId() != null) {
                for (ZLinkInternalSpotNode candidate : routeBridgeNodesByName.values()) {
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

    boolean dispatchSpotRouteBridgePacket(ZLinkBackendReceived received) {
        return channels != null && channels.dispatchSpotRouteBridgePacket(received);
    }


    void replyActorDispatchError(
        SpotDispatchLine dispatchLine,
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
        ZLinkBackendActorRef actorRef = new ZLinkBackendActorRef(
            headerPart.actor().nodeRid(),
            actorId,
            headerPart.actor().generation());
        if (isNoBindActorRequest(packetHeader, headerPart)) {
            dispatchLine.enqueueDispatch(() -> {
                try (Message frame = Message.from(frameBytes)) {
                    primaryNode.replyActorNoBind(
                        actorRef,
                        headerPart.sourceNodeRid(),
                        headerPart.sourceSessionRid(),
                        headerPart.requestId(),
                        headerPart.flags(),
                        List.of(frame));
                    return java.util.concurrent.CompletableFuture.completedFuture(null);
                } finally {
                    headerPart.close();
                }
            });
            return;
        }
        dispatchLine.enqueueDispatch(() -> sendActorBoundSessionWithRetry(
                primaryNode,
                actorRef,
                actorId,
                frameBytes,
                failureMessage)
            .whenComplete((ignored, sendError) -> headerPart.close()));
    }

    @Override
    DefaultSpotOutbound createContextOutbound(
        ZLinkBackendSpot backendSpot,
        RoutingId nodeRid) {
        return createSpotOutbound(
            backendSpot,
            spotLocations.publisherChannelName(nodeRid));
    }

    @Override
    ZLinkSpotTimerRegistry createTimerRegistry(
        RoutingId spotRid,
        ZLinkSpotTimerRegistry.Dispatch dispatch) {
        return new ZLinkSpotTimerRegistry(
            spotRid,
            timerExecutor,
            handlerFactory,
            suspendHandlerInvokers,
            eventDispatcher,
            primaryNodeSourceName,
            operation -> dispatch.enqueue(() -> {
                if (!dispatchErrors.flow().enabled(
                    systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.SENT)) {
                    return operation.get();
                }
                systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.State timerFlow =
                    systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.create(
                        systems.zlink.framework.configuration.ZLinkFlowOrigin.TIMER);
                try (systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.Scope ignored =
                    systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.enter(timerFlow)) {
                    return operation.get();
                }
            }));
    }

    @Override
    CompletionStage<Void> destroyActorFromEntry(
        RoutingId nodeRid,
        ZLinkActor actor) {
        return actorAdmissions.destroyFromEntry(nodeRid, actor);
    }

    @Override
    CompletionStage<Void> leaveActor(
        RoutingId nodeRid,
        ZLinkSpot<?> spot,
        ZLinkActor actor,
        RoutingId fallbackSpotRid) {
        if (actor == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor is required"));
        }
        try {
            if (actorAdmissionsRuntime().isRoutedTransferActor(actor)) {
                EntrySpotActivation entry = entrySpotActivationFor(
                    primaryNode.entrySpot().routingId());
                if (entry == null) {
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "Entry Spot activation is not available for actor leave"));
                }
                @SuppressWarnings({"rawtypes", "unchecked"})
                ZLinkEntrySpot rawEntrySpot = entry.entrySpot();
                return actorAdmissions.leaveRoutedActorToLocalEntry(
                    actor,
                    primaryNode.routingId(),
                    actorId -> ZLinkHandlerStages.fromStageSupplier(() ->
                        rawEntrySpot.onActorJoin(actorId, ZLinkMessage.empty())),
                    joinedActor -> notifySpotActorLifecycleAndSuppressBackendEvent(
                        rawEntrySpot,
                        joinedActor,
                        primaryNode.entrySpot().routingId(),
                        true));
            }
            EntrySpotActivation entry = entrySpotActivationFor(
                primaryNode.entrySpot().routingId());
            return actorAdmissions.leaveSpot(
                nodeByRid(nodeRid),
                actor,
                fallbackSpotRid,
                entry == null ? null : primaryNode.routingId(),
                defaultRequestTimeout);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private ZLinkActorRuntime actorAdmissionsRuntime() {
        return actorAdmissions.runtime();
    }

    @Override
    CompletionStage<Boolean> closeSpot(RoutingId spotRid) {
        return close(spotRid);
    }

    @Override
    <T> CompletionStage<T> runWithOutbound(
        DefaultSpotOutbound outbound,
        Supplier<CompletionStage<T>> operation) {
        return withCurrentOutbound(outbound, operation);
    }

    @Override
    CompletionStage<Void> runEntryDispatch(
        Object entryContext,
        Supplier<CompletionStage<Void>> operation) {
        try (systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationContext.Scope ignored =
                 systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationContext
                     .enterEntrySpotDispatch(entryContext)) {
            return operation.get();
        }
    }

    private DefaultSpotOutbound createSpotOutbound(
        ZLinkBackendSpot backendSpot,
        String publisherChannelName) {
        return new DefaultSpotOutbound(
            backendSpot,
            publisherChannelName,
            serializer,
            routedOutbound,
            directOutbound,
            publishers,
            channels,
            channels != null && !routeMeshChannels.isEmpty(),
            defaultRequestTimeout,
            () -> (SpotTransportAddressResolver) handlerFactory.create(
                SpotTransportAddressResolver.class));
    }


    CompletionStage<Void> sendActorBoundSessionWithRetry(
        ZLinkInternalSpotNode node,
        ZLinkBackendActorRef actor,
        String actorId,
        byte[] frameBytes,
        String failureMessage) {
        return boundSessionSender.send(
            node,
            actor,
            actorId,
            frameBytes,
            failureMessage);
    }

    private static void traceActorSession(String message) {
        if (!STREAM_TRACE) {
            return;
        }
        LOGGER.fine("[zlink-java-stream-trace] actor-session " + message);
    }

    static ActorMessageRead readActorMessage(
        List<ZLinkBackendActorReceived> actorMessages,
        int index,
        ZLinkBackendActorReceived pendingActorHeader) {
        boolean fromPendingHeader = pendingActorHeader != null;
        ZLinkBackendActorReceived headerPart = fromPendingHeader
            ? pendingActorHeader
            : actorMessages.get(index++);
        ZLinkBackendActorReceived bodyPart =
            headerPart.hasMore() && index < actorMessages.size()
                ? actorMessages.get(index++)
                : null;
        if (headerPart.hasMore() && bodyPart == null) {
            return new ActorMessageRead(
                false,
                fromPendingHeader,
                headerPart,
                null,
                fromPendingHeader ? pendingActorHeader : copyActorReceived(headerPart),
                index);
        }
        return new ActorMessageRead(
            true,
            fromPendingHeader,
            headerPart,
            bodyPart,
            null,
            index);
    }

    boolean dispatchActorControlPacket(
        ActorPacketFrames.Header packetHeader,
        ZLinkBackendActorReceived headerPart,
        ZLinkActor actor,
        boolean pendingHeader) {
        if (REMOTE_BOUND_SESSION_BIND_PACKET_NAME.equals(packetHeader.packetName())) {
            actorSessions.bindNativeSession(
                actor,
                primaryNode,
                headerPart.actor(),
                headerPart.sourceNodeRid(),
                headerPart.sourceSessionRid());
            closePendingActorHeader(headerPart, pendingHeader);
            return true;
        }
        if (ZLinkActorSpotRoutePackets.SESSION_DISCONNECTED_PACKET_NAME.equals(packetHeader.packetName())) {
            closePendingActorHeader(headerPart, pendingHeader);
            notifySpotActorDisconnected(actor).exceptionally(error -> null);
            return true;
        }
        return false;
    }

    Supplier<CompletionStage<Void>> actorLifecycleTransition(
        Object spotSurface,
        ZLinkBackendActorLifecycleEvent event,
        ZLinkBackendActorRef actorRef,
        ZLinkActor actor,
        RoutingId defaultSpotRid) {
        if (event.kind() == ZLinkBackendActorLifecycleEventKind.DISCONNECTED) {
            return () -> notifySpotActorDisconnected(actor);
        }
        RoutingId spotRid = actorLifecycleSpotRid(event, defaultSpotRid);
        if (shouldIgnoreJoinedOrLeftLifecycle(event, actorRef, actor, spotRid)) {
            return null;
        }
        if (event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT) {
            return () -> actorAdmissions.markLeft(actor)
                .thenCompose(ignored -> notifySpotActorLifecycle(spotSurface, actor, false))
                .whenComplete((ignored, error) ->
                    actorAdmissions.completeLeave(actor.actorId(), error));
        }
        return () -> actorAdmissions.markJoined(
                actor,
                actorRef,
                spotRid,
                spotSurfaceFor(spotRid) instanceof ZLinkSpot<?> spot ? spot : null)
            .thenCompose(ignored -> notifySpotActorLifecycle(spotSurface, actor, true))
            .whenComplete((ignored, error) -> {
                if (spotSurface instanceof ZLinkEntrySpot<?>) {
                    actorAdmissions.completeEntryJoin(actor.actorId(), error);
                }
            });
    }

    boolean shouldRunActorLifecycleInSpotDispatch(
        ZLinkBackendActorLifecycleEvent event,
        ZLinkActor actor) {
        return event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
            && actorAdmissions.isLeavePending(actor.actorId());
    }

    void dispatchLocalActorPacket(
        SpotDispatchLine dispatchLine,
        Object spotSurface,
        ZLinkActor actor,
        ActorPacketFrames.Header packetHeader,
        ZLinkBackendActorReceived headerPart,
        ZLinkBackendActorReceived bodyPart,
        boolean pendingHeader) {
        SpotActorPacketHandlerRegistration handler =
            resolveActorPacketHandler(
                packetHeader.packetName(),
                spotSurface,
                packetHeader.requestSeq().isPresent()
                    ? ZLinkScannedHandlerKind.ACTOR_REQUEST
                    : ZLinkScannedHandlerKind.ACTOR_SEND);
        if (handler == null) {
            boolean request = packetHeader.requestSeq().isPresent();
            reportSpotActorHandlerMissing(
                packetHeader,
                dispatchLine.spotRid(),
                actor.actorId(),
                headerPart.sourceNodeRid());
            if (request) {
                ZLinkBackendActorReceived headerCopy = pendingHeader
                    ? headerPart
                    : copyActorReceived(headerPart);
                replyActorDispatchError(
                    dispatchLine,
                    packetHeader,
                    headerCopy,
                    actor.actorId(),
                    new ZLinkConfigurationException(
                        "No SPOT actor request handler is registered for '"
                            + packetHeader.packetName() + "'."),
                    "actor dispatch error reply failed");
            } else {
                closePendingActorHeader(headerPart, pendingHeader);
            }
            return;
        }
        if (packetHeader.requestSeq().isPresent()
            != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
            closePendingActorHeader(headerPart, pendingHeader);
            return;
        }
        if (!handler.actorType().isInstance(actor)) {
            closePendingActorHeader(headerPart, pendingHeader);
            return;
        }
        ZLinkBackendActorReceived headerCopy = pendingHeader
            ? headerPart
            : copyActorReceived(headerPart);
        Message payloadCopy = bodyPart == null
            ? Message.from(new byte[0])
            : Message.from(bodyPart.message());
        CompletionStage<Optional<Message>> captured = null;
        if (actorSessions.isMoving(actor)) {
            ZLinkActorReplyRoute replyRoute = isNoBindActorRequest(packetHeader, headerCopy)
                ? new ZLinkActorReplyRoute(
                    headerCopy.actor(),
                    headerCopy.sourceNodeRid(),
                    headerCopy.sourceSessionRid(),
                    headerCopy.requestId(),
                    headerCopy.flags())
                : null;
            captured = actorSessions.captureMoving(
                actor, packetHeader.toStreamHeader(), payloadCopy, replyRoute);
        }
        if (captured != null) {
            captured.thenCompose(reply -> replyCapturedActorPacket(
                    actor, packetHeader, headerCopy, reply))
                .whenComplete((ignored, error) -> {
                    payloadCopy.close();
                    headerCopy.close();
                });
            return;
        }
        actorSessions.dispatch(
            actor,
            () -> {
                if (packetHeader.flowId().isEmpty()) {
                    return dispatchActorPacketToHandler(
                        dispatchLine.dispatchOutbound(), handler, spotSurface, actor,
                        packetHeader, headerCopy, payloadCopy,
                        "actor bound session reply failed");
                }
                var state = new systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.State(
                    packetHeader.flowId().orElseThrow(), packetHeader.flowOrigin().orElseThrow());
                try (var ignored = systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.enter(state)) {
                    return dispatchActorPacketToHandler(
                        dispatchLine.dispatchOutbound(), handler, spotSurface, actor,
                        packetHeader, headerCopy, payloadCopy,
                        "actor bound session reply failed");
                }
            });
    }

    private CompletionStage<Void> replyCapturedActorPacket(
        ZLinkActor actor,
        ActorPacketFrames.Header packetHeader,
        ZLinkBackendActorReceived headerPart,
        Optional<Message> reply) {
        if (reply.isEmpty()) {
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        byte[] frameBytes;
        try (Message payload = reply.get();
             Message frame = ActorPacketFrames.encodeReply(packetHeader, payload)) {
            frameBytes = frame.toByteArray();
        }
        if (isNoBindActorRequest(packetHeader, headerPart)) {
            try (Message frame = Message.from(frameBytes)) {
                primaryNode.replyActorNoBind(
                    headerPart.actor(),
                    headerPart.sourceNodeRid(),
                    headerPart.sourceSessionRid(),
                    headerPart.requestId(),
                    headerPart.flags(),
                    List.of(frame));
            }
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        return sendActorBoundSessionWithRetry(
            primaryNode,
            headerPart.actor(),
            actor.actorId(),
            frameBytes,
            "actor handoff reply failed");
    }

    static void closePendingActorHeader(
        ZLinkBackendActorReceived headerPart,
        boolean pendingHeader) {
        if (pendingHeader) {
            headerPart.close();
        }
    }

    static ZLinkBackendActorReceived copyActorReceived(
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

    static boolean isNoBindActorRequest(
        ActorPacketFrames.Header packetHeader,
        ZLinkBackendActorReceived headerPart) {
        return packetHeader.requestSeq().isPresent()
            && headerPart.requestId() != 0
            && (headerPart.flags() & ACTOR_RECV_INFO_NO_BIND) != 0;
    }

    void traceMessageFlow(
        ZLinkMessageFlowOutcome outcome,
        ZLinkDispatchErrorSurface surface,
        ZLinkDispatchMessageKind messageKind,
        String packetName,
        String channelName,
        String topic,
        String correlationId,
        String sourceRid,
        String spotRid,
        String actorId) {
        if (!dispatchErrors.flow().enabled(outcome)) {
            return;
        }
        dispatchErrors.flow().trace(new ZLinkMessageFlowEvent(
            outcome,
            surface,
            messageKind,
            packetName,
            channelName,
            topic,
            correlationId,
            sourceRid,
            spotRid,
            actorId,
            null));
    }

    private void reportDispatchError(DispatchFailureReport failure) {
        dispatchErrors.report(new ZLinkDispatchFailure(
            failure.surface,
            failure.messageKind,
            failure.reason,
            failure.action,
            failure.packetName == null || failure.packetName.isBlank() ? null : failure.packetName,
            failure.channelName,
            failure.topic,
            failure.spotRid == null ? null : failure.spotRid.toString(),
            failure.actorId,
            failure.sourceRid == null ? null : failure.sourceRid.toString(),
            failure.correlationId,
            errorType(failure.error),
            errorMessage(failure.error)));
    }

    void reportSpotRouteSendDropped(
        ZLinkBackendReceived received,
        String packetName,
        RoutingId spotRid) {
        reportDispatchError(DispatchFailureReport.of(
                ZLinkDispatchErrorSurface.SPOT_ROUTE,
                ZLinkDispatchMessageKind.SEND,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                ZLinkDispatchErrorAction.DROP)
            .packetName(packetName)
            .spotRid(spotRid)
            .sourceRid(received.routingId().orElse(null)));
    }

    void reportSpotSubscriptionDropped(
        String topic,
        String packetName,
        RoutingId spotRid,
        ZLinkDispatchErrorReason reason) {
        reportDispatchError(DispatchFailureReport.of(
                ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                ZLinkDispatchMessageKind.PUBLISH,
                reason,
                ZLinkDispatchErrorAction.DROP)
            .packetName(packetName)
            .topic(topic)
            .spotRid(spotRid));
    }

    void reportSpotActorHandlerMissing(
        ActorPacketFrames.Header packetHeader,
        RoutingId spotRid,
        String actorId,
        RoutingId sourceRid) {
        boolean request = packetHeader.requestSeq().isPresent();
        reportDispatchError(DispatchFailureReport.of(
                ZLinkDispatchErrorSurface.SPOT_ACTOR,
                request
                    ? ZLinkDispatchMessageKind.ACTOR_REQUEST
                    : ZLinkDispatchMessageKind.ACTOR_SEND,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                request
                    ? ZLinkDispatchErrorAction.REPLY_ERROR
                    : ZLinkDispatchErrorAction.DROP)
            .packetName(packetHeader.packetName())
            .spotRid(spotRid)
            .actorId(actorId)
            .sourceRid(sourceRid)
            .correlationId(packetHeader.requestSeq().map(Object::toString).orElse(null)));
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

    void replySpotRouteDispatchError(
        ZLinkBackendReceived received,
        String packetName,
        RoutingId spotRid,
        ZLinkDispatchErrorReason reason,
        Throwable error) {
        Throwable cause = unwrapCompletion(error);
        List<Message> reply = ZLinkFrameworkErrorReply.create(errorText(reason, packetName, cause));
        try {
            received.reply(reply);
        } catch (RuntimeException ignored) {
        } finally {
            reply.forEach(Message::close);
        }
        reportDispatchError(DispatchFailureReport.of(
                ZLinkDispatchErrorSurface.SPOT_ROUTE,
                ZLinkDispatchMessageKind.REQUEST,
                reason,
                ZLinkDispatchErrorAction.REPLY_ERROR)
            .packetName(packetName)
            .spotRid(spotRid)
            .sourceRid(received.routingId().orElse(null))
            .correlationId(received.requestSeq().map(Object::toString).orElse(null))
            .error(cause));
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

    static ParsedPacket parsePacket(List<Message> parts) {
        if (parts.size() >= 2) {
            return new ParsedPacket(parts.get(0).toUtf8String(), parts.get(1));
        }
        return new ParsedPacket("", parts.get(0));
    }

    static boolean isProbeFrame(List<Message> parts) {
        return parts.isEmpty() || parts.get(0).size() == 0;
    }

    static void traceSpotRouteInbound(
        String phase,
        ZLinkBackendSpot backendSpot,
        ZLinkBackendReceived received) {
        if (!STREAM_TRACE) {
            return;
        }
        LOGGER.fine("[zlink-java-stream-trace] spot-route " + phase
            + " localSpot=" + backendSpot.routingId()
            + " sourceRid=" + received.routingId().map(Object::toString).orElse(null)
            + " sourceSpot=" + received.spotRid().map(Object::toString).orElse(null)
            + " requestSeq=" + received.requestSeq().map(Object::toString).orElse(null)
            + " result=" + received.result()
            + " parts=" + describeTraceParts(received.parts()));
    }

    static void traceSpotRouteDispatch(
        String phase,
        ZLinkBackendSpot backendSpot,
        ZLinkBackendReceived received,
        ParsedPacket packet) {
        if (!STREAM_TRACE) {
            return;
        }
        LOGGER.fine("[zlink-java-stream-trace] spot-route " + phase
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


    void awaitClosing(CompletionStage<Void> closingStage) {
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
