package systems.zlink.framework.runtime.binding;

import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.SubSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSubscriberSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendTopicMessage;

record ZLinkJavaSubscriberSocket(SubSocket socket)
    implements ZLinkBackendSubscriberSocket, ZLinkJavaSocketBacked {
    @Override public Socket nativeSocket() { return socket; }
    @Override public String name() { return "subscriber"; }
    @Override public void bind(String endpoint) { socket.bind(endpoint); }
    @Override public void connect(String endpoint) { socket.connect(endpoint); }
    @Override public void disconnect(String endpoint) { socket.disconnect(endpoint); }
    @Override public void setChannelName(String channelName) { ZLinkJavaSocketSupport.validateChannelName(channelName); }
    @Override public void setSubscription(String topic) { socket.setSubscription(topic); }

    @Override
    public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) {
        try (TopicMessage result = new TopicMessage()) {
            return socket.subscribe(result, ZLinkJavaSocketSupport.map(mode))
                ? new ZLinkBackendTopicMessage(
                    result.getRoutingId(),
                    result.topic(),
                    ZLinkJavaBackendCodec.copyParts(result.parts()))
                : null;
        }
    }

    @Override public void close() { socket.close(); }
}
