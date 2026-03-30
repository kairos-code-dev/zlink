/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;
import java.util.Optional;

public final class XPubSocket extends Socket {
    private final XPubSocketOptions options = new XPubSocketOptions(this);

    public XPubSocket(Context ctx) {
        super(ctx, SocketType.XPUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void publish(String topicId, Message part) { super.publish(topicId, part); }
    public void publish(String topicId, List<Message> parts) { super.publish(topicId, parts); }
    public SendResult tryPublish(String topicId, Message part) { return super.tryPublish(topicId, part); }
    public SendResult tryPublish(String topicId, List<Message> parts) { return super.tryPublish(topicId, parts); }
    public SubscriptionEvent receiveSubscriptionEvent() { return super.receiveSubscriptionEvent(); }
    public Optional<SubscriptionEvent> tryReceiveSubscriptionEvent() {
        return super.tryReceiveSubscriptionEvent();
    }
    public void onSubscribe(SubscribeHandler handler) { super.onSubscribe(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public XPubSocketOptions options() { return options; }
}
