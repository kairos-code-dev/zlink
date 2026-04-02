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
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void publish(String topicId, Message part) { super.publish(topicId, part); }
    public void publish(String topicId, List<Message> parts) { super.publish(topicId, parts); }
    public SendResult tryPublish(String topicId, Message part) { return super.tryPublish(topicId, part); }
    public SendResult tryPublish(String topicId, List<Message> parts) { return super.tryPublish(topicId, parts); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public PubSocketOptions options() { return options; }
}
