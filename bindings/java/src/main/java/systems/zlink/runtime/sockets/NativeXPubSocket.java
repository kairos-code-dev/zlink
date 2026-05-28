/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SendOp;
import systems.zlink.contracts.messaging.SubscriptionEvent;
public final class NativeXPubSocket extends NativeSocketBase implements XPubSocket {
    private final PubSocketOptions options = new PubSocketOptions(this);

    NativeXPubSocket(Context ctx) {
        super(ctx, SocketType.XPUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public SendOp publish(String topicId) {
        return SocketOperations.send((parts, flags) ->
            super.publish(topicId, parts, SendFlag.fromValue(flags.value())));
    }
    public boolean receiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags) { return super.receiveSubscriptionEvent(result, ReceiveFlag.fromValue(flags.value())); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public PubSocketOptions options() { return options; }
}
