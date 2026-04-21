/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public final class XSubSocket extends Socket {
    private final SubSocketOptions options = new SubSocketOptions(this);

    public XSubSocket(Context ctx) {
        super(ctx, SocketType.XSUB);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void setSubscription(String filter) { super.setSubscription(filter); }
    public void unsetSubscription(String filter) { super.unsetSubscription(filter); }
    public TopicMessage subscribe() { return super.subscribe(); }
    public TopicMessage subscribe(RecvFlags flags) { return super.subscribe(ReceiveFlag.fromValue(flags.value())); }
    @Override public SubSocketOptions options() { return options; }
}
