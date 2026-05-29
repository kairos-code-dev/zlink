/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import java.util.List;
import java.util.Objects;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.ReceiveFlag;
import systems.zlink.contracts.sockets.SendFlag;
import systems.zlink.contracts.sockets.SendResult;

final class MessagePlane {
    private final NativeSocketRuntime socket;

    MessagePlane(NativeSocketRuntime socket) {
        this.socket = socket;
    }

    void send(Message part) {
        send(part, SendFlag.NONE);
    }

    void send(Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        if (flags == SendFlag.DONTWAIT) {
            SendResult result = socket.sendMessageFrameNoWaitResult(part);
            if (result != SendResult.SENT) {
                throw NativeSocketRuntime.submitExceptionFromSendResult(result);
            }
            return;
        }
        socket.sendMessageFrame(part, flags);
    }

    void send(List<Message> parts) {
        send(parts, SendFlag.NONE);
    }

    void send(List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        socket.sendParts(null, parts, flags, false);
    }

    SendResult sendNoWaitResult(Message part) {
        Objects.requireNonNull(part, "part");
        return socket.sendMessageFrameNoWaitResult(part);
    }

    SendResult sendNoWaitResult(List<Message> parts) {
        Objects.requireNonNull(parts, "parts");
        return socket.sendNoWaitPartsResult(null, parts);
    }

    void send(RoutingId rid, Message part) {
        send(rid, part, SendFlag.NONE);
    }

    void send(RoutingId rid, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        if (flags == SendFlag.DONTWAIT) {
            SendResult result = socket.sendMessageFrameNoWaitResult(rid, part);
            if (result != SendResult.SENT) {
                throw NativeSocketRuntime.submitExceptionFromSendResult(result);
            }
            return;
        }
        socket.sendMessageFrame(rid, part, flags);
    }

    void send(RoutingId rid, List<Message> parts) {
        send(rid, parts, SendFlag.NONE);
    }

    void send(RoutingId rid, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        socket.sendParts(rid, parts, flags, false);
    }

    SendResult sendNoWaitResult(RoutingId rid, Message part) {
        Objects.requireNonNull(part, "part");
        return socket.sendMessageFrameNoWaitResult(rid, part);
    }

    SendResult sendNoWaitResult(RoutingId rid, List<Message> parts) {
        Objects.requireNonNull(parts, "parts");
        return socket.sendNoWaitPartsResult(rid, parts);
    }

    Received recv() {
        return recv(ReceiveFlag.NONE);
    }

    Received recv(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        return socket.recvLazy(flags);
    }

    Received recvNoWaitOrNull() {
        return socket.recvLazyNoWaitOrNull();
    }

    Optional<Received> recvNoWait() {
        return Optional.ofNullable(recvNoWaitOrNull());
    }

    void sendMessageFrame(Message message, SendFlag flag) {
        socket.sendMessageFrame(message, flag);
    }

    boolean sendMessageFrameNoWaitResult(Message message, SendFlag flag) {
        return socket.sendMessageFrameNoWaitResult(message, flag);
    }

    void recvMessageFrame(Message message, ReceiveFlag flag) {
        socket.recvMessageFrame(message, flag);
    }

    int recvMessageFrameNoWait(Message message, ReceiveFlag flag) {
        return socket.recvMessageFrameNoWait(message, flag);
    }
}
