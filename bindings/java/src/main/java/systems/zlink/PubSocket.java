/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.spot.SendOp;

public final class PubSocket extends Socket {
    private final PubSocketOptions options = new PubSocketOptions(this);

    public PubSocket(Context ctx) {
        super(ctx, SocketType.PUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public SendOp publish(String topicId) {
        return SocketOperations.send((parts, flags) ->
            super.publish(topicId, parts, SendFlag.fromValue(flags.value())));
    }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public PubSocketOptions options() { return options; }
}
