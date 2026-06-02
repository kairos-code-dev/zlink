package systems.zlink.framework.runtime.streams;

import systems.zlink.framework.runtime.backend.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkStreamDiagnostic;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamSessionError;

public final class ZLinkStreamRuntime implements AutoCloseable {
    private final ZLinkBackendContext context;
    private final ZLinkMessageSerializer serializer;
    private final List<ZLinkBackendStreamSocket> streams = new ArrayList<>();
    private final Map<String, ZLinkBackendStreamSocket> streamsByName = new HashMap<>();
    private final Map<String, SessionState> sessions = new HashMap<>();

    public ZLinkStreamRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        Map<String, ZLinkBackendSpotNode> spotNodes,
        ZLinkMessageSerializer serializer) {
        if (registration.streamNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one stream node is required");
        }
        this.serializer = serializer;
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
        SessionState state = sessions.computeIfAbsent(
            sessionKey(streamNode, routingId),
            ignored -> createSessionState(streamNode));
        ZLinkStreamHeader streamHeader = new ZLinkStreamHeader(
            header.toUtf8String(),
            Map.of(),
            Optional.empty());
        Message payloadCopy = Message.from(payload);
        state.queue().enqueue(() ->
            state.session().onDispatchAsync(streamHeader, payloadCopy)
                .whenComplete((ignored, error) -> payloadCopy.close()));
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

    private SessionState createSessionState(StreamNodeRegistration streamNode) {
        try {
            ZLinkSession session = streamNode.sessionType()
                .getConstructor()
                .newInstance();
            ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
            queue.enqueue(session::onConnectedAsync);
            return new SessionState(session, queue);
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "stream session type must expose a public no-arg constructor: "
                    + streamNode.sessionType().getName());
        }
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
        ZLinkAsyncSerialQueue queue) {
    }
}
