package systems.zlink.framework.runtime.streams;

import systems.zlink.framework.runtime.backend.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkSessionClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionReplyCall;
import systems.zlink.framework.streams.ZLinkSessionSendCall;
import systems.zlink.framework.streams.ZLinkStreamDiagnostic;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamSessionError;

public final class ZLinkStreamRuntime implements AutoCloseable {
    private final ZLinkBackendContext context;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actors;
    private final ZLinkHandlerFactory handlerFactory;
    private final List<ZLinkBackendStreamSocket> streams = new ArrayList<>();
    private final Map<String, ZLinkBackendStreamSocket> streamsByName = new HashMap<>();
    private final Map<String, SessionState> sessions = new HashMap<>();

    public ZLinkStreamRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        Map<String, ZLinkBackendSpotNode> spotNodes,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actors,
        ZLinkHandlerFactory handlerFactory) {
        if (registration.streamNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one stream node is required");
        }
        this.serializer = serializer;
        this.actors = actors;
        this.handlerFactory = handlerFactory;
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
            if (streamNode.actorGatewaySpotNodeName() != null) {
                ZLinkBackendSpotNode spotNode =
                    spotNodes.get(streamNode.actorGatewaySpotNodeName());
                if (spotNode == null) {
                    throw new ZLinkConfigurationException(
                        "stream node actor gateway SpotNode is not running: "
                            + streamNode.actorGatewaySpotNodeName());
                }
                stream.attachActorGateway(spotNode);
            }
            streams.add(stream);
            streamsByName.put(streamNode.name(), stream);
        }
    }

    public ZLinkSessionActorsRuntime sessionActors(
        String streamNodeName,
        systems.zlink.contracts.core.RoutingId sessionRid,
        ZLinkActorRuntime actors) {
        ZLinkBackendStreamSocket stream = streamsByName.get(streamNodeName);
        if (stream == null) {
            throw new ZLinkConfigurationException(
                "stream node is not running: " + streamNodeName);
        }
        return new ZLinkSessionActorsRuntime(stream, sessionRid, actors, serializer);
    }

    private void dispatchToSession(
        StreamNodeRegistration streamNode,
        RoutingId routingId,
        Message header,
        Message payload) {
        ZLinkBackendStreamSocket stream = streamsByName.get(streamNode.name());
        SessionState state = sessions.computeIfAbsent(
            sessionKey(streamNode, routingId),
            ignored -> createSessionState(streamNode, stream, routingId));
        ZLinkStreamHeader streamHeader =
            ZLinkStreamHeaderCodec.decodeOrPlain(header.toByteArray());
        Message payloadCopy = Message.from(payload);
        state.queue().enqueue(() ->
            state.context().dispatchAsync(streamHeader, payloadCopy, state.session()));
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
        state.queue().enqueue(() -> state.session().onErrorAsync(new ZLinkStreamError(
                ZLinkStreamSessionError.TRANSPORT_ERROR,
                Optional.of(new ZLinkStreamDiagnostic(nativeCode, message)))));
    }

    private SessionState createSessionState(
        StreamNodeRegistration streamNode,
        ZLinkBackendStreamSocket stream,
        RoutingId routingId) {
        DefaultSessionContext context = new DefaultSessionContext(
            streamNode.name(),
            stream,
            routingId,
            actors == null
                ? null
                : new ZLinkSessionActorsRuntime(stream, routingId, actors, serializer));
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> dispatcher =
            new ZLinkSessionPacketDispatcherRuntime<>(
                streamNode.sessionPacketHandlers(),
                handlerFactory);
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
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        queue.enqueue(session::onConnectedAsync);
        return new SessionState(session, queue, context);
    }

    private static String sessionKey(StreamNodeRegistration streamNode, RoutingId routingId) {
        return streamNode.name() + ":" + routingId.toString();
    }

    @Override
    public void close() {
        for (SessionState state : sessions.values()) {
            state.queue().enqueue(state.session()::onDisconnectedAsync);
        }
        for (ZLinkBackendStreamSocket stream : streams) {
            stream.close();
        }
        context.close();
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
                    "stream node is not attached to an actor gateway");
            }
            return actors;
        }

        @Override
        public CompletionStage<Void> closeAsync() {
            return CompletableFuture.completedFuture(null);
        }

        CompletionStage<Void> dispatchAsync(
            ZLinkStreamHeader header,
            Message payload,
            ZLinkSession session) {
            currentDispatchHeader = header;
            CompletionStage<Void> stage;
            try {
                stage = session.onDispatchAsync(header, payload);
            } catch (RuntimeException ex) {
                currentDispatchHeader = null;
                payload.close();
                return CompletableFuture.failedFuture(ex);
            }
            return stage.whenComplete((ignored, error) -> {
                currentDispatchHeader = null;
                payload.close();
            });
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
        public <TMessage> ZLinkSessionSendCall send(TMessage message) {
            return new SessionSendCall(
                stream,
                routingId,
                serializer.serialize(message),
                Optional.empty());
        }

        @Override
        public <TMessage> ZLinkSessionReplyCall reply(TMessage message) {
            return new SessionReplyCall(
                stream,
                routingId,
                serializer.serialize(message),
                context);
        }
    }

    private record SessionSendCall(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        Message payload,
        Optional<String> packetName) implements ZLinkSessionSendCall {
        @Override
        public ZLinkSessionSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkSessionSendCall packetName(String messageName) {
            if (messageName == null || messageName.isBlank()) {
                throw new IllegalArgumentException("messageName is required");
            }
            return new SessionSendCall(stream, routingId, payload, Optional.of(messageName));
        }

        @Override
        public ZLinkSessionSendCall compress() {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            List<Message> parts = parts(packetName, payload);
            try {
                if (!stream.send(routingId, parts, SendFlags.DONT_WAIT)) {
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "session send failed: " + routingId));
                }
                return CompletableFuture.completedFuture(null);
            } finally {
                parts.forEach(Message::close);
            }
        }
    }

    private record SessionReplyCall(
        ZLinkBackendStreamSocket stream,
        RoutingId routingId,
        Message payload,
        DefaultSessionContext context) implements ZLinkSessionReplyCall {
        @Override
        public ZLinkSessionReplyCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkSessionReplyCall compress() {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            Optional<ZLinkStreamHeader> currentHeader = context.currentDispatchHeader();
            if (currentHeader.isEmpty() || currentHeader.get().requestSequence().isEmpty()) {
                payload.close();
                return CompletableFuture.failedFuture(new IllegalStateException(
                    "Reply is only available while handling a request packet."));
            }
            try {
                ZLinkStreamHeader header = currentHeader.get();
                if (!stream.reply(
                    routingId,
                    header.requestSequence().get(),
                    header.packetName(),
                    List.of(payload),
                    SendFlags.DONT_WAIT)) {
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "session reply failed: " + routingId));
                }
                return CompletableFuture.completedFuture(null);
            } finally {
                payload.close();
            }
        }
    }

    private static List<Message> parts(Optional<String> packetName, Message payload) {
        if (packetName.isEmpty()) {
            return List.of(payload);
        }
        return List.of(Message.from(packetName.get()), payload);
    }
}
