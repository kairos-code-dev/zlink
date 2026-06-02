package systems.zlink.framework.runtime;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class ZLinkStreamRuntime implements AutoCloseable {
    private final ZLinkBackendContext context;
    private final ZLinkMessageSerializer serializer;
    private final List<ZLinkBackendStreamSocket> streams = new ArrayList<>();
    private final Map<String, ZLinkBackendStreamSocket> streamsByName = new HashMap<>();
    private final Map<String, ZLinkSession> sessions = new HashMap<>();

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

    ZLinkSessionActorsRuntime sessionActors(
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
        ZLinkSession session = sessions.computeIfAbsent(
            streamNode.name() + ":" + routingId.toString(),
            ignored -> createSession(streamNode));
        ZLinkStreamHeader streamHeader = new ZLinkStreamHeader(
            header.toUtf8String(),
            Map.of(),
            Optional.empty());
        Message payloadCopy = Message.from(payload);
        try {
            session.onDispatchAsync(streamHeader, payloadCopy)
                .toCompletableFuture()
                .join();
        } finally {
            payloadCopy.close();
        }
    }

    private ZLinkSession createSession(StreamNodeRegistration streamNode) {
        try {
            ZLinkSession session = streamNode.sessionType()
                .getConstructor()
                .newInstance();
            session.onConnectedAsync().toCompletableFuture().join();
            return session;
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "stream session type must expose a public no-arg constructor: "
                    + streamNode.sessionType().getName());
        }
    }

    @Override
    public void close() {
        for (ZLinkSession session : sessions.values()) {
            session.onDisconnectedAsync().toCompletableFuture().join();
        }
        for (ZLinkBackendStreamSocket stream : streams) {
            stream.close();
        }
        context.close();
    }
}
