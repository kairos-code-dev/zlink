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
        socket.sendParts(null, List.of(part), flags, false);
    }

    void send(List<Message> parts) {
        send(parts, SendFlag.NONE);
    }

    void send(List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        socket.sendParts(null, parts, flags, false);
    }

    SendResult trySend(Message part) {
        Objects.requireNonNull(part, "part");
        return socket.trySendParts(null, List.of(part));
    }

    SendResult trySend(List<Message> parts) {
        Objects.requireNonNull(parts, "parts");
        return socket.trySendParts(null, parts);
    }

    void send(RoutingId rid, Message part) {
        send(rid, part, SendFlag.NONE);
    }

    void send(RoutingId rid, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        socket.sendParts(rid, List.of(part), flags, false);
    }

    void send(RoutingId rid, List<Message> parts) {
        send(rid, parts, SendFlag.NONE);
    }

    void send(RoutingId rid, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        socket.sendParts(rid, parts, flags, false);
    }

    SendResult trySend(RoutingId rid, Message part) {
        Objects.requireNonNull(part, "part");
        return socket.trySendParts(rid, List.of(part));
    }

    SendResult trySend(RoutingId rid, List<Message> parts) {
        Objects.requireNonNull(parts, "parts");
        return socket.trySendParts(rid, parts);
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
                RoutingId rid = Socket.toRoutingId(received.routingId());
                Message[] parts = Message.fromOwnedMsgVector(received.parts(),
                    received.partCount());
                return new Received(rid, parts);
            }

            int errno = Native.errno();
            if (errno == Socket.ERRNO_EINTR)
                continue;
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    Optional<Received> tryRecv() {
        while (true) {
            Native.MultipartReceive received = Native.recvMultipart(socket.handle(),
                ReceiveFlag.DONTWAIT.getValue());
            if (received != null) {
                RoutingId rid = Socket.toRoutingId(received.routingId());
                Message[] parts = Message.fromOwnedMsgVector(received.parts(),
                    received.partCount());
                return Optional.of(new Received(rid, parts));
            }

            int errno = Native.errno();
            if (errno == Socket.ERRNO_EINTR)
                continue;
            if (errno == Socket.ERRNO_EAGAIN
                || errno == Socket.ERRNO_EWOULDBLOCK_WIN) {
                return Optional.empty();
            }
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    void sendMessageFrame(Message message, SendFlag flag) {
        socket.sendMessageFrame(message, flag);
    }

    boolean trySendMessageFrame(Message message, SendFlag flag) {
        return socket.trySendMessageFrame(message, flag);
    }

    void recvMessageFrame(Message message, ReceiveFlag flag) {
        socket.recvMessageFrame(message, flag);
    }

    int tryRecvMessageFrame(Message message, ReceiveFlag flag) {
        return socket.tryRecvMessageFrame(message, flag);
    }
}
