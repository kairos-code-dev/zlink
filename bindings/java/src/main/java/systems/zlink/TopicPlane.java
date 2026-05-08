/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.Native;
import systems.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

final class TopicPlane {
    private static final int MAX_TOPIC_OR_FILTER_BYTES = Socket.TOPIC_CAPACITY - 1;

    private final Socket socket;

    TopicPlane(Socket socket) {
        this.socket = socket;
    }

    void publish(String topicId, Message part) {
        publish(topicId, part, SendFlag.NONE);
    }

    void publish(String topicId, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        validateTopicUtf8(topicId, "topicId");
        socket.publishMessageFrame(topicId, part, flags);
    }

    void publish(String topicId, List<Message> parts) {
        publish(topicId, parts, SendFlag.NONE);
    }

    void publish(String topicId, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        validateTopicUtf8(topicId, "topicId");
        socket.publishParts(topicId, parts, flags, false);
    }

    SendResult publishNoWaitResult(String topicId, Message part) {
        Objects.requireNonNull(part, "part");
        validateTopicUtf8(topicId, "topicId");
        return socket.publishMessageFrameNoWaitResult(topicId, part);
    }

    SendResult publishNoWaitResult(String topicId, List<Message> parts) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(parts, "parts");
        validateTopicUtf8(topicId, "topicId");
        return socket.publishNoWaitPartsResult(topicId, parts);
    }

    TopicMessage subscribe() {
        return subscribe(ReceiveFlag.NONE);
    }

    TopicMessage subscribe(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        TopicMessage message = subscribeInternal(flags, flags == ReceiveFlag.DONTWAIT);
        if (message == null) {
            throw new RecvException(RecvResult.NO_DATA, Native.errno());
        }
        return message;
    }

    Optional<TopicMessage> subscribeNoWait() {
        return Optional.ofNullable(subscribeInternal(ReceiveFlag.DONTWAIT, true));
    }

    SubscriptionEvent subscriptionEvent(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        socket.ensureOpen();
        socket.prepareRecvLikeOperation();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            rid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
                (byte) 0);
            MemorySegment subscribedOut = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment topicOut = arena.allocate(Socket.TOPIC_CAPACITY);
            MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
            topicLenOut.set(ValueLayout.JAVA_LONG, 0, Socket.TOPIC_CAPACITY);

            int rc = Native.subscriptionEvent(socket.handle(), rid, subscribedOut,
                topicOut, topicLenOut, flags.getValue());
            if (rc != 0) {
                int errno = Native.errno();
                if (errno == Socket.ERRNO_EINTR) {
                    return subscriptionEvent(flags);
                }
                if (flags == ReceiveFlag.DONTWAIT
                    && (errno == Socket.ERRNO_EAGAIN
                        || errno == Socket.ERRNO_EWOULDBLOCK_WIN)) {
                    throw new RecvException(RecvResult.NO_DATA, errno);
                }
                throw ZlinkException.fromErrno("zlink_xpub_recv_part", errno);
            }

            int topicLength = Socket.normalizeTopicLength(topicOut, Socket.TOPIC_CAPACITY,
                topicLenOut.get(ValueLayout.JAVA_LONG, 0));
            String filter = topicLength == 0
                ? ""
                : new String(topicOut.asSlice(0, topicLength).toArray(
                    ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
            return new SubscriptionEvent(java.util.Optional.ofNullable(
                Socket.toRoutingId(Socket.decodeRoutingId(rid))),
                java.util.Optional.empty(), filter,
                subscribedOut.get(ValueLayout.JAVA_INT, 0) != 0);
        }
    }

    Optional<SubscriptionEvent> trySubscriptionEvent() {
        socket.ensureOpen();
        socket.prepareRecvLikeOperation();
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment rid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
                rid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
                    (byte) 0);
                MemorySegment subscribedOut = arena.allocate(ValueLayout.JAVA_INT);
                MemorySegment topicOut = arena.allocate(Socket.TOPIC_CAPACITY);
                MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
                topicLenOut.set(ValueLayout.JAVA_LONG, 0, Socket.TOPIC_CAPACITY);

                int rc = Native.subscriptionEvent(socket.handle(), rid, subscribedOut,
                    topicOut, topicLenOut, ReceiveFlag.DONTWAIT.getValue());
                if (rc == 0) {
                    int topicLength = Socket.normalizeTopicLength(topicOut,
                        Socket.TOPIC_CAPACITY,
                        topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                    String filter = topicLength == 0 ? ""
                        : new String(topicOut.asSlice(0, topicLength).toArray(
                            ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
                    return Optional.of(new SubscriptionEvent(
                        java.util.Optional.ofNullable(
                          Socket.toRoutingId(Socket.decodeRoutingId(rid))),
                        java.util.Optional.empty(), filter,
                        subscribedOut.get(ValueLayout.JAVA_INT, 0) != 0));
                }
            }

            int errno = Native.errno();
            if (errno == Socket.ERRNO_EINTR)
                continue;
            if (errno == Socket.ERRNO_EAGAIN
                || errno == Socket.ERRNO_EWOULDBLOCK_WIN) {
                return Optional.empty();
            }
            throw ZlinkException.fromLastError("zlink_xpub_recv_part");
        }
    }

    private TopicMessage subscribeInternal(ReceiveFlag flags,
                                           boolean allowNoData) {
        socket.ensureOpen();
        socket.prepareRecvLikeOperation();
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment ridOut = arena.allocate(ValueLayout.ADDRESS);
                MemorySegment topicOut = arena.allocate(Socket.TOPIC_CAPACITY);
                MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
                MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
                topicLenOut.set(ValueLayout.JAVA_LONG, 0, Socket.TOPIC_CAPACITY);
                ArrayList<Message> parts = new ArrayList<>();
                RoutingId routingId = null;
                String topicId = "";
                while (true) {
                    Message part = new Message();
                    boolean success = false;
                    try {
                        int rc = Native.subscribePart(socket.handle(), ridOut,
                            topicOut, Socket.TOPIC_CAPACITY, topicLenOut,
                            part.nativeHandle(), hasMoreOut, flags.getValue());
                        if (rc == 0) {
                            success = true;
                            part.finishReceive(
                                hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                            if (parts.isEmpty()) {
                                routingId = Socket.toRoutingId(
                                    Socket.decodeRoutingIdPtr(
                                        ridOut.get(ValueLayout.ADDRESS, 0)));
                                int topicLength = Socket.normalizeTopicLength(
                                    topicOut, Socket.TOPIC_CAPACITY,
                                    topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                                topicId = topicLength == 0 ? ""
                                    : new String(topicOut.asSlice(0, topicLength)
                                        .toArray(ValueLayout.JAVA_BYTE),
                                        StandardCharsets.UTF_8);
                            }
                            parts.add(part);
                            if (!part.more()) {
                                return new TopicMessage(routingId, null, topicId,
                                    parts.toArray(Message[]::new));
                            }
                            continue;
                        }
                    } finally {
                        if (!success) {
                            try {
                                part.close();
                            } catch (RuntimeException ignored) {
                            }
                        }
                    }

                    int errno = Native.errno();
                    Message.closeAll(parts);
                    if (errno == Socket.ERRNO_EINTR)
                        break;
                    if (allowNoData && (errno == Socket.ERRNO_EAGAIN
                        || errno == Socket.ERRNO_EWOULDBLOCK_WIN)) {
                        return null;
                    }
                    throw ZlinkException.fromLastError("zlink_subscribe_part");
                }
            }
        }
    }

    void setRoutingId(RoutingId rid) {
        Objects.requireNonNull(rid, "rid");
        byte[] value = rid.trustedBytes();
        socket.setRoutingIdBytes(value, 0, value.length);
    }

    RoutingId routingId() {
        return RoutingId.fromBytes(socket.getRoutingIdBytes());
    }

    void setSubscription(String filter) {
        Objects.requireNonNull(filter, "filter");
        byte[] bytes = filter.getBytes(StandardCharsets.UTF_8);
        validateFilterBytes(bytes.length, "filter");
        socket.setSubscriptionBytes(bytes, 0, bytes.length, true);
    }

    void setSubscription(byte[] filter) {
        Objects.requireNonNull(filter, "filter");
        validateFilterBytes(filter.length, "filter");
        socket.setSubscriptionBytes(filter, 0, filter.length, true);
    }

    void unsetSubscription(String filter) {
        Objects.requireNonNull(filter, "filter");
        byte[] bytes = filter.getBytes(StandardCharsets.UTF_8);
        validateFilterBytes(bytes.length, "filter");
        socket.setSubscriptionBytes(bytes, 0, bytes.length, false);
    }

    void unsetSubscription(byte[] filter) {
        Objects.requireNonNull(filter, "filter");
        validateFilterBytes(filter.length, "filter");
        socket.setSubscriptionBytes(filter, 0, filter.length, false);
    }

    List<SubscriptionEntry> subscriptions() {
        socket.ensureOpen();
        ArrayList<SubscriptionEntry> out = new ArrayList<>();
        int capacity = 64;
        try (Arena arena = Arena.ofConfined()) {
            for (long index = 0;; index++) {
                MemorySegment lenInOut = arena.allocate(ValueLayout.JAVA_LONG);
                MemorySegment isPatternOut = arena.allocate(ValueLayout.JAVA_INT);
                byte[] filter = socket.subscriptionAt(index, lenInOut,
                    isPatternOut, capacity);
                if (filter == null)
                    break;
                capacity = Math.max(capacity, filter.length);
                out.add(SubscriptionEntry.fromBytes(filter,
                    isPatternOut.get(ValueLayout.JAVA_INT, 0) != 0));
            }
        }
        return List.copyOf(out);
    }

    private static void validateTopicUtf8(String topicId, String name) {
        int bytes = topicId.getBytes(StandardCharsets.UTF_8).length;
        validateFilterBytes(bytes, name);
    }

    private static void validateFilterBytes(int bytes, String name) {
        if (bytes >= Socket.TOPIC_CAPACITY) {
            throw new IllegalArgumentException(name + " too long: " + bytes
                + " >= " + Socket.TOPIC_CAPACITY);
        }
        if (bytes < 0) {
            throw new IllegalArgumentException(name + " length must be non-negative");
        }
    }
}
