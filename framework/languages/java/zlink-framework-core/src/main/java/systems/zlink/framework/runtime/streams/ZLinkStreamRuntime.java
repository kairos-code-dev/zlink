package systems.zlink.framework.runtime.streams;

import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

import systems.zlink.framework.runtime.backend.*;

import java.util.ArrayList;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.function.Predicate;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.logging.Logger;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics;
import systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext;
import systems.zlink.framework.configuration.ZLinkFlowOrigin;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamCompressionCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamDiagnostic;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;
import systems.zlink.framework.streams.ZLinkStreamSessionError;

public final class ZLinkStreamRuntime implements AutoCloseable {
    private static final Logger LOGGER = Logger.getLogger(ZLinkStreamRuntime.class.getName());
    private static final String HEARTBEAT_PING_NAME = "$zlink.heartbeat.ping";
    private static final String HEARTBEAT_PONG_NAME = "$zlink.heartbeat.pong";
    private static final long HEARTBEAT_TIMEOUT_NANOS = TimeUnit.SECONDS.toNanos(5);
    private static final long IDLE_TIMEOUT_NANOS = TimeUnit.SECONDS.toNanos(30);
    private static final boolean STREAM_TRACE =
        "1".equals(System.getenv("ZLINK_JAVA_STREAM_TRACE"));
    private final ZLinkBackendContext context;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actors;
    private final ZLinkHandlerActivator handlerFactory;
    private final Executor handlerExecutor;
    private final systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer flow;
    private final List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers;
    private final ZLinkStreamCodec defaultCodec;
    private final ZLinkStreamCompressionCodec compressionCodec;
    private final Predicate<RoutingId> sessionRelayRouteReady;
    private final ZLinkSessionActorsRuntime.LocalActorDispatcher localActorDispatcher;
    private final List<ZLinkBackendStreamSocket> streams = new ArrayList<>();
    private final Map<String, ZLinkBackendStreamSocket> streamsByName = new HashMap<>();
    private final Map<String, Boolean> streamSessionRelayAttached = new HashMap<>();
    private final Map<String, ZLinkInternalSpotNode> streamSessionRelaySpotNodes = new HashMap<>();
    private final Map<String, SessionState> sessions = new HashMap<>();
    private final ScheduledExecutorService livenessExecutor;
    private volatile boolean draining;
    private systems.zlink.framework.actors.ZLinkActorDirectory actorDirectory;

    public void setActorDirectory(
        systems.zlink.framework.actors.ZLinkActorDirectory actorDirectory) {
        this.actorDirectory = actorDirectory;
    }

    public ZLinkStreamRuntime(
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        Map<String, ZLinkInternalSpotNode> spotNodes,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actors,
        ZLinkHandlerActivator handlerFactory) {
        this(
            backendFactory,
            adapterOptions,
            registration,
            spotNodes,
            serializer,
            actors,
            handlerFactory,
            ignored -> true,
            null);
    }

    public ZLinkStreamRuntime(
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        Map<String, ZLinkInternalSpotNode> spotNodes,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actors,
        ZLinkHandlerActivator handlerFactory,
        Predicate<RoutingId> sessionRelayRouteReady,
        ZLinkSpotRuntime spots) {
        this(
            backendFactory,
            adapterOptions,
            registration,
            spotNodes,
            serializer,
            actors,
            handlerFactory,
            sessionRelayRouteReady,
            spots,
            null);
    }

