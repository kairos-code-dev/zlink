/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;

public final class XPubSocket extends Socket {
    public XPubSocket(Context ctx) {
        super(ctx, SocketType.XPUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void publish(String topicId, Message part) { super.publish(topicId, part); }
    public void publish(String topicId, Message part, SendFlag flags) { super.publish(topicId, part, flags); }
    public void publish(String topicId, List<Message> parts) { super.publish(topicId, parts); }
    public void publish(String topicId, List<Message> parts, SendFlag flags) { super.publish(topicId, parts, flags); }
    public SubscriptionEvent subscriptionEvent() { return super.subscriptionEvent(); }
    public SubscriptionEvent subscriptionEvent(ReceiveFlag flags) { return super.subscriptionEvent(flags); }
    public void onSubscribe(SubscribeHandler handler) { super.onSubscribe(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
}
