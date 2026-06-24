package systems.zlink.framework.runtime.streams;

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
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkSessionClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionReplyCall;
import systems.zlink.framework.streams.ZLinkSessionSendCall;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamDiagnostic;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;
import systems.zlink.framework.streams.ZLinkStreamSessionError;

public final class ZLinkStreamRuntime implements AutoCloseable {
    private final ZLinkBackendContext context;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actors;
    private final ZLinkHandlerFactory handlerFactory;
    private final Executor handlerExecutor;
    private final systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer flow;
    private final List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers;
    private final ZLinkStreamCodec defaultCodec;
    private final Predicate<RoutingId> sessionRelayRouteReady;
    private final ZLinkSessionActorsRuntime.LocalActorDispatcher localActorDispatcher;
    private final List<ZLinkBackendStreamSocket> streams = new ArrayList<>();
    private final Map<String, ZLinkBackendStreamSocket> streamsByName = new HashMap<>();
    private final Map<String, Boolean> streamSessionRelayAttached = new HashMap<>();
    private final Map<String, ZLinkBackendSpotNode> streamSessionRelaySpotNodes = new HashMap<>();
    private final Map<String, SessionState> sessions = new HashMap<>();

    public ZLinkStreamRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        Map<String, ZLinkBackendSpotNode> spotNodes,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actors,
        ZLinkHandlerFactory handlerFactory) {
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
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        Map<String, ZLinkBackendSpotNode> spotNodes,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actors,
        ZLinkHandlerFactory handlerFactory,
        Predicate<RoutingId> sessionRelayRouteReady,
        ZLinkSpotRuntime spots) {
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
            registration.dispatchOptions(), handlerFactory, this.handlerExecutor);
        this.suspendHandlerInvokers = registration.suspendHandlerInvokers();
        this.defaultCodec = defaultCodec(registration);
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
            stream.bind(streamNode.bindEndpoint());
            stream.onPacket((routingId, header, payload) ->
                dispatchToSession(streamNode, routingId, header, payload));
            stream.onTransportError((routingId, nativeCode, message) ->
                reportTransportError(streamNode, routingId, nativeCode, message));
            ZLinkBackendSpotNode spotNode = resolveSessionRelayNode(spotNodes);
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

    private static ZLinkBackendSpotNode resolveSessionRelayNode(
        Map<String, ZLinkBackendSpotNode> spotNodes) {
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
            dispatchStreamNotification(streamNode, stream, routingId);
            return;
        }
        SessionState state = sessions.computeIfAbsent(
            sessionKey(streamNode, routingId),
            ignored -> createSessionState(streamNode, stream, routingId));
        ZLinkStreamHeader streamHeader =
            ZLinkStreamHeaderCodec.decodeOrPlain(header.toByteArray());
        if (flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowPhase.RECEIVED)) {
            String corr = streamHeader.correlationId()
                .orElseGet(() -> streamHeader.requestSequence().map(String::valueOf).orElse(null));
            flow.trace(new systems.zlink.framework.configuration.ZLinkMessageFlowEvent(
                systems.zlink.framework.configuration.ZLinkMessageFlowPhase.RECEIVED,
                systems.zlink.framework.configuration.ZLinkDispatchErrorSurface.STREAM_SESSION,
                streamHeader.requestSequence().isPresent()
                    ? systems.zlink.framework.configuration.ZLinkDispatchMessageKind.REQUEST
                    : systems.zlink.framework.configuration.ZLinkDispatchMessageKind.SEND,
                streamHeader.packetName(), null, null, corr, null, null, null, null));
        }
        Message payloadCopy = Message.from(decodePayload(streamHeader, payload));
        ZLinkMessage sessionPayload = ZLinkMessage.fromEncoded(
            ZLinkMessagePayloads.encoded(payloadCopy),
            serializer);
        payloadCopy.close();
        state.queue().enqueue(() ->
            executeHandler(() -> state.context().dispatchStage(streamHeader, sessionPayload, state.session())));
    }

    private void dispatchStreamNotification(
        StreamNodeRegistration streamNode,
        ZLinkBackendStreamSocket stream,
        RoutingId routingId) {
        String key = sessionKey(streamNode, routingId);
        SessionState state = sessions.get(key);
        if (state == null) {
            sessions.put(key, createSessionState(streamNode, stream, routingId));
            return;
        }
        sessions.remove(key);
        state.queue().enqueue(() -> executeHandler(() ->
            ZLinkHandlerStages.fromRunnable(state.session()::onDisconnected)));
    }

    private static boolean isStreamNotification(Message header, Message payload) {
        return header.toByteArray().length == 0 && payload.toByteArray().length == 0;
    }

    private void reportTransportError(
        StreamNodeRegistration streamNode,
        RoutingId routingId,
        int nativeCode,
        String message) {
        SessionState state = sessions.get(sessionKey(streamNode, routingId));
        if (state == null) {
            return;
        }
        sessions.remove(sessionKey(streamNode, routingId));
        state.queue().enqueue(() -> executeHandler(() ->
            ZLinkHandlerStages.fromRunnable(() -> {
                state.session().onError(new ZLinkStreamError(
                    ZLinkStreamSessionError.TRANSPORT_ERROR,
                    Optional.of(new ZLinkStreamDiagnostic(nativeCode, message))));
                state.session().onDisconnected();
            })));
    }

    private SessionState createSessionState(
        StreamNodeRegistration streamNode,
        ZLinkBackendStreamSocket stream,
        RoutingId routingId) {
        DefaultSessionContext context = new DefaultSessionContext(
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
                    defaultCodec));
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> dispatcher =
            new ZLinkSessionPacketDispatcherRuntime<>(
                streamNode.sessionPacketHandlers(),
                handlerFactory,
                serializer,
                handlerExecutor,
                suspendHandlerInvokers);
        ZLinkHandlerFactory.MutableServices sessionFactory =
            ZLinkHandlerFactory.services(handlerFactory)
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
        queue.enqueue(() -> executeHandler(() ->
            ZLinkHandlerStages.fromRunnable(session::onConnected)));
        return new SessionState(session, queue, context);
    }

    private static String sessionKey(StreamNodeRegistration streamNode, RoutingId routingId) {
        return streamNode.name() + ":" + routingId.toString();
    }

    @Override
    public void close() {
        for (SessionState state : sessions.values()) {
            state.queue().enqueue(() -> executeHandler(() ->
                ZLinkHandlerStages.fromRunnable(state.session()::onDisconnected)));
        }
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

    private record SessionState(
        ZLinkSession session,
        ZLinkAsyncSerialQueue queue,
        DefaultSessionContext context) {
    }

    private final class DefaultSessionContext implements ZLinkSessionContext {
        private final String streamNodeName;
        private final ZLinkBackendStreamSocket stream;
        private final RoutingId routingId;
        private final ZLinkSessionActors actors;
        private ZLinkStreamHeader currentDispatchHeader;

        DefaultSessionContext(
            String streamNodeName,
            ZLinkBackendStreamSocket stream,
            RoutingId routingId,
            ZLinkSessionActors actors) {
            this.streamNodeName = streamNodeName;
            this.stream = stream;
            this.routingId = routingId;
            this.actors = actors;
        }

        @Override
        public String sessionId() {
            return streamNodeName + ":" + routingId;
        }

        @Override
        public Optional<RoutingId> routingId() {
            return Optional.of(routingId);
        }

        @Override
        public Optional<String> localAddr() {
            return Optional.empty();
        }

        @Override
        public Optional<String> remoteAddr() {
            return Optional.empty();
        }

        @Override
        public ZLinkSessionClient client() {
            return new SessionClient(stream, routingId, this);
        }

        @Override
        public ZLinkSessionActors actors() {
            if (actors == null) {
                throw new ZLinkConfigurationException(
                    "stream node is not attached to a session relay");
            }
            return actors;
        }

        @Override
        public CompletionStage<Void> close() {
            return CompletableFuture.completedFuture(null);
        }

        CompletionStage<Void> dispatchStage(
            ZLinkStreamHeader header,
            ZLinkMessage payload,
            ZLinkSession session) {
            currentDispatchHeader = header;
            ZLinkSessionDispatchContext dispatch = new ZLinkSessionDispatchContext(
                header.name(),
                header.metadata(),
                header.requestSequence().isPresent());
            CompletionStage<Void> stage;
            try {
                stage = ZLinkHandlerStages.fromRunnable(() -> {
                    ZLinkSessionActorsRuntime.enterRelayDispatch(header);
                    try {
                        session.onDispatch(dispatch, payload);
                    } finally {
                        ZLinkSessionActorsRuntime.exitRelayDispatch();
                    }
                });
            } catch (RuntimeException ex) {
                currentDispatchHeader = null;
                return CompletableFuture.failedFuture(ex);
            }
            return stage.whenComplete((ignored, error) -> {
                currentDispatchHeader = null;
                if (error == null
                    && header.requestSequence().isEmpty()
                    && flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowPhase.DISPATCHED)) {
                    flow.trace(new systems.zlink.framework.configuration.ZLinkMessageFlowEvent(
                        systems.zlink.framework.configuration.ZLinkMessageFlowPhase.DISPATCHED,
                        systems.zlink.framework.configuration.ZLinkDispatchErrorSurface.STREAM_SESSION,
                        systems.zlink.framework.configuration.ZLinkDispatchMessageKind.SEND,
                        header.packetName(), null, null,
                        header.correlationId().orElse(null), null, null, null, null));
                }
            });
        }

        void traceStreamReplied(ZLinkStreamHeader requestHeader) {
            if (flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowPhase.REPLIED)) {
                flow.trace(new systems.zlink.framework.configuration.ZLinkMessageFlowEvent(
                    systems.zlink.framework.configuration.ZLinkMessageFlowPhase.REPLIED,
                    systems.zlink.framework.configuration.ZLinkDispatchErrorSurface.STREAM_SESSION,
                    systems.zlink.framework.configuration.ZLinkDispatchMessageKind.REQUEST,
                    requestHeader.packetName(), null, null,
                    requestHeader.correlationId()
                        .orElseGet(() -> requestHeader.requestSequence().map(String::valueOf).orElse(null)),
                    null, null, null, null));
            }
        }

        Optional<ZLinkStreamHeader> currentDispatchHeader() {
            return Optional.ofNullable(currentDispatchHeader);
        }
    }

    private final class SessionClient implements ZLinkSessionClient {
        private final ZLinkBackendStreamSocket stream;
        private final RoutingId routingId;
        private final DefaultSessionContext context;

        SessionClient(
            ZLinkBackendStreamSocket stream,
            RoutingId routingId,
            DefaultSessionContext context) {
            this.stream = stream;
            this.routingId = routingId;
            this.context = context;
        }

        @Override
        public ZLinkSessionSendCall send(Object message) {
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            return new SessionSendCall(
                stream,
                routingId,
                encoded.payload(),
                encoded.packetName(),
                Map.of(),
                false,
                defaultCodec);
        }

        @Override
        public ZLinkSessionReplyCall reply(Object message) {
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, message);
            return new SessionReplyCall(
                stream,
                routingId,
                encoded.payload(),
                context,
                encoded.packetName(),
                Map.of(),
                false);
        }
    }

    private record SessionSendCall(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        Message payload,
        String packetName,
        Map<String, String> metadata,
        boolean compressed,
        ZLinkStreamCodec codec) implements ZLinkSessionSendCall {
        @Override
        public ZLinkSessionSendCall metadata(String key, String value) {
            Map<String, String> next = new HashMap<>(metadata);
            next.put(key, value);
            return new SessionSendCall(
                stream,
                routingId,
                payload,
                packetName,
                Map.copyOf(next),
                compressed,
                codec);
        }

        @Override
        public ZLinkSessionSendCall packetName(String messageName) {
            if (messageName == null || messageName.isBlank()) {
                throw new IllegalArgumentException("messageName is required");
            }
            return new SessionSendCall(
                stream,
                routingId,
                payload,
                messageName,
                metadata,
                compressed,
                codec);
        }

        @Override
        public ZLinkSessionSendCall compress() {
            return new SessionSendCall(
                stream,
                routingId,
                payload,
                packetName,
                metadata,
                true,
                codec);
        }

        @Override
        public CompletionStage<Void> submit() {
            EncodedStreamPayload encoded = encodePayload(payload, compressed);
            ZLinkStreamHeader header = new ZLinkStreamHeader(
                ZLinkStreamMessageKind.SEND,
                codec,
                encoded.flags(),
                Optional.empty(),
                packetName,
                metadata,
                Optional.of(ZLinkStreamCorrelation.next()));
            List<Message> parts = List.of(Message.from(encoded.payload()));
            try {
                if (!stream.send(routingId, header, parts, SendFlags.DONT_WAIT)) {
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "session send failed: " + routingId));
                }
                return CompletableFuture.completedFuture(null);
            } finally {
                parts.forEach(Message::close);
                payload.close();
            }
        }
    }

    private record SessionReplyCall(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        Message payload,
        DefaultSessionContext context,
        String packetName,
        Map<String, String> metadata,
        boolean compressed) implements ZLinkSessionReplyCall {
        @Override
        public ZLinkSessionReplyCall metadata(String key, String value) {
            Map<String, String> next = new HashMap<>(metadata);
            next.put(key, value);
            return new SessionReplyCall(stream, routingId, payload, context, packetName, Map.copyOf(next), compressed);
        }

        @Override
        public ZLinkSessionReplyCall compress() {
            return new SessionReplyCall(stream, routingId, payload, context, packetName, metadata, true);
        }

        @Override
        public CompletionStage<Void> submit() {
            Optional<ZLinkStreamHeader> currentHeader = context.currentDispatchHeader();
            if (currentHeader.isEmpty() || currentHeader.get().requestSequence().isEmpty()) {
                payload.close();
                return CompletableFuture.failedFuture(new IllegalStateException(
                    "Reply is only available while handling a request packet."));
            }
            EncodedStreamPayload encoded = encodePayload(payload, compressed);
            List<Message> parts = List.of(Message.from(encoded.payload()));
            try {
                ZLinkStreamHeader current = currentHeader.get();
                ZLinkStreamHeader replyHeader = new ZLinkStreamHeader(
                    ZLinkStreamMessageKind.RESPONSE,
                    current.codec(),
                    encoded.flags(),
                    current.requestSequence(),
                    packetName,
                    metadata,
                    // Echo the request's correlation id onto the reply.
                    current.correlationId());
                if (!stream.reply(
                    routingId,
                    replyHeader,
                    parts,
                    SendFlags.DONT_WAIT)) {
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "session reply failed: " + routingId));
                }
                context.traceStreamReplied(current);
                return CompletableFuture.completedFuture(null);
            } finally {
                parts.forEach(Message::close);
                payload.close();
            }
        }
    }

    private static ZLinkStreamCodec defaultCodec(ZLinkFrameworkRegistration registration) {
        return registration.codecs().streamCodecForCustomSerializer()
            .orElse(ZLinkStreamCodec.JSON);
    }

    private static EncodedStreamPayload encodePayload(Message payload, boolean compress) {
        byte[] bytes = payload.toByteArray();
        if (!compress) {
            return new EncodedStreamPayload(bytes, EnumSet.noneOf(ZLinkStreamHeaderFlag.class));
        }
        return new EncodedStreamPayload(
            ZLinkStreamLz4Pickler.pickle(bytes),
            EnumSet.of(ZLinkStreamHeaderFlag.PAYLOAD_COMPRESSED));
    }

    private static byte[] decodePayload(ZLinkStreamHeader header, Message payload) {
        byte[] bytes = payload.toByteArray();
        if (!header.flags().contains(ZLinkStreamHeaderFlag.PAYLOAD_COMPRESSED)) {
            return bytes;
        }
        return ZLinkStreamLz4Pickler.unpickle(bytes);
    }

    private record EncodedStreamPayload(
        byte[] payload,
        EnumSet<ZLinkStreamHeaderFlag> flags) {
    }
}
