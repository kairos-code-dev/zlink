/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;
import java.util.Optional;

public final class PairSocket extends Socket {
    public PairSocket(Context ctx) {
        super(ctx, SocketType.PAIR);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void send(Message part) { super.send(part); }
    public void send(Message part, SendFlags flags) { super.send(part, SendFlag.fromValue(flags.value())); }
    public void send(List<Message> parts) { super.send(parts); }
    public void send(List<Message> parts, SendFlags flags) { super.send(parts, SendFlag.fromValue(flags.value())); }
    public boolean trySend(Message part) { return super.trySend(part); }
    public boolean trySend(List<Message> parts) { return super.trySend(parts); }
    SendResult sendNoWaitResult(Message part) { return super.sendNoWaitResult(part); }
    SendResult sendNoWaitResult(List<Message> parts) { return super.sendNoWaitResult(parts); }
    public Received recv() { return super.recv(); }
    public Received recv(RecvFlags flags) { return super.recv(ReceiveFlag.fromValue(flags.value())); }
    public Received tryRecv() { return super.tryRecv(); }
    Optional<Received> recvNoWait() { return super.recvNoWait(); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
}
