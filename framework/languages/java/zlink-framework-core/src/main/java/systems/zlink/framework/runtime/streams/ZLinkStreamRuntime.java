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
        this.handlerExecutor = java.util.Objects.requireNonNull(
            registration.handlerExecutor(),
            "handlerExecutor");
        this.flow = new systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer(
            registration.dispatchOptions(), handlerFactory, this.handlerExecutor, eventDispatcher);
        this.suspendHandlerInvokers = registration.suspendHandlerInvokers();
        this.defaultCodec = defaultCodec(registration);
        this.compressionCodec = registration.streamCompressionCodec();
        this.sessionRelayRouteReady =
            sessionRelayRouteReady == null ? ignored -> true : sessionRelayRouteReady;
        this.localActorDispatcher = spots == null ? null : spots::dispatchLocalSessionActor;
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
            defaultCodec);
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
        trace("stream-node frame-received node=" + streamNode.name()
            + " routingId=" + routingId
            + " kind=" + streamHeader.kind()
            + " name=" + streamHeader.packetName()
            + " requestSeq=" + streamHeader.requestSequence().orElse(null)
            + " correlation=" + streamHeader.correlationId().orElse(null)
            + " payloadBytes=" + payload.toByteArray().length);
        if (streamHeader.kind() == ZLinkStreamMessageKind.CONTROL) {
            dispatchControl(stream, routingId, streamHeader, payload);
            return;
        }
        SessionState state = getOrCreateSessionState(streamNode, stream, routingId);
        if (flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.RECEIVED)) {
            String corr = streamHeader.correlationId()
                .orElseGet(() -> streamHeader.requestSequence().map(String::valueOf).orElse(null));
            flow.trace(new systems.zlink.framework.configuration.ZLinkMessageFlowEvent(
                systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.RECEIVED,
                systems.zlink.framework.configuration.ZLinkDispatchErrorSurface.STREAM_SESSION,
                streamHeader.requestSequence().isPresent()
                    ? systems.zlink.framework.configuration.ZLinkDispatchMessageKind.REQUEST
                    : systems.zlink.framework.configuration.ZLinkDispatchMessageKind.SEND,
                streamHeader.packetName(), null, null, corr, null, null, null, null));
        }
        Message payloadCopy = Message.from(ZLinkStreamPayloadCodec.decode(
            streamHeader,
            payload,
            compressionCodec));
        ZLinkMessage sessionPayload = ZLinkMessage.fromEncoded(
            ZLinkMessagePayloads.encoded(payloadCopy),
            serializer);
        payloadCopy.close();
        trace("stream-node dispatch-enqueue node=" + streamNode.name()
            + " routingId=" + routingId
            + " name=" + streamHeader.packetName()
            + " requestSeq=" + streamHeader.requestSequence().orElse(null)
            + " correlation=" + streamHeader.correlationId().orElse(null));
        state.queue().enqueue(() ->
            executeHandler(() -> state.context().dispatchStage(streamHeader, sessionPayload, state.session())));
    }

    private void dispatchControl(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        ZLinkStreamHeader header,
        Message payload) {
        if (payload.toByteArray().length != 0) {
            throw new IllegalArgumentException("STREAM control packet payload must be empty");
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
            getOrCreateSessionState(streamNode, stream, routingId);
            return;
        }
        synchronized (sessions) {
            sessions.remove(key);
        }
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
            dispatchConnected(state);
        }
        return state;
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
                    defaultCodec),
            serializer,
            defaultCodec,
            compressionCodec,
            flow);
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
        return new SessionState(session, queue, context);
    }

    private void dispatchConnected(SessionState state) {
        state.queue().enqueue(() -> executeHandler(() ->
            ZLinkHandlerStages.fromRunnable(state.session()::onConnected)));
    }

    private static String sessionKey(StreamNodeRegistration streamNode, RoutingId routingId) {
        return streamNode.name() + ":" + routingId.toString();
    }

    @Override
    public void close() {
        List<SessionState> activeSessions;
        synchronized (sessions) {
            activeSessions = List.copyOf(sessions.values());
        }
        CompletableFuture.allOf(activeSessions.stream()
            .map(state -> state.queue().enqueue(() -> executeHandler(() -> disconnectSessionStage(state)))
                .toCompletableFuture())
            .toArray(CompletableFuture[]::new))
            .join();
        for (ZLinkBackendStreamSocket stream : streams) {
            stream.close();
        }
        context.close();
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

    private CompletionStage<Void> disconnectSessionStage(SessionState state) {
        return notifyBoundActorsDisconnectedBestEffort(state)
            .thenCompose(ignored -> ZLinkHandlerStages.fromRunnable(state.session()::onDisconnected));
    }

    private CompletionStage<Void> transportErrorDisconnectSessionStage(
        SessionState state,
        int nativeCode,
        String message) {
        return ZLinkHandlerStages.fromRunnable(() -> state.session().onError(new ZLinkStreamError(
                ZLinkStreamSessionError.TRANSPORT_ERROR,
                Optional.of(new ZLinkStreamDiagnostic(nativeCode, message)))))
            .thenCompose(ignored -> notifyBoundActorsDisconnectedBestEffort(state))
            .thenCompose(ignored -> ZLinkHandlerStages.fromRunnable(state.session()::onDisconnected));
    }

    private CompletionStage<Void> notifyBoundActorsDisconnectedBestEffort(SessionState state) {
        return state.context().notifyBoundActorsDisconnected()
            .handle((ignored, error) -> (Void) null);
    }

    private record SessionState(
        ZLinkSession session,
        ZLinkAsyncSerialQueue queue,
        ZLinkStreamSessionContextState context) {
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
