package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.SpotRouteBridge;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRouteBridge;

record ZLinkJavaSpotRouteBridge(SpotRouteBridge bridge) implements ZLinkBackendSpotRouteBridge {
    @Override public String name() { return "spotRouteBridge"; }
    @Override public void attachRouterChannel(String channelName, ZLinkBackendRouterSocket router) {
        bridge.attachRouterChannel(channelName, ((ZLinkJavaRouterSocket) router).socket());
    }
    @Override public boolean send(String channelName, RoutingId targetNodeRid, RoutingId targetSpotRid, List<Message> parts, SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(bridge.send(channelName, targetNodeRid, targetSpotRid), parts, flags);
    }
    @Override public boolean request(String channelName, RoutingId targetNodeRid, RoutingId targetSpotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
        return ZLinkJavaSocketSupport.submitRequest(
            bridge.request(channelName, targetNodeRid, targetSpotRid),
            parts,
            callback,
            flags,
            timeout);
    }
    @Override public CompletionStage<List<Message>> requestAsync(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        SendFlags flags,
        Duration timeout) {
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("SPOT route bridge request must contain at least one part");
        }
        var submit = bridge.request(channelName, targetNodeRid, targetSpotRid)
            .message(parts.get(0));
        if (timeout != null) {
            submit = submit.timeout(timeout);
        }
        for (int i = 1; i < parts.size(); i++) {
            submit = submit.message(parts.get(i));
        }
        return submit.submit();
    }
    @Override public boolean handleRouterReceived(String channelName, RoutingId sourceNodeRid, long requestSeq, List<Message> parts) {
        return bridge.handleRouterReceived(channelName, sourceNodeRid, requestSeq, parts);
    }
    @Override public int drain() { return bridge.drain(); }
    @Override public void close() { bridge.close(); }
}