    public ZLinkStreamRuntime(
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        Map<String, ZLinkInternalSpotNode> spotNodes,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actors,
        ZLinkHandlerActivator handlerFactory,
        Predicate<RoutingId> sessionRelayRouteReady,
        ZLinkSpotRuntime spots,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        if (registration.streamNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one stream node is required");
        }
        this.serializer = serializer;
        this.actors = actors;
        this.handlerFactory = handlerFactory;
        this.handlerExecutor = ZLinkFlowContext.propagating(java.util.Objects.requireNonNull(
            registration.handlerExecutor(),
            "handlerExecutor"));
        this.flow = new systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer(
            registration.dispatchOptions(), handlerFactory, this.handlerExecutor, eventDispatcher);
        this.suspendHandlerInvokers = registration.suspendHandlerInvokers();
        this.defaultCodec = defaultCodec(registration);
        this.compressionCodec = registration.streamCompressionCodec();
        this.sessionRelayRouteReady =
            sessionRelayRouteReady == null ? ignored -> true : sessionRelayRouteReady;
        this.localActorDispatcher = spots == null ? null : spots::dispatchLocalSessionActor;
        this.livenessExecutor = Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-stream-liveness");
            thread.setDaemon(true);
            return thread;
        });
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkStreamBackendAdapter streamAdapter =
            backendFactory.createStreamAdapter(adapterOptions);
        this.context = channelAdapter.createContext();
        for (StreamNodeRegistration streamNode : registration.streamNodes()) {
            ZLinkBackendStreamSocket stream =
                streamAdapter.createStreamSocket(context);
            if (streamNode.tlsServer() != null) {
                stream.setTlsServer(
                    streamNode.tlsServer().certificatePath(),
                    streamNode.tlsServer().keyPath(),
                    streamNode.tlsServer().requireClientCertificate());
            }
            for (String bindEndpoint : streamNode.bindEndpoints()) {
                trace("stream-node bind node=" + streamNode.name() + " endpoint=" + bindEndpoint);
                stream.bind(bindEndpoint);
            }
            stream.onPacket((routingId, header, payload) ->
                dispatchToSession(streamNode, routingId, header, payload));
            stream.onTransportError((routingId, nativeCode, message) ->
                reportTransportError(streamNode, routingId, nativeCode, message));
            ZLinkInternalSpotNode spotNode = resolveSessionRelayNode(spotNodes);
            streams.add(stream);
            streamsByName.put(streamNode.name(), stream);
            streamSessionRelayAttached.put(
                streamNode.name(),
                spotNode != null);
            if (spotNode != null) {
                streamSessionRelaySpotNodes.put(streamNode.name(), spotNode);
            }
        }
        livenessExecutor.scheduleAtFixedRate(
            this::checkSessionLiveness, 1L, 1L, TimeUnit.SECONDS);
    }

    private static ZLinkInternalSpotNode resolveSessionRelayNode(
        Map<String, ZLinkInternalSpotNode> spotNodes) {
        return spotNodes.values().stream().findFirst().orElse(null);
    }

    public ZLinkSessionActorsRuntime sessionActors(
        String streamNodeName,
        RoutingId sessionRid,
        ZLinkActorRuntime actors) {
        ZLinkBackendStreamSocket stream = streamsByName.get(streamNodeName);
        if (stream == null) {
            throw new ZLinkConfigurationException(
                "stream node is not running: " + streamNodeName);
        }
        return new ZLinkSessionActorsRuntime(
            streamSessionRelaySpotNodes.get(streamNodeName),
            stream,
            sessionRid,
            actors,
            serializer,
            sessionRelayRouteReady,
            localActorDispatcher,
            streamSessionRelayAttached.getOrDefault(streamNodeName, false),
            defaultCodec,
            flow).actorDirectory(actorDirectory);
    }

    private void dispatchToSession(
        StreamNodeRegistration streamNode,
        RoutingId routingId,
        Message header,
        Message payload) {
        ZLinkBackendStreamSocket stream = streamsByName.get(streamNode.name());
        if (isStreamNotification(header, payload)) {
            trace("stream-node notification node=" + streamNode.name()
                + " routingId=" + routingId);
            dispatchStreamNotification(streamNode, stream, routingId);
            return;
        }
        ZLinkStreamHeader streamHeader =
            ZLinkStreamHeaderCodec.decodeOrPlain(header.toByteArray());
        ZLinkFlowContext.State incomingFlow = streamHeader.flowId().isPresent()
            ? new ZLinkFlowContext.State(streamHeader.flowId().orElseThrow(),
                streamHeader.flowOrigin().orElseThrow())
            : (flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.RECEIVED)
                ? ZLinkFlowContext.create(ZLinkFlowOrigin.INBOUND)
                : null);
        if (streamHeader.kind() != ZLinkStreamMessageKind.CONTROL
            && incomingFlow != null
            && streamHeader.flowId().isEmpty()) {
            streamHeader = streamHeader.withFlow(incomingFlow.flowId(), incomingFlow.origin());
        }
        final ZLinkStreamHeader dispatchHeader = streamHeader;
        trace("stream-node frame-received node=" + streamNode.name()
            + " routingId=" + routingId
            + " kind=" + streamHeader.kind()
            + " name=" + streamHeader.packetName()
            + " requestSeq=" + streamHeader.requestSequence().orElse(null)
            + " correlation=" + streamHeader.correlationId().orElse(null)
            + " payloadBytes=" + payload.toByteArray().length);
        if (streamHeader.kind() == ZLinkStreamMessageKind.CONTROL) {
            dispatchControl(streamNode, stream, routingId, streamHeader, payload);
            return;
        }
        if (draining) {
            sendSessionClosing(stream, routingId);
            return;
        }
        SessionState state = getOrCreateSessionState(streamNode, stream, routingId);
        state.markApplicationReceived();
        if (flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.RECEIVED)) {
            String corr = ZLinkStreamCorrelations.forTrace(dispatchHeader);
            flow.trace(new systems.zlink.framework.configuration.ZLinkMessageFlowEvent(
                systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.RECEIVED,
                systems.zlink.framework.configuration.ZLinkDispatchErrorSurface.STREAM_SESSION,
                dispatchHeader.requestSequence().isPresent()
                    ? systems.zlink.framework.configuration.ZLinkDispatchMessageKind.REQUEST
                    : systems.zlink.framework.configuration.ZLinkDispatchMessageKind.SEND,
                dispatchHeader.packetName(), null, null, corr, null, null, null, null,
                null, null, null, null,
                incomingFlow == null ? null : incomingFlow.flowId(),
                incomingFlow == null ? null : incomingFlow.origin()));
        }
        Message payloadCopy = Message.from(ZLinkStreamPayloadCodec.decode(
            dispatchHeader,
            payload,
            compressionCodec));
        ZLinkMessage sessionPayload = ZLinkMessage.fromEncoded(
            ZLinkMessagePayloads.encoded(payloadCopy),
            serializer);
        payloadCopy.close();
        trace("stream-node dispatch-enqueue node=" + streamNode.name()
            + " routingId=" + routingId
            + " name=" + dispatchHeader.packetName()
            + " requestSeq=" + dispatchHeader.requestSequence().orElse(null)
            + " correlation=" + dispatchHeader.correlationId().orElse(null));
        state.queue().enqueue(() -> {
            if (incomingFlow == null) {
                return executeHandler(() ->
                    state.context().dispatchStage(dispatchHeader, sessionPayload, state.session()));
            }
            try (ZLinkFlowContext.Scope ignored = ZLinkFlowContext.enter(incomingFlow)) {
                return executeHandler(() ->
                    state.context().dispatchStage(dispatchHeader, sessionPayload, state.session()));
            }
        });
    }

    private void dispatchControl(
        StreamNodeRegistration streamNode,
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        ZLinkStreamHeader header,
        Message payload) {
        if (payload.toByteArray().length != 0) {
            throw new IllegalArgumentException("STREAM control packet payload must be empty");
        }
        if (HEARTBEAT_PONG_NAME.equals(header.packetName())) {
            SessionState state;
            synchronized (sessions) {
                state = sessions.get(sessionKey(streamNode, routingId));
            }
            if (state != null) {
                state.markHeartbeatPong();
            }
            return;
        }
        if (!HEARTBEAT_PING_NAME.equals(header.packetName())) {
            throw new IllegalArgumentException("unknown STREAM control packet: " + header.packetName());
        }
        Message empty = Message.from(new byte[0]);
        try {
            ZLinkStreamHeader pong = new ZLinkStreamHeader(
                ZLinkStreamMessageKind.CONTROL,
                ZLinkStreamCodec.RAW,
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                Optional.empty(),
                HEARTBEAT_PONG_NAME,
                Map.of(),
                Optional.empty());
            if (!stream.send(routingId, pong, List.of(empty), SendFlags.DONT_WAIT)) {
                throw new ZLinkConfigurationException("heartbeat pong send failed: " + routingId);
            }
        } finally {
            empty.close();
        }
    }

    private void dispatchStreamNotification(
        StreamNodeRegistration streamNode,
        ZLinkBackendStreamSocket stream,
        RoutingId routingId) {
        String key = sessionKey(streamNode, routingId);
        SessionState state;
        synchronized (sessions) {
            state = sessions.get(key);
        }
        if (state == null) {
            if (draining) {
                sendSessionClosing(stream, routingId);
                return;
            }
            getOrCreateSessionState(streamNode, stream, routingId);
            return;
        }
        synchronized (sessions) {
            sessions.remove(key);
        }
        recordSessionClosed("client_close");
        state.queue().enqueue(() -> executeHandler(() -> disconnectSessionStage(state)));
    }

    private static boolean isStreamNotification(Message header, Message payload) {
        return header.toByteArray().length == 0 && payload.toByteArray().length == 0;
    }

    private void reportTransportError(
        StreamNodeRegistration streamNode,
        RoutingId routingId,
        int nativeCode,
        String message) {
        String key = sessionKey(streamNode, routingId);
        SessionState state;
        synchronized (sessions) {
            state = sessions.get(key);
        }
        if (state == null) {
            return;
        }
        synchronized (sessions) {
            sessions.remove(key);
        }
        recordSessionClosed(nativeCode == 0 ? "transport_error" : "protocol_error");
        if (nativeCode == 0 && "DISCONNECTED".equals(message)) {
            state.queue().enqueue(() -> executeHandler(() -> disconnectSessionStage(state)));
            return;
        }
        state.queue().enqueue(() -> executeHandler(() ->
            transportErrorDisconnectSessionStage(state, nativeCode, message)));
    }

    private SessionState getOrCreateSessionState(
        StreamNodeRegistration streamNode,
        ZLinkBackendStreamSocket stream,
        RoutingId routingId) {
        String key = sessionKey(streamNode, routingId);
        SessionState state;
        boolean created = false;
        synchronized (sessions) {
            state = sessions.get(key);
            if (state == null) {
                state = createSessionState(streamNode, stream, routingId);
                sessions.put(key, state);
                created = true;
            }
        }
        if (created) {
            ZLinkRuntimeMetrics.add("zlink.stream.connections.active", 1, Map.of());
            ZLinkRuntimeMetrics.increment("zlink.stream.connections.opened", Map.of());
            dispatchConnected(state);
        }
        return state;
    }

    private static void recordSessionClosed(String reason) {
        ZLinkRuntimeMetrics.add("zlink.stream.connections.active", -1, Map.of());
        ZLinkRuntimeMetrics.increment("zlink.stream.connections.closed",
            Map.of("close_reason", reason));
    }

    private SessionState createSessionState(
        StreamNodeRegistration streamNode,
        ZLinkBackendStreamSocket stream,
        RoutingId routingId) {
        ZLinkStreamSessionContextState context = new ZLinkStreamSessionContextState(
            streamNode.name(),
            stream,
            routingId,
            actors == null && !streamSessionRelayAttached.getOrDefault(streamNode.name(), false)
                ? null
                : new ZLinkSessionActorsRuntime(
                    streamSessionRelaySpotNodes.get(streamNode.name()),
                    stream,
                    routingId,
                    actors,
                    serializer,
                    sessionRelayRouteReady,
                    localActorDispatcher,
                    streamSessionRelayAttached.getOrDefault(streamNode.name(), false),
                    defaultCodec,
                    flow).actorDirectory(actorDirectory),
            serializer,
            defaultCodec,
            compressionCodec,
            flow,
            () -> {
                sendSessionClosing(stream, routingId);
                return CompletableFuture.completedFuture(null);
            });
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> dispatcher =
            new ZLinkSessionPacketDispatcherRuntime<>(
                streamNode.sessionPacketHandlers(),
                handlerFactory,
                serializer,
                handlerExecutor,
                suspendHandlerInvokers);
        ZLinkHandlerActivator.MutableServices sessionFactory =
            ZLinkHandlerActivator.services(handlerFactory)
                .add(ZLinkSessionContext.class, context)
                .add(ZLinkSessionPacketDispatcher.class, dispatcher);
        if (actors != null) {
            sessionFactory.add(ZLinkActorManager.class, actors);
        }
        Object createdSession = sessionFactory.create(streamNode.sessionType());
        if (!(createdSession instanceof ZLinkSession session)) {
            throw new ZLinkConfigurationException(
                "stream session type must implement ZLinkSession: "
                    + streamNode.sessionType().getName());
        }
        if (session.context() != context) {
            throw new ZLinkConfigurationException(
                "stream session must expose the context provided by the runtime: "
                    + streamNode.sessionType().getName());
        }
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        return new SessionState(session, queue, context, stream, routingId);
    }

    private void dispatchConnected(SessionState state) {
        state.queue().enqueue(() -> executeHandler(() ->
            ZLinkHandlerStages.fromStageSupplier(state.session()::onConnected)));
    }

    private static String sessionKey(StreamNodeRegistration streamNode, RoutingId routingId) {
        return streamNode.name() + ":" + routingId.toString();
    }

    @Override
    public void close() {
        closeAsync();
    }

    public CompletionStage<Void> closeAsync() {
        List<SessionState> activeSessions;
        synchronized (sessions) {
            activeSessions = List.copyOf(sessions.values());
        }
        return CompletableFuture.allOf(activeSessions.stream()
            .map(state -> state.queue().enqueue(() -> executeHandler(() -> disconnectSessionStage(state)))
                .toCompletableFuture())
            .toArray(CompletableFuture[]::new))
            .handle((ignored, failure) -> {
                finishClose(activeSessions);
                return null;
            });
    }

    private void finishClose(List<SessionState> activeSessions) {
        synchronized (sessions) {
            sessions.clear();
        }
        String closeReason = draining ? "server_drain" : "transport_error";
        for (int index = 0; index < activeSessions.size(); index++) {
            recordSessionClosed(closeReason);
        }
        for (ZLinkBackendStreamSocket stream : streams) {
            stream.close();
        }
        livenessExecutor.shutdownNow();
        context.close();
    }

    public void beginDrain() {
        draining = true;
    }

    public CompletionStage<Void> notifyServerDrain() {
        List<Map.Entry<String, SessionState>> active;
        synchronized (sessions) {
            active = List.copyOf(sessions.entrySet());
        }
        for (Map.Entry<String, SessionState> entry : active) {
            SessionState state = entry.getValue();
            sendSessionClosing(state.stream(), state.routingId());
        }
        return CompletableFuture.completedFuture(null);
    }

    private static void sendSessionClosing(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId) {
        sendSessionClosing(stream, routingId, ZLinkSessionClosingControl.SERVER_DRAIN, "server drain");
    }

    private static void sendSessionClosing(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        int reason,
        String diagnostic) {
        Message payload = Message.from(ZLinkSessionClosingControl.encode(reason, diagnostic));
        try {
            ZLinkStreamHeader header = new ZLinkStreamHeader(
                ZLinkStreamMessageKind.CONTROL,
                ZLinkStreamCodec.RAW,
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                Optional.empty(),
                ZLinkSessionClosingControl.NAME,
                Map.of(),
                Optional.empty());
            if (!stream.send(routingId, header, List.of(payload), SendFlags.NONE)) {
                throw new ZLinkConfigurationException("session-closing send failed: " + routingId);
            }
        } finally {
            payload.close();
        }
    }

    private void checkSessionLiveness() {
        long now = System.nanoTime();
        List<Map.Entry<String, SessionState>> snapshot;
        synchronized (sessions) {
            snapshot = List.copyOf(sessions.entrySet());
        }
        for (Map.Entry<String, SessionState> entry : snapshot) {
            SessionState state = entry.getValue();
            int reason = now - state.lastHeartbeatPongNanos() >= HEARTBEAT_TIMEOUT_NANOS
                ? ZLinkSessionClosingControl.HEARTBEAT_TIMEOUT
                : now - state.lastApplicationNanos() >= IDLE_TIMEOUT_NANOS
                    ? ZLinkSessionClosingControl.IDLE_TIMEOUT
                    : 0;
            if (reason == 0) {
                sendHeartbeatPing(state);
                continue;
            }
            synchronized (sessions) {
                if (!sessions.remove(entry.getKey(), state)) {
                    continue;
                }
            }
            sendSessionClosing(
                state.stream(), state.routingId(), reason,
                reason == ZLinkSessionClosingControl.HEARTBEAT_TIMEOUT
                    ? "heartbeat timeout"
                    : "idle timeout");
            state.queue().enqueue(() -> executeHandler(() -> disconnectSessionStage(state)));
        }
    }

    private static void sendHeartbeatPing(SessionState state) {
        Message empty = Message.from(new byte[0]);
        try {
            ZLinkStreamHeader ping = new ZLinkStreamHeader(
                ZLinkStreamMessageKind.CONTROL,
                ZLinkStreamCodec.RAW,
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                Optional.empty(),
                HEARTBEAT_PING_NAME,
                Map.of(),
                Optional.empty());
            state.stream().send(state.routingId(), ping, List.of(empty), SendFlags.DONT_WAIT);
        } finally {
            empty.close();
        }
    }

    private <T> CompletionStage<T> executeHandler(
        java.util.function.Supplier<CompletionStage<T>> operation) {
        CompletableFuture<CompletionStage<T>> entered = new CompletableFuture<>();
        ZLinkFlowContext.State capturedFlow = ZLinkFlowContext.current();
        try {
            handlerExecutor.execute(() -> {
                try (ZLinkFlowContext.Scope ignored = capturedFlow == null
                    ? () -> { }
                    : ZLinkFlowContext.enter(capturedFlow)) {
                    entered.complete(java.util.Objects.requireNonNull(
                        operation.get(), "handler result"));
                } catch (RuntimeException ex) {
                    entered.completeExceptionally(ex);
                }
            });
        } catch (RuntimeException ex) {
            entered.completeExceptionally(ex);
        }
        // The serial queue releases an automatic turn as soon as this method returns.
        // Wait only until the configured handler executor has entered the handler and
        // obtained its stage; the stage itself remains incomplete and does not hold the turn.
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue
            .deferCurrentReleaseUntil(entered)
            .thenCompose(java.util.function.Function.identity());
    }

    private CompletionStage<Void> disconnectSessionStage(SessionState state) {
        return notifyBoundActorsDisconnectedBestEffort(state)
            .thenCompose(ignored -> ZLinkHandlerStages.fromStageSupplier(state.session()::onDisconnected));
    }

    private CompletionStage<Void> transportErrorDisconnectSessionStage(
        SessionState state,
        int nativeCode,
        String message) {
        return ZLinkHandlerStages.fromStageSupplier(() -> state.session().onError(new ZLinkStreamError(
                ZLinkStreamSessionError.TRANSPORT_ERROR,
                Optional.of(new ZLinkStreamDiagnostic(nativeCode, message)))))
            .thenCompose(ignored -> notifyBoundActorsDisconnectedBestEffort(state))
            .thenCompose(ignored -> ZLinkHandlerStages.fromStageSupplier(state.session()::onDisconnected));
    }

    private CompletionStage<Void> notifyBoundActorsDisconnectedBestEffort(SessionState state) {
        return state.context().notifyBoundActorsDisconnected()
            .handle((ignored, error) -> (Void) null);
    }

    private static final class SessionState {
        private final ZLinkSession session;
        private final ZLinkAsyncSerialQueue queue;
        private final ZLinkStreamSessionContextState context;
        private final ZLinkBackendStreamSocket stream;
        private final RoutingId routingId;
        private volatile long lastApplicationNanos = System.nanoTime();
        private volatile long lastHeartbeatPongNanos = System.nanoTime();

        SessionState(
            ZLinkSession session,
            ZLinkAsyncSerialQueue queue,
            ZLinkStreamSessionContextState context,
            ZLinkBackendStreamSocket stream,
            RoutingId routingId) {
            this.session = session;
            this.queue = queue;
            this.context = context;
            this.stream = stream;
            this.routingId = routingId;
        }

        ZLinkSession session() { return session; }
        ZLinkAsyncSerialQueue queue() { return queue; }
        ZLinkStreamSessionContextState context() { return context; }
        ZLinkBackendStreamSocket stream() { return stream; }
        RoutingId routingId() { return routingId; }
        long lastApplicationNanos() { return lastApplicationNanos; }
        long lastHeartbeatPongNanos() { return lastHeartbeatPongNanos; }
        void markApplicationReceived() { lastApplicationNanos = System.nanoTime(); }
        void markHeartbeatPong() { lastHeartbeatPongNanos = System.nanoTime(); }
    }

    private static ZLinkStreamCodec defaultCodec(ZLinkFrameworkRegistration registration) {
        return registration.codecs().streamCodecForCustomSerializer()
            .orElse(ZLinkStreamCodec.JSON);
    }

    static void trace(String message) {
        if (STREAM_TRACE) {
            LOGGER.fine("[zlink-java-stream-trace] " + message);
        }
    }
}
