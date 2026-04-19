/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

final class MessagePlane {
    private final Socket socket;

    MessagePlane(Socket socket) {
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
            if (result != SendResult.SENT)
                throw Socket.submitExceptionFromSendResult(result.nativeValue());
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
            if (result != SendResult.SENT)
                throw Socket.submitExceptionFromSendResult(result.nativeValue());
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
        while (true) {
            Native.MultipartReceive received = Native.recvMultipart(socket.handle(),
                flags.getValue());
            if (received != null) {
                byte[] ridBytes = received.routingId();
                if (received.partCount() == 1) {
                    Message part = Message.fromOwnedMsgSingle(received.parts());
                    return new Received(ridBytes, null, part, 0L, false, null);
                }
                Message[] parts = Message.fromOwnedMsgVector(
                    received.parts(), received.partCount());
                return new Received(ridBytes, null, parts, true, 0L, false, null);
            }

            int errno = Native.errno();
            if (errno == Socket.ERRNO_EINTR)
                continue;
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    Received recvNoWaitOrNull() {
        while (true) {
            Native.MultipartReceive received = Native.recvMultipart(socket.handle(),
                ReceiveFlag.DONTWAIT.getValue());
            if (received != null) {
                byte[] ridBytes = received.routingId();
                if (received.partCount() == 1) {
                    Message part = Message.fromOwnedMsgSingle(received.parts());
                    return new Received(ridBytes, null, part, 0L, false, null);
                }
                Message[] parts = Message.fromOwnedMsgVector(
                    received.parts(), received.partCount());
                return new Received(ridBytes, null, parts, true, 0L, false, null);
            }

            int errno = Native.errno();
            if (errno == Socket.ERRNO_EINTR)
                continue;
            if (errno == Socket.ERRNO_EAGAIN
                || errno == Socket.ERRNO_EWOULDBLOCK_WIN) {
                return null;
            }
            throw ZlinkException.fromLastError("zlink_recv");
        }
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
