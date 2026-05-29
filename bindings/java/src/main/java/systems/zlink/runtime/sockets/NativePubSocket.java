/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SendOperation;

public final class NativePubSocket extends NativeSocketBase implements PubSocket {
    private final PubSocketOptions options = new PubSocketOptions(this);

    NativePubSocket(Context ctx) {
        super(ctx, SocketType.PUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public SendOperation publish(String topicId) {
        return SocketOperations.send(
            (part, flags) -> super.publish(topicId, part,
                SendFlag.fromValue(flags.value())),
            (parts, flags) ->
            super.publish(topicId, parts, SendFlag.fromValue(flags.value())));
    }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }
    @Override public PubSocketOptions options() { return options; }
}
