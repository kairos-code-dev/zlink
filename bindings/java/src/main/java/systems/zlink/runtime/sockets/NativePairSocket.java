/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.runtime.messaging.MessageOperations;
import java.util.List;
final class NativePairSocket extends NativeSocketBase implements PairSocket {
    NativePairSocket(Context ctx) {
        super(ctx, SocketType.PAIR);
    }

    public SendOperation send() {
        return MessageOperations.send(
            (part, flags) -> super.send(part, SendFlag.fromValue(flags.value())),
            (parts, flags) -> super.send(parts, SendFlag.fromValue(flags.value())));
    }
    SendResult sendNoWaitResult(Message part) { return super.sendNoWaitResult(part); }
    SendResult sendNoWaitResult(List<Message> parts) { return super.sendNoWaitResult(parts); }
    Received recv(RecvFlags flags) { return super.recv(ReceiveFlag.fromValue(flags.value())); }
    /**
     * Receives into caller-provided {@link Received} storage.
     *
     * <p>HOT PATH: PAIR single-part recv fills {@code result} in place and
     * avoids allocating a fresh {@link Received} plus immutable parts list for
     * each message.
     */
    public boolean recv(Received result, RecvFlags flags) {
        java.util.Objects.requireNonNull(result, "result");
        java.util.Objects.requireNonNull(flags, "flags");
        return super.recvInto(result, ReceiveFlag.fromValue(flags.value()));
    }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }
}
