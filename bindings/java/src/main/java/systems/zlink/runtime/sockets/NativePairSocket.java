/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.runtime.nativeapi.ContractAccess;

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
        return MessageOperations.send((parts, flags) ->
            super.send(parts, SendFlag.fromValue(flags.value())));
    }
    SendResult sendNoWaitResult(Message part) { return super.sendNoWaitResult(part); }
    SendResult sendNoWaitResult(List<Message> parts) { return super.sendNoWaitResult(parts); }
    Received recv(RecvFlags flags) { return super.recv(ReceiveFlag.fromValue(flags.value())); }
    /** Canonical caller-provided storage recv. See doc/spec/bindings/README.md. */
    public boolean recv(Received result, RecvFlags flags) {
        java.util.Objects.requireNonNull(result, "result");
        java.util.Objects.requireNonNull(flags, "flags");
        Received fresh = super.recv(ReceiveFlag.fromValue(flags.value()));
        if (fresh == null) return false;
        ContractAccess.receivedAdoptFrom(result, fresh);
        return true;
    }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }
}
