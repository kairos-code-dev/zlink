/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.ReceiveFlag;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.sockets.SendFlag;
import systems.zlink.contracts.sockets.SendResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.RecvScratch;

final class TopicPlane {
    private final NativeSocketRuntime socket;

    TopicPlane(NativeSocketRuntime socket) {
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
        TopicMessage message = subscribeInternal(flags,
            flags == ReceiveFlag.DONTWAIT);
        if (message == null) {
            throw new ZlinkRecvException(RecvResult.NO_DATA, Native.errno());
        }
        return message;
    }

    Optional<TopicMessage> subscribeNoWait() {
        return Optional.ofNullable(subscribeInternal(ReceiveFlag.DONTWAIT,
            true));
    }

    boolean subscribe(TopicMessage result, ReceiveFlag flags) {
        Objects.requireNonNull(result, "result");
        if (flags == ReceiveFlag.DONTWAIT) {
            return subscribeIntoFastNoWait(result);
        }
        TopicMessage fresh = subscribe(flags);
        if (fresh == null)
            return false;
        result.adoptFrom(fresh);
        return true;
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
            MemorySegment topicOut = arena.allocate(
                NativeSocketRuntime.TOPIC_CAPACITY);
            MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
            topicLenOut.set(ValueLayout.JAVA_LONG, 0,
                NativeSocketRuntime.TOPIC_CAPACITY);

            int rc = Native.subscriptionEvent(socket.handle(), rid,
                subscribedOut, topicOut, topicLenOut, flags.getValue());
            if (rc != 0) {
                int errno = Native.errno();
                if (errno == NativeSocketRuntime.ERRNO_EINTR) {
                    return subscriptionEvent(flags);
                }
                if (flags == ReceiveFlag.DONTWAIT
                    && (errno == NativeSocketRuntime.ERRNO_EAGAIN
                        || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN)) {
                    throw new ZlinkRecvException(RecvResult.NO_DATA, errno);
                }
                throw ZlinkException.fromErrno("zlink_xpub_recv_part", errno);
            }

            return subscriptionEventFromNative(rid, subscribedOut, topicOut,
                topicLenOut);
        }
    }

    Optional<SubscriptionEvent> trySubscriptionEvent() {
        socket.ensureOpen();
        socket.prepareRecvLikeOperation();
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment rid = arena.allocate(
                    NativeLayouts.ROUTING_ID_LAYOUT);
                rid.set(ValueLayout.JAVA_BYTE,
                    NativeLayouts.ROUTING_ID_SIZE_OFFSET, (byte) 0);
                MemorySegment subscribedOut = arena.allocate(
                    ValueLayout.JAVA_INT);
                MemorySegment topicOut = arena.allocate(
                    NativeSocketRuntime.TOPIC_CAPACITY);
                MemorySegment topicLenOut = arena.allocate(
                    ValueLayout.JAVA_LONG);
                topicLenOut.set(ValueLayout.JAVA_LONG, 0,
                    NativeSocketRuntime.TOPIC_CAPACITY);

                int rc = Native.subscriptionEvent(socket.handle(), rid,
                    subscribedOut, topicOut, topicLenOut,
                    ReceiveFlag.DONTWAIT.getValue());
                if (rc == 0) {
                    return Optional.of(subscriptionEventFromNative(rid,
                        subscribedOut, topicOut, topicLenOut));
                }
            }

            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR) {
                continue;
            }
            if (errno == NativeSocketRuntime.ERRNO_EAGAIN
                || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN) {
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
                MemorySegment topicOut = arena.allocate(
                    NativeSocketRuntime.TOPIC_CAPACITY);
                MemorySegment topicLenOut = arena.allocate(
                    ValueLayout.JAVA_LONG);
                MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
                topicLenOut.set(ValueLayout.JAVA_LONG, 0,
                    NativeSocketRuntime.TOPIC_CAPACITY);
                ArrayList<Message> parts = new ArrayList<>();
                RoutingId routingId = null;
                String topicId = "";
                while (true) {
                    Message part = new Message();
                    boolean success = false;
                    try {
                        int rc = Native.subscribePart(socket.handle(), ridOut,
                            topicOut, NativeSocketRuntime.TOPIC_CAPACITY,
                            topicLenOut,
                            InternalAccess.messageNativeHandle(part),
                            hasMoreOut, flags.getValue());
                        if (rc == 0) {
                            success = true;
                            InternalAccess.messageFinishReceive(part,
                                hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                            if (parts.isEmpty()) {
                                routingId = NativeSocketRuntime.toRoutingId(
                                    NativeSocketRuntime.decodeRoutingIdPtr(
                                        ridOut.get(ValueLayout.ADDRESS, 0)));
                                topicId = decodeTopic(topicOut, topicLenOut);
                            }
                            parts.add(part);
                            if (!part.more()) {
                                return new TopicMessage(routingId, topicId,
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
                    if (errno == NativeSocketRuntime.ERRNO_EINTR) {
                        break;
                    }
                    if (allowNoData
                        && (errno == NativeSocketRuntime.ERRNO_EAGAIN
                            || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN)) {
                        return null;
                    }
                    throw ZlinkException.fromLastError("zlink_subscribe_part");
                }
            }
        }
    }

    // Non-allocating subscribe hot path for the DONT_WAIT single-part case
    // (the perf/streaming common path). Multipart payloads fall back to the
    // general allocating reader to preserve exact semantics.
    private boolean subscribeIntoFastNoWait(TopicMessage result) {
        socket.ensureOpen();
        socket.prepareRecvLikeOperation();
        RecvScratch scratch = socket.recvScratch();
        while (true) {
            scratch.topicLenOut.set(ValueLayout.JAVA_LONG, 0,
                RecvScratch.TOPIC_CAPACITY);
            Message part = new Message();
            boolean success = false;
            try {
                int rc = Native.subscribePartNoWaitCritical(socket.handle(),
                    scratch.sourceRidOut, scratch.topicOut,
                    RecvScratch.TOPIC_CAPACITY, scratch.topicLenOut,
                    InternalAccess.messageNativeHandle(part),
                    scratch.hasMoreOut, ReceiveFlag.DONTWAIT.getValue());
                if (rc == 0) {
                    boolean hasMore =
                        scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(part, hasMore);
                    if (hasMore) {
                        success = true;
                        TopicMessage fresh = subscribeAssembleRemainder(
                            scratch, part);
                        result.adoptFrom(fresh);
                        return true;
                    }
                    RoutingId routingId = NativeSocketRuntime.toRoutingId(
                        NativeSocketRuntime.decodeRoutingIdPtr(
                            scratch.sourceRidOut.get(ValueLayout.ADDRESS, 0)));
                    int topicLength = NativeSocketRuntime.normalizeTopicLength(
                        scratch.topicOut, RecvScratch.TOPIC_CAPACITY,
                        scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                    String topicId = cachedTopicString(scratch,
                        scratch.topicOut, topicLength);
                    success = true;
                    InternalAccess.topicMessageAdoptSingle(result, routingId,
                        topicId, part);
                    return true;
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
            if (errno == NativeSocketRuntime.ERRNO_EINTR) {
                continue;
            }
            if (errno == NativeSocketRuntime.ERRNO_EAGAIN
                || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_subscribe_part");
        }
    }

    private static String cachedTopicString(RecvScratch scratch,
                                            MemorySegment topicOut,
                                            int topicLength) {
        if (topicLength == 0) {
            return "";
        }
        byte[] cached = scratch.cachedTopicBytes;
        if (cached != null && cached.length == topicLength) {
            boolean same = true;
            for (int i = 0; i < topicLength; i++) {
                if (cached[i] != topicOut.get(ValueLayout.JAVA_BYTE, i)) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return scratch.cachedTopicString;
            }
        }
        byte[] raw = topicOut.asSlice(0, topicLength)
            .toArray(ValueLayout.JAVA_BYTE);
        String decoded = new String(raw, StandardCharsets.UTF_8);
        scratch.cachedTopicBytes = raw;
        scratch.cachedTopicString = decoded;
        return decoded;
    }

    private TopicMessage subscribeAssembleRemainder(RecvScratch scratch,
                                                    Message firstPart) {
        RoutingId routingId = NativeSocketRuntime.toRoutingId(
            NativeSocketRuntime.decodeRoutingIdPtr(
                scratch.sourceRidOut.get(ValueLayout.ADDRESS, 0)));
        int topicLength = NativeSocketRuntime.normalizeTopicLength(
            scratch.topicOut, RecvScratch.TOPIC_CAPACITY,
            scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
        String topicId = topicLength == 0 ? ""
            : new String(scratch.topicOut.asSlice(0, topicLength)
                .toArray(ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
        ArrayList<Message> parts = new ArrayList<>();
        parts.add(firstPart);
        while (true) {
            Message next = new Message();
            boolean ok = false;
            try {
                int rc = Native.subscribePart(socket.handle(),
                    scratch.sourceRidOut, scratch.topicOut,
                    RecvScratch.TOPIC_CAPACITY, scratch.topicLenOut,
                    InternalAccess.messageNativeHandle(next),
                    scratch.hasMoreOut, ReceiveFlag.NONE.getValue());
                if (rc == 0) {
                    ok = true;
                    boolean more =
                        scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(next, more);
                    parts.add(next);
                    if (!more) {
                        return new TopicMessage(routingId, topicId,
                            parts.toArray(Message[]::new));
                    }
                    continue;
                }
            } finally {
                if (!ok) {
                    try {
                        next.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR) {
                continue;
            }
            Message.closeAll(parts);
            throw ZlinkException.fromLastError("zlink_subscribe_part");
        }
    }

    void setRoutingId(RoutingId rid) {
        Objects.requireNonNull(rid, "rid");
        byte[] value = InternalAccess.routingIdTrustedBytes(rid);
        socket.setRoutingIdBytes(value, 0, value.length);
    }

    RoutingId getRoutingId() {
        return RoutingId.from(socket.getRoutingIdBytes());
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
                MemorySegment isPatternOut = arena.allocate(
                    ValueLayout.JAVA_INT);
                byte[] filter = socket.subscriptionAt(index, lenInOut,
                    isPatternOut, capacity);
                if (filter == null) {
                    break;
                }
                capacity = Math.max(capacity, filter.length);
                out.add(SubscriptionEntry.fromBytes(filter,
                    isPatternOut.get(ValueLayout.JAVA_INT, 0) != 0));
            }
        }
        return List.copyOf(out);
    }

    private static SubscriptionEvent subscriptionEventFromNative(
        MemorySegment rid,
        MemorySegment subscribedOut,
        MemorySegment topicOut,
        MemorySegment topicLenOut) {
        int topicLength = NativeSocketRuntime.normalizeTopicLength(topicOut,
            NativeSocketRuntime.TOPIC_CAPACITY,
            topicLenOut.get(ValueLayout.JAVA_LONG, 0));
        String filter = topicLength == 0 ? ""
            : new String(topicOut.asSlice(0, topicLength).toArray(
                ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
        return new SubscriptionEvent(Optional.ofNullable(
            NativeSocketRuntime.toRoutingId(
                NativeSocketRuntime.decodeRoutingId(rid))),
            filter, subscribedOut.get(ValueLayout.JAVA_INT, 0) != 0);
    }

    private static String decodeTopic(MemorySegment topicOut,
                                      MemorySegment topicLenOut) {
        int topicLength = NativeSocketRuntime.normalizeTopicLength(topicOut,
            NativeSocketRuntime.TOPIC_CAPACITY,
            topicLenOut.get(ValueLayout.JAVA_LONG, 0));
        if (topicLength == 0) {
            return "";
        }
        return new String(topicOut.asSlice(0, topicLength)
            .toArray(ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
    }

    private static void validateTopicUtf8(String topicId, String name) {
        int chars = topicId.length();
        if ((long) chars * 3L < NativeSocketRuntime.TOPIC_CAPACITY) {
            return;
        }
        int bytes = topicId.getBytes(StandardCharsets.UTF_8).length;
        validateFilterBytes(bytes, name);
    }

    private static void validateFilterBytes(int bytes, String name) {
        if (bytes >= NativeSocketRuntime.TOPIC_CAPACITY) {
            throw new IllegalArgumentException(name + " too long: " + bytes
                + " >= " + NativeSocketRuntime.TOPIC_CAPACITY);
        }
        if (bytes < 0) {
            throw new IllegalArgumentException(
                name + " length must be non-negative");
        }
    }
}
