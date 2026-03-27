/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;

public final class XSubSocket extends Socket {
    public XSubSocket(Context ctx) {
        super(ctx, SocketType.XSUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void setSubscription(String filter) { super.setSubscription(filter); }
    public void unsetSubscription(String filter) { super.unsetSubscription(filter); }
    public List<SubscriptionEntry> subscriptions() { return super.subscriptions(); }
    public TopicMessage subscribe() { return super.subscribe(); }
    public TopicMessage subscribe(ReceiveFlag flags) { return super.subscribe(flags); }
    public void onSubscribe(SubscribeHandler handler) { super.onSubscribe(handler); }
}
