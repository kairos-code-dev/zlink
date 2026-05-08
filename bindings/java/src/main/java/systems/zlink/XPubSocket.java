/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.util.List;
public final class XPubSocket extends Socket {
    private final PubSocketOptions options = new PubSocketOptions(this);

    public XPubSocket(Context ctx) {
        super(ctx, SocketType.XPUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public boolean publish(String topicId, Message part) { return super.publish(topicId, part); }
    public boolean publish(String topicId, Message part, SendFlags flags) { return super.publish(topicId, part, SendFlag.fromValue(flags.value())); }
    public boolean publish(String topicId, List<Message> parts) { return super.publish(topicId, parts); }
    public boolean publish(String topicId, List<Message> parts, SendFlags flags) { return super.publish(topicId, parts, SendFlag.fromValue(flags.value())); }
    public SubscriptionEvent receiveSubscriptionEvent() { return super.receiveSubscriptionEvent(); }
    public SubscriptionEvent receiveSubscriptionEvent(RecvFlags flags) { return super.receiveSubscriptionEvent(ReceiveFlag.fromValue(flags.value())); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public PubSocketOptions options() { return options; }
}
