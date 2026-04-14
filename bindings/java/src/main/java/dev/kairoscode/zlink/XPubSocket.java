/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;
import java.util.Optional;

public final class XPubSocket extends Socket {
    private final PubSocketOptions options = new PubSocketOptions(this);

    public XPubSocket(Context ctx) {
        super(ctx, SocketType.XPUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void publish(String topicId, Message part) { super.publish(topicId, part); }
    public void publish(String topicId, Message part, SendFlags flags) { super.publish(topicId, part, SendFlag.fromValue(flags.value())); }
    public void publish(String topicId, List<Message> parts) { super.publish(topicId, parts); }
    public void publish(String topicId, List<Message> parts, SendFlags flags) { super.publish(topicId, parts, SendFlag.fromValue(flags.value())); }
    public SubscriptionEvent receiveSubscriptionEvent() { return super.receiveSubscriptionEvent(); }
    public SubscriptionEvent receiveSubscriptionEvent(RecvFlags flags) { return super.receiveSubscriptionEvent(ReceiveFlag.fromValue(flags.value())); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public PubSocketOptions options() { return options; }
}
