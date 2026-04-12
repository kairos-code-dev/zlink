/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.discovery.Discovery;
import java.util.Optional;

public final class SubSocket extends Socket {
    private final SubSocketOptions options = new SubSocketOptions(this);

    public SubSocket(Context ctx) {
        super(ctx, SocketType.SUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setSubscription(String filter) { super.setSubscription(filter); }
    public void unsetSubscription(String filter) { super.unsetSubscription(filter); }
    public TopicMessage subscribe() { return super.subscribe(); }
    public TopicMessage subscribe(RecvFlags flags) { return super.subscribe(ReceiveFlag.fromValue(flags.value())); }
    public void onSubscribe(SubscribeHandler handler) { super.onSubscribe(handler); }
    @Override public SubSocketOptions options() { return options; }
}
