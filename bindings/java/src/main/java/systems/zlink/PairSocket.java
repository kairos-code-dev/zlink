/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.util.List;
public final class PairSocket extends Socket {
    public PairSocket(Context ctx) {
        super(ctx, SocketType.PAIR);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public boolean send(Message part) { return super.send(part); }
    public boolean send(Message part, SendFlags flags) { return super.send(part, SendFlag.fromValue(flags.value())); }
    public boolean send(List<Message> parts) { return super.send(parts); }
    public boolean send(List<Message> parts, SendFlags flags) { return super.send(parts, SendFlag.fromValue(flags.value())); }
    SendResult sendNoWaitResult(Message part) { return super.sendNoWaitResult(part); }
    SendResult sendNoWaitResult(List<Message> parts) { return super.sendNoWaitResult(parts); }
    @Deprecated
    public Received recv() { return super.recv(); }
    @Deprecated
    public Received recv(RecvFlags flags) { return super.recv(ReceiveFlag.fromValue(flags.value())); }

    /** Canonical caller-provided storage recv. See doc/spec/bindings/README.md. */
    public boolean recv(Received result, RecvFlags flags) {
        java.util.Objects.requireNonNull(result, "result");
        java.util.Objects.requireNonNull(flags, "flags");
        Received fresh = super.recv(ReceiveFlag.fromValue(flags.value()));
        if (fresh == null) return false;
        result.adoptFrom(fresh);
        return true;
    }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
}
