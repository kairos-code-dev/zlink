/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Objects;

public final class Spot implements AutoCloseable {
    private MemorySegment pubHandle;
    private MemorySegment subHandle;
    private static final long MSG_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();
    private static final int TOPIC_CAPACITY = 256;

    public Spot(SpotNode node) {
        this.pubHandle = Native.spotPubNew(node.handle());
        this.subHandle = Native.spotSubNew(node.handle());
        if (pubHandle == null || pubHandle.address() == 0 || subHandle == null || subHandle.address() == 0) {
            close();
            throw new RuntimeException("zlink_spot_pub_new/zlink_spot_sub_new failed");
        }
    }

    public void publish(String topicId, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment topic = NativeHelpers.toCString(arena, topicId);
            publishInternal(arena, topic, parts, flags, false);
        }
    }

    public void publish(PreparedTopic topic, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topic, "topic");
        try (Arena arena = Arena.ofConfined()) {
            publishInternal(arena, topic.cString(), parts, flags, false);
        }
    }

    public void publishMove(String topicId, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment topic = NativeHelpers.toCString(arena, topicId);
            publishInternal(arena, topic, parts, flags, true);
        }
    }

    public void publishMove(PreparedTopic topic, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topic, "topic");
        try (Arena arena = Arena.ofConfined()) {
            publishInternal(arena, topic.cString(), parts, flags, true);
        }
    }

    public void subscribe(String topicId) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubSubscribe(subHandle, NativeHelpers.toCString(arena, topicId));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_subscribe failed");
        }
    }

    public void subscribe(PreparedTopic topic) {
        Objects.requireNonNull(topic, "topic");
        int rc = Native.spotSubSubscribe(subHandle, topic.cString());
        if (rc != 0)
            throw new RuntimeException("zlink_spot_sub_subscribe failed");
    }

    public void subscribePattern(String pattern) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubSubscribePattern(subHandle, NativeHelpers.toCString(arena, pattern));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_subscribe_pattern failed");
        }
    }

    public void unsubscribe(String topicIdOrPattern) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubUnsubscribe(subHandle, NativeHelpers.toCString(arena, topicIdOrPattern));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_unsubscribe failed");
        }
    }

    public void unsubscribe(PreparedTopic topic) {
        Objects.requireNonNull(topic, "topic");
        int rc = Native.spotSubUnsubscribe(subHandle, topic.cString());
        if (rc != 0)
            throw new RuntimeException("zlink_spot_sub_unsubscribe failed");
    }

    public PreparedTopic prepareTopic(String topicId) {
        return new PreparedTopic(topicId);
    }

    public SpotMessage recv(ReceiveFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment topic = arena.allocate(TOPIC_CAPACITY);
            MemorySegment topicLen = arena.allocate(ValueLayout.JAVA_LONG);
            topicLen.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
            int rc = Native.spotSubRecv(subHandle, partsPtr, count, flags.getValue(), topic, topicLen);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_recv failed");
            long partCount = count.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment partsAddr = partsPtr.get(ValueLayout.ADDRESS, 0);
            byte[][] messages = NativeMsg.readMsgVector(partsAddr, partCount);
            String topicId = NativeHelpers.fromCString(topic, TOPIC_CAPACITY);
            return new SpotMessage(topicId, messages);
        }
    }

    public SpotMessages recvMessages(ReceiveFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment topic = arena.allocate(TOPIC_CAPACITY);
            MemorySegment topicLen = arena.allocate(ValueLayout.JAVA_LONG);
            topicLen.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
            int rc = Native.spotSubRecv(subHandle, partsPtr, count, flags.getValue(),
                topic, topicLen);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_recv failed");
            long partCount = count.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment partsAddr = partsPtr.get(ValueLayout.ADDRESS, 0);
            Message[] parts = Message.fromMsgVector(partsAddr, partCount);
            String topicId = NativeHelpers.fromCString(topic, TOPIC_CAPACITY);
            return new SpotMessages(topicId, parts);
        }
    }

    public RecvContext createRecvContext() {
        return new RecvContext();
    }

    public SpotRawMessage recvRaw(ReceiveFlag flags, RecvContext context) {
        Objects.requireNonNull(context, "context");
        context.ensureOpen();
        context.topicLength().set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
        int rc = Native.spotSubRecv(subHandle,
            context.partsPtr(),
            context.partCount(),
            flags.getValue(),
            context.topicId(),
            context.topicLength());
        if (rc != 0)
            throw new RuntimeException("zlink_spot_sub_recv failed");
        long partCount = context.partCount().get(ValueLayout.JAVA_LONG, 0);
        MemorySegment partsAddr = context.partsPtr().get(ValueLayout.ADDRESS, 0);
        Message[] reusable = Message.fromMsgVector(partsAddr, partCount,
            context.reusableParts());
        context.setReusableParts(reusable);
        int topicLen = normalizeTopicLength(context.topicId(), TOPIC_CAPACITY,
            context.topicLength().get(ValueLayout.JAVA_LONG, 0));
        MemorySegment topicRaw = context.topicId().asSlice(0, topicLen);
        return new SpotRawMessage(topicRaw, reusable);
    }

    @Override
    public void close() {
        if (pubHandle != null && pubHandle.address() != 0) {
            Native.spotPubDestroy(pubHandle);
            pubHandle = MemorySegment.NULL;
        }
        if (subHandle != null && subHandle.address() != 0) {
            Native.spotSubDestroy(subHandle);
            subHandle = MemorySegment.NULL;
        }
    }

    public record SpotMessage(String topicId, byte[][] parts) {}

    public record SpotRawMessage(MemorySegment topicId, Message[] parts) {}

    public static final class PreparedTopic implements AutoCloseable {
        private final String topicId;
        private Arena arena;
        private MemorySegment cString;

        PreparedTopic(String topicId) {
            this.topicId = Objects.requireNonNull(topicId, "topicId");
            this.arena = Arena.ofShared();
            this.cString = NativeHelpers.toCString(arena, topicId);
        }

        public String topicId() {
            return topicId;
        }

        MemorySegment cString() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("prepared topic is closed");
            return cString;
        }

        @Override
        public void close() {
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
            cString = MemorySegment.NULL;
        }
    }

    public static final class SpotMessages implements AutoCloseable {
        private final String topicId;
        private final Message[] parts;

        SpotMessages(String topicId, Message[] parts) {
            this.topicId = topicId == null ? "" : topicId;
            this.parts = parts == null ? new Message[0] : parts;
        }

        public String topicId() {
            return topicId;
        }

        public Message[] parts() {
            return parts;
        }

        @Override
        public void close() {
            Message.closeAll(parts);
        }
    }

    public static final class RecvContext implements AutoCloseable {
        private Arena arena;
        private final MemorySegment partsPtr;
        private final MemorySegment partCount;
        private final MemorySegment topicId;
        private final MemorySegment topicLength;
        private Message[] reusableParts;

        RecvContext() {
            this.arena = Arena.ofShared();
            this.partsPtr = arena.allocate(ValueLayout.ADDRESS);
            this.partCount = arena.allocate(ValueLayout.JAVA_LONG);
            this.topicId = arena.allocate(TOPIC_CAPACITY);
            this.topicLength = arena.allocate(ValueLayout.JAVA_LONG);
            this.reusableParts = new Message[0];
        }

        void ensureOpen() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("recv context is closed");
        }

        MemorySegment partsPtr() {
            ensureOpen();
            return partsPtr;
        }

        MemorySegment partCount() {
            ensureOpen();
            return partCount;
        }

        MemorySegment topicId() {
            ensureOpen();
            return topicId;
        }

        MemorySegment topicLength() {
            ensureOpen();
            return topicLength;
        }

        Message[] reusableParts() {
            ensureOpen();
            return reusableParts;
        }

        void setReusableParts(Message[] parts) {
            ensureOpen();
            reusableParts = parts == null ? new Message[0] : parts;
        }

        @Override
        public void close() {
            Message.closeAll(reusableParts);
            reusableParts = new Message[0];
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
        }
    }

    private void publishInternal(Arena arena,
                                 MemorySegment topicId,
                                 Message[] parts,
                                 SendFlag flags,
                                 boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, parts.length);
        int initialized = 0;
        try {
            for (int i = 0; i < parts.length; i++) {
                Message part = parts[i];
                if (part == null)
                    throw new IllegalArgumentException("parts[" + i + "] is null");
                MemorySegment dest = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
                int rc = NativeMsg.msgInit(dest);
                if (rc != 0)
                    throw new RuntimeException("zlink_msg_init failed");
                initialized++;
                if (move)
                    part.moveTo(dest);
                else
                    part.copyTo(dest);
            }
            int rc = Native.spotPubPublish(pubHandle, topicId, vec, parts.length,
                flags.getValue());
            if (rc != 0)
                throw new RuntimeException("zlink_spot_pub_publish failed");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private static void closeMsgVector(MemorySegment vec, int count) {
        for (int i = 0; i < count; i++) {
            MemorySegment msg = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            NativeMsg.msgClose(msg);
        }
    }

    private static int normalizeTopicLength(MemorySegment topic,
                                            int capacity,
                                            long reportedLength) {
        long len = reportedLength;
        if (len < 0)
            len = 0;
        if (len > capacity)
            len = capacity;
        int topicLen = (int) len;
        if (topicLen > 0 && topic.get(ValueLayout.JAVA_BYTE, topicLen - 1) == 0)
            topicLen--;
        return topicLen;
    }
}
