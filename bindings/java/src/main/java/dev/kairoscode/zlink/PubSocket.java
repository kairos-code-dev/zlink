/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.discovery.Discovery;
import java.util.List;

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
    public boolean publish(String topicId, Message part) { return super.publish(topicId, part); }
    public boolean publish(String topicId, Message part, SendFlags flags) { return super.publish(topicId, part, SendFlag.fromValue(flags.value())); }
    public boolean publish(String topicId, List<Message> parts) { return super.publish(topicId, parts); }
    public boolean publish(String topicId, List<Message> parts, SendFlags flags) { return super.publish(topicId, parts, SendFlag.fromValue(flags.value())); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public PubSocketOptions options() { return options; }
}
