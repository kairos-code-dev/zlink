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
    public void send(List<Message> parts) { super.send(parts); }
    public SendResult trySend(Message part) { return super.trySend(part); }
    public SendResult trySend(List<Message> parts) { return super.trySend(parts); }
    public Received recv() { return super.recv(); }
    public Optional<Received> tryRecv() { return super.tryRecv(); }
    public void onReceive(SocketMessageHandler handler) { super.onReceive(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
}
